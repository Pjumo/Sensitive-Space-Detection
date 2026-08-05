#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "presence_uapi.h"

#define PIR_DEVICE_PATH "/dev/pir_presence"

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static const char *event_name(__u32 event_type)
{
    switch (event_type) {
    case PRESENCE_EVENT_ASSERTED:
        return "ASSERTED";

    case PRESENCE_EVENT_DEASSERTED:
        return "DEASSERTED";

    case PRESENCE_EVENT_ERROR:
        return "ERROR";

    default:
        return "NONE";
    }
}

static int print_device_info(int fd)
{
    __u32 api_version = 0;
    struct presence_caps caps = {0};
    struct presence_state state = {0};

    if (ioctl(
            fd,
            PRESENCE_IOC_GET_API_VERSION,
            &api_version) < 0) {
        perror("PRESENCE_IOC_GET_API_VERSION");
        return -1;
    }

    if (ioctl(
            fd,
            PRESENCE_IOC_GET_CAPS,
            &caps) < 0) {
        perror("PRESENCE_IOC_GET_CAPS");
        return -1;
    }

    if (ioctl(
            fd,
            PRESENCE_IOC_GET_STATE,
            &state) < 0) {
        perror("PRESENCE_IOC_GET_STATE");
        return -1;
    }

    printf("API version  : %u\n", api_version);
    printf("sensor type  : %u\n", caps.sensor_type);
    printf("event size   : %u bytes\n", caps.event_size);
    printf("FIFO depth   : %u events\n", caps.fifo_depth);
    printf("capabilities : 0x%08x\n",
           caps.capability_flags);
    printf("GPIO state   : %u\n", state.raw_value);
    printf("sequence     : %u\n", state.sequence);
    printf("waiting for PIR events...\n");
    printf("press Ctrl+C to stop\n");

    return 0;
}

static void print_statistics(int fd)
{
    struct presence_stats stats = {0};

    if (ioctl(
            fd,
            PRESENCE_IOC_GET_STATS,
            &stats) < 0) {
        perror("PRESENCE_IOC_GET_STATS");
        return;
    }

    printf("\nstatistics\n");

    printf(
        "  total     : %" PRIu64 "\n",
        (uint64_t)stats.total_events);

    printf(
        "  delivered : %" PRIu64 "\n",
        (uint64_t)stats.delivered_events);

    printf(
        "  dropped   : %" PRIu64 "\n",
        (uint64_t)stats.dropped_events);

    printf(
        "  last time : %" PRIu64 " ns\n",
        (uint64_t)stats.last_timestamp_ns);
}

int main(int argc, char **argv)
{
    const char *device_path = PIR_DEVICE_PATH;
    struct sigaction signal_action = {0};
    int fd;

    if (argc > 2) {
        fprintf(
            stderr,
            "usage: %s [device-path]\n",
            argv[0]);

        return EXIT_FAILURE;
    }

    if (argc == 2)
        device_path = argv[1];

    signal_action.sa_handler = handle_signal;
    sigemptyset(&signal_action.sa_mask);

    if (sigaction(
            SIGINT,
            &signal_action,
            NULL) < 0) {
        perror("sigaction SIGINT");
        return EXIT_FAILURE;
    }

    if (sigaction(
            SIGTERM,
            &signal_action,
            NULL) < 0) {
        perror("sigaction SIGTERM");
        return EXIT_FAILURE;
    }

    fd = open(
        device_path,
        O_RDONLY | O_CLOEXEC | O_NONBLOCK);

    if (fd < 0) {
        perror(device_path);
        return EXIT_FAILURE;
    }

    if (print_device_info(fd) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    while (!stop_requested) {
        struct pollfd poll_descriptor = {
            .fd = fd,
            .events = POLLIN,
        };

        struct presence_event event;
        ssize_t bytes;
        int ret;

        ret = poll(&poll_descriptor, 1, 1000);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        if (ret == 0)
            continue;

        if (poll_descriptor.revents &
            (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(
                stderr,
                "device poll error: revents=0x%x\n",
                poll_descriptor.revents);

            break;
        }

        if (!(poll_descriptor.revents & POLLIN))
            continue;

        bytes = read(fd, &event, sizeof(event));

        if (bytes < 0) {
            if (errno == EAGAIN ||
                errno == EINTR)
                continue;

            perror("read");
            break;
        }

        if ((size_t)bytes != sizeof(event)) {
            fprintf(
                stderr,
                "unexpected event size: %zd\n",
                bytes);

            break;
        }

        printf(
            "event seq=%u type=%s raw=%u "
            "time=%" PRIu64 " ns flags=0x%08x%s\n",
            event.sequence,
            event_name(event.event_type),
            event.raw_value,
            (uint64_t)event.timestamp_ns,
            event.flags,
            (event.flags &
             PRESENCE_EVENT_FLAG_DROPPED_BEFORE)
                ? " [older event dropped]"
                : "");

        fflush(stdout);
    }

    print_statistics(fd);

    close(fd);

    return EXIT_SUCCESS;
}
