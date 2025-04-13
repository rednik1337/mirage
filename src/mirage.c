#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include "mirage.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aeterna");
MODULE_DESCRIPTION("Map pages from one process into another");


static struct page *get_page_by_addr(struct task_struct *task, unsigned long addr) {
    struct mm_struct *mm = task->mm;
    pgd_t *pgd = pgd_offset(mm, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        pr_err("pgd_offset failed");
        return 0;
    }

    p4d_t *p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        pr_err("p4d_offset failed");
        return 0;
    }

    pud_t *pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        pr_err("pud_offset failed");
        return 0;
    }

    pmd_t *pmd = pmd_offset(pud, addr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        pr_err("pmd_offset failed");
        return 0;
    }

    pte_t *pte = pte_offset_kernel(pmd, addr);
    if (!pte) {
        pr_err("pte_offset failed");
        pte_unmap(pte);
        return 0;
    }

    struct page *page = pte_page(*pte);

    pte_unmap(pte);
    return page;
}


static struct task_struct *get_task(pid_t pid) {
    struct task_struct *task = pid_task(find_vpid(pid), PIDTYPE_PID);
    return task;
}


static int ioctl_map_page(struct task_struct *src_task, unsigned long src_addr, struct task_struct *dst_task, unsigned long dst_addr) {
    struct page *src_page = get_page_by_addr(src_task, src_addr);
    if (!src_page) {
        pr_err("pagewalk failed");
        return -EFAULT;
    }


    down_read(&dst_task->mm->mmap_lock);
    struct vm_area_struct *vma = find_vma(dst_task->mm, dst_addr);
    up_read(&dst_task->mm->mmap_lock);
    if (!vma) {
        pr_err("find_vma failed");
        return -EFAULT;
    }

    down_write(&dst_task->mm->mmap_lock);
    int ret = vm_insert_page(vma, dst_addr, src_page);
    up_write(&dst_task->mm->mmap_lock);

    if (ret < 0) {
        pr_err("vm_insert_page failed");
        return -EFAULT;
    }
    return 0;
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
            return ioctl_map_page(get_task(arg.src_pid), arg.src_addr, get_task(arg.dst_pid), arg.dst_addr);
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

