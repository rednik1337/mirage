#include <linux/init.h>
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include "mirage.h"
#include "asm/pgtable.h"
#include "linux/mm_types.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aeterna");
MODULE_DESCRIPTION("Map pages from one process into another");


static struct task_struct *get_task(pid_t pid) {
    struct task_struct *task = pid_task(find_vpid(pid), PIDTYPE_PID);
    return task;
}


static int ioctl_map_vma(struct task_struct *src_task, unsigned long src_addr, struct task_struct *dst_task, unsigned long dst_addr, unsigned long size) {
    struct vm_area_struct *dst_vma, *src_vma;
    unsigned long src_vma_page_size, dst_vma_page_size, page_size;
    unsigned long total_pages;

    int ret = 0;

    pr_info("Mapping %lx to %lx (%lu MB)\n", src_addr, dst_addr, size >> 20);

    down_write(&dst_task->mm->mmap_lock);
    down_write(&src_task->mm->mmap_lock);

    
    dst_vma = find_vma(dst_task->mm, dst_addr);
    if (dst_vma) {
        pr_info("Found dst_vma: %lx-%lx %lx\n", dst_vma->vm_start, dst_vma->vm_end, dst_vma->vm_flags);
    } else {
        pr_err("find_vma(dst_task->mm, dst_addr) failed for 0x%lx\n", dst_addr);
        ret = -EFAULT;
        goto unlock_return;
    }

    src_vma = find_vma(src_task->mm, src_addr);
    if (src_vma) {
        pr_info("Found src_vma: %lx-%lx %lx\n", src_vma->vm_start, src_vma->vm_end, src_vma->vm_flags);
    } else {
        pr_err("find_vma(src_task->mm, src_addr) failed for 0x%lx\n", src_addr);
        ret = -EFAULT;
        goto unlock_return;
    }
    

    src_vma_page_size = vma_kernel_pagesize(src_vma);
    dst_vma_page_size = vma_kernel_pagesize(dst_vma);
    if (src_vma_page_size != dst_vma_page_size) {
        pr_err("src_vma_page_size (0x%lx) doesnt match dst_vma_page_size (0x%lx)\n", src_vma_page_size, dst_vma_page_size);
        ret = -EFAULT;
        goto unlock_return;
    }
    page_size = src_vma_page_size;


    total_pages = size / page_size;
    struct page **pages = vmalloc(total_pages * sizeof(struct page *));
    if (!pages) {
        pr_err("vmalloc(total_pages * sizeof(struct page *)) failed\n");
        ret = -ENOMEM;
        goto unlock_return;
    }

    unsigned long pinned = get_user_pages_remote(src_task->mm, src_addr, total_pages, FOLL_GET, pages, 0);
    if (pinned != total_pages) {
        pr_err("pinned pages (%lu) != total_pages (%lu)\n", pinned, total_pages);
        ret = -EFAULT;
        goto free_unlock_return;
    }

    for (unsigned long i = 0; i < pinned; ++i) {
        unsigned long pfn = page_to_pfn(pages[i]);
        int res = remap_pfn_range(dst_vma, dst_addr + i * page_size, pfn, page_size, dst_vma->vm_page_prot);
        if (res < 0) {
            pr_err("remap_pfn_range(dst_vma, dst_addr + i * page_size, pfn, page_size, dst_vma->vm_page_prot) failed at pages[%lu]. stopping now\n", i);
            goto free_unlock_return;
        }
    }


    pr_info("map_vma completed, %lx pages were mapped\n", pinned);


free_unlock_return:
    for (int i = 0; i < pinned; ++i)
        put_page(pages[i]);
    vfree(pages);
 
 
unlock_return:
    up_write(&src_task->mm->mmap_lock);
    up_write(&dst_task->mm->mmap_lock);
    return ret;
}
    


static int dev_open(struct inode *, struct file *) {
    return 0;
}

static int dev_release(struct inode *, struct file *) {
    return 0;
}

static long dev_ioctl(struct file *, unsigned int cmd, unsigned long parg) {
    mirage_ioctl_arg arg;

    switch (cmd) {
        case MIRAGE_IOCTL_MAP:
            if (copy_from_user(&arg, (mirage_ioctl_arg __user *)parg, sizeof(arg)))
                return -EFAULT;
            return ioctl_map_vma(get_task(arg.src_pid), arg.src_addr, get_task(arg.dst_pid), arg.dst_addr, arg.size);
        default:
            return -EINVAL;
    }

    return 0;
}

static struct file_operations fops = {
    .owner            = THIS_MODULE,
    .open             = dev_open,
    .release          = dev_release,
    .unlocked_ioctl   = dev_ioctl,
};


static struct class *cls;
static int major;
static int __init mirage_init(void) {
    major = register_chrdev(0, MIRAGE_DEVICE_NAME, &fops);

    cls = class_create(MIRAGE_DEVICE_NAME);
    device_create(cls, NULL, MKDEV(major, 0), NULL, MIRAGE_DEVICE_NAME);

    pr_info("Mirage loaded\n");

    return 0;
}
 

static void __exit mirage_exit(void) {
    device_destroy(cls, MKDEV(major, 0));

    class_destroy(cls);
    unregister_chrdev(major, MIRAGE_DEVICE_NAME);

    pr_info("Mirage unloaded\n");
}


module_init(mirage_init);
module_exit(mirage_exit);
