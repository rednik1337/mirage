#pragma once

#define MIRAGE_DEVICE_NAME "mirage"
#define MIRAGE_DEVICE_PATH "/dev/" MIRAGE_DEVICE_NAME

typedef struct mirage_ioctl_arg {
    pid_t src_pid;
    pid_t dst_pid;
    unsigned long src_addr;
    unsigned long dst_addr;
} mirage_ioctl_arg;

#define MIRAGE_IOCTL_MAP _IOW('m', 0, mirage_ioctl_arg)
