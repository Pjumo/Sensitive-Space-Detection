#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include "real_backend.h"

#define PIR_DEVICE     "/dev/pir_dev"
#define PIR_DEV_MAJOR  230
#define PIR_DEV_MINOR  0

static int fd = -1;

static int create_device_node(void)
{
    dev_t dev_num;

    if (access(PIR_DEVICE, F_OK) == 0)
        return 0;

    if (errno != ENOENT) {
        perror("access");
        return -1;
    }

    dev_num = makedev(PIR_DEV_MAJOR, PIR_DEV_MINOR);

    if (mknod(PIR_DEVICE,
              S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
              dev_num) < 0) {
        perror("mknod");
        return -1;
    }

    return 0;
}

int real_backend_init(void)
{
    if (create_device_node() < 0)
        return -1;

    /* 논블로킹으로 열어야 poll과 함께 안전하게 씀 */
    fd = open(PIR_DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    printf("[real] PIR 실물 드라이버 연결 완료 (%s)\n", PIR_DEVICE);
    return 0;
}

int real_backend_wait_read(struct presence_event *ev, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeout_ms);

    if (ret < 0) {
        if (errno == EINTR)
            return 0;   /* 시그널로 깬 경우, 타임아웃처럼 취급 */
        perror("poll");
        return -1;
    }

    if (ret == 0) {
        return 0;   /* 타임아웃, 이벤트 없음 */
    }

    if (pfd.revents & (POLLERR | POLLHUP)) {
        fprintf(stderr, "[real] 장치 오류 (POLLERR/POLLHUP)\n");
        return -1;
    }

    if (!(pfd.revents & POLLIN))
        return 0;

    ssize_t n = read(fd, ev, sizeof(*ev));

    if (n < 0) {
        if (errno == EAGAIN)
            return 0;   /* 논블로킹인데 막상 읽을 게 없었던 경우 */
        perror("read");
        return -1;
    }

    if (n != sizeof(*ev)) {
        fprintf(stderr, "[real] 비정상 read 크기: %zd\n", n);
        return -1;
    }

    return 1;   /* 이벤트 정상 수신 */
}

void real_backend_close(void)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    printf("[real] PIR 실물 드라이버 연결 종료\n");
}
