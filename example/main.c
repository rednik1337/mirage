#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "../src/mirage.h"

int main() {
    setbuf(stdout, 0);
    mirage_ioctl_arg arg;

    printf("src_pid: ");
    scanf("%d", &arg.src_pid);

    printf("src_addr (hex): ");
    scanf("%lx", &arg.src_addr);

    printf("pid: %d\naddr: %p\n", arg.src_pid, (void *)arg.src_addr);

    arg.dst_pid = getpid();
    arg.dst_addr = (unsigned long)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    puts("Sending..");

    int fd = open(MIRAGE_DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        exit(1);
    }

    if (ioctl(fd, MIRAGE_IOCTL_MAP, &arg) < 0) {
        perror("ioctl failed");
        close(fd);
        exit(1);
    }
    close(fd);

    puts("OK, completed");


    FILE *file = fopen("output.bin", "wb");
    fwrite((void *)arg.dst_addr, 1, 0x1000, file);
    fclose(file);

    return 0;
}
