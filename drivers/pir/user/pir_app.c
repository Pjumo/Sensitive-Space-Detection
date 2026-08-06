#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>

#include "presence_uapi.h"

#define PIR_DEVICE     "/dev/pir_dev"
#define PIR_DEV_MAJOR  230
#define PIR_DEV_MINOR  0


static int createDeviceNode(void)
{
        dev_t dev_num;

        if(access(PIR_DEVICE, F_OK) == 0)
                return 0;

        if(errno != ENOENT)
        {
                perror("access");
                return -1;
        }

        dev_num = makedev(
                PIR_DEV_MAJOR,
                PIR_DEV_MINOR
        );

        if(mknod(
                PIR_DEVICE,
                S_IFCHR |
                S_IRUSR | S_IWUSR |
                S_IRGRP | S_IWGRP |
                S_IROTH | S_IWOTH,
                dev_num) < 0)
        {
                perror("mknod");
                return -1;
        }

        return 0;
}


static const char *sensorTypeToString(__u32 sensor_type)
{
        switch(sensor_type)
        {
        case PRESENCE_SENSOR_PIR:
                return "PIR";

        case PRESENCE_SENSOR_RADAR:
                return "RADAR";

        default:
                return "UNKNOWN";
        }
}


static const char *eventTypeToString(__u32 event_type)
{
        switch(event_type)
        {
        case PRESENCE_EVENT_NONE:
                return "NONE";

        case PRESENCE_EVENT_ASSERTED:
                return "ASSERTED";

        case PRESENCE_EVENT_DEASSERTED:
                return "DEASSERTED";

        case PRESENCE_EVENT_ERROR:
                return "ERROR";

        default:
                return "UNKNOWN";
        }
}


static void printCapabilities(const struct presence_caps *caps)
{
        printf("\n[Capabilities]\n");
        printf("API version : %u\n", caps->api_version);
        printf("Sensor      : %s\n",
               sensorTypeToString(caps->sensor_type));
        printf("Event size  : %u bytes\n", caps->event_size);
        printf("FIFO depth  : %u\n", caps->fifo_depth);

        printf("Functions   :");

        if(caps->capability_flags & PRESENCE_CAP_READ)
                printf(" READ");

        if(caps->capability_flags & PRESENCE_CAP_POLL)
                printf(" POLL");

        if(caps->capability_flags &
           PRESENCE_CAP_CURRENT_STATE)
        {
                printf(" CURRENT_STATE");
        }

        if(caps->capability_flags & PRESENCE_CAP_STATS)
                printf(" STATS");

        if(caps->capability_flags &
           PRESENCE_CAP_RISING_EDGE)
        {
                printf(" RISING_EDGE");
        }

        if(caps->capability_flags &
           PRESENCE_CAP_SINGLE_READER)
        {
                printf(" SINGLE_READER");
        }

        printf("\n");
}


static void printState(const struct presence_state *state)
{
        printf("\n[Current state]\n");
        printf("Sensor      : %s\n",
               sensorTypeToString(state->sensor_type));
        printf("Raw value   : %u\n", state->raw_value);
        printf("Sequence    : %u\n", state->sequence);
        printf("Timestamp   : %llu ns\n",
               (unsigned long long)
               state->last_timestamp_ns);
}


static void printEvent(const struct presence_event *event)
{
        printf("\n[Presence event]\n");
        printf("API version : %u\n", event->api_version);
        printf("Sensor      : %s\n",
               sensorTypeToString(event->sensor_type));
        printf("Event       : %s\n",
               eventTypeToString(event->event_type));
        printf("Sequence    : %u\n", event->sequence);
        printf("Timestamp   : %llu ns\n",
               (unsigned long long)
               event->timestamp_ns);
        printf("Raw value   : %u\n", event->raw_value);

        if(event->flags &
           PRESENCE_EVENT_FLAG_DROPPED_BEFORE)
        {
                printf("Warning     : previous event dropped\n");
        }
}


static void printStats(int fd)
{
        struct presence_stats stats;

        memset(&stats, 0, sizeof(stats));

        if(ioctl(
                fd,
                PRESENCE_IOC_GET_STATS,
                &stats) < 0)
        {
                perror("ioctl(GET_STATS)");
                return;
        }

        printf(
                "Stats: total=%llu, delivered=%llu, "
                "dropped=%llu\n",
                (unsigned long long)stats.total_events,
                (unsigned long long)stats.delivered_events,
                (unsigned long long)stats.dropped_events
        );
}


int main(void)
{
        int fd;
        int ret;
        __u32 api_version;

        struct presence_caps caps;
        struct presence_state state;
        struct presence_event event;
        struct pollfd pfd;

        ssize_t read_size;

        if(createDeviceNode() < 0)
                return 1;

        fd = open(
                PIR_DEVICE,
                O_RDONLY | O_NONBLOCK
        );

        if(fd < 0)
        {
                perror("open");
                return 1;
        }

        api_version = 0;

        if(ioctl(
                fd,
                PRESENCE_IOC_GET_API_VERSION,
                &api_version) < 0)
        {
                perror("ioctl(GET_API_VERSION)");
                close(fd);
                return 1;
        }

        printf("Presence API version: %u\n", api_version);

        memset(&caps, 0, sizeof(caps));

        if(ioctl(
                fd,
                PRESENCE_IOC_GET_CAPS,
                &caps) < 0)
        {
                perror("ioctl(GET_CAPS)");
                close(fd);
                return 1;
        }

        printCapabilities(&caps);

        if(caps.event_size != sizeof(struct presence_event))
        {
                fprintf(
                        stderr,
                        "Event size mismatch: driver=%u app=%zu\n",
                        caps.event_size,
                        sizeof(struct presence_event)
                );

                close(fd);
                return 1;
        }

        memset(&state, 0, sizeof(state));

        if(ioctl(
                fd,
                PRESENCE_IOC_GET_STATE,
                &state) < 0)
        {
                perror("ioctl(GET_STATE)");
                close(fd);
                return 1;
        }

        printState(&state);

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        printf("\nWaiting for PIR events...\n");
        printf("Press Ctrl+C to exit\n");

        while(1)
        {
                ret = poll(&pfd, 1, -1);

                if(ret < 0)
                {
                        if(errno == EINTR)
                                continue;

                        perror("poll");
                        break;
                }

                if(pfd.revents &
                   (POLLERR | POLLHUP | POLLNVAL))
                {
                        fprintf(
                                stderr,
                                "poll error: revents=0x%x\n",
                                pfd.revents
                        );

                        break;
                }

                if(pfd.revents & POLLIN)
                {
                        /*
                         * FIFO에 여러 이벤트가 있을 수 있으므로
                         * EAGAIN이 나올 때까지 모두 읽습니다.
                         */
                        while(1)
                        {
                                read_size = read(
                                        fd,
                                        &event,
                                        sizeof(event)
                                );

                                if(read_size < 0)
                                {
                                        if(errno == EAGAIN ||
                                           errno == EWOULDBLOCK)
                                        {
                                                break;
                                        }

                                        if(errno == EINTR)
                                                continue;

                                        perror("read");
                                        close(fd);
                                        return 1;
                                }

                                if(read_size != sizeof(event))
                                {
                                        fprintf(
                                                stderr,
                                                "Invalid read size: %zd\n",
                                                read_size
                                        );

                                        close(fd);
                                        return 1;
                                }

                                printEvent(&event);
                        }

                        printStats(fd);
                }

                pfd.revents = 0;
        }

        close(fd);

        return 0;
}
