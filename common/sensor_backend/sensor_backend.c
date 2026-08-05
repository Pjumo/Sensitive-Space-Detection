#define _POSIX_C_SOURCE 200809L

#include "sensor_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct sensor_backend {
    int fd;
    bool nonblocking;
    char *device_path;
    struct presence_caps caps;
};

static int sensor_backend_ioctl(int fd,
                                unsigned long command,
                                void *argument)
{
    int ret;

    do {
        ret = ioctl(fd, command, argument);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0)
        return -errno;

    return 0;
}

static bool sensor_type_is_valid(__u32 sensor_type)
{
    return sensor_type == PRESENCE_SENSOR_PIR ||
           sensor_type == PRESENCE_SENSOR_RADAR;
}

static bool event_type_is_valid(__u32 event_type)
{
    return event_type == PRESENCE_EVENT_NONE ||
           event_type == PRESENCE_EVENT_ASSERTED ||
           event_type == PRESENCE_EVENT_DEASSERTED ||
           event_type == PRESENCE_EVENT_ERROR;
}

static int validate_caps(const struct presence_caps *caps)
{
    const __u32 required_caps =
        PRESENCE_CAP_READ | PRESENCE_CAP_POLL;

    if (caps->api_version != PRESENCE_API_VERSION)
        return -EPROTO;

    if (!sensor_type_is_valid(caps->sensor_type))
        return -EPROTO;

    if (caps->event_size != sizeof(struct presence_event))
        return -EPROTO;

    if ((caps->capability_flags & required_caps) != required_caps)
        return -EOPNOTSUPP;

    return 0;
}

int sensor_backend_open(sensor_backend_t **out_backend,
                        const char *device_path,
                        bool nonblocking)
{
    sensor_backend_t *backend;
    __u32 api_version = 0;
    int open_flags;
    int ret;

    if (!out_backend || !device_path || device_path[0] == '\0')
        return -EINVAL;

    *out_backend = NULL;

    backend = calloc(1, sizeof(*backend));
    if (!backend)
        return -ENOMEM;

    backend->fd = -1;
    backend->nonblocking = nonblocking;

    backend->device_path = strdup(device_path);
    if (!backend->device_path) {
        ret = -ENOMEM;
        goto error;
    }

    open_flags = O_RDONLY | O_CLOEXEC;
    if (nonblocking)
        open_flags |= O_NONBLOCK;

    backend->fd = open(device_path, open_flags);
    if (backend->fd < 0) {
        ret = -errno;
        goto error;
    }

    ret = sensor_backend_ioctl(
        backend->fd,
        PRESENCE_IOC_GET_API_VERSION,
        &api_version);
    if (ret < 0)
        goto error;

    if (api_version != PRESENCE_API_VERSION) {
        ret = -EPROTO;
        goto error;
    }

    ret = sensor_backend_ioctl(
        backend->fd,
        PRESENCE_IOC_GET_CAPS,
        &backend->caps);
    if (ret < 0)
        goto error;

    ret = validate_caps(&backend->caps);
    if (ret < 0)
        goto error;

    *out_backend = backend;
    return 0;

error:
    sensor_backend_close(backend);
    return ret;
}

void sensor_backend_close(sensor_backend_t *backend)
{
    if (!backend)
        return;

    if (backend->fd >= 0)
        close(backend->fd);

    free(backend->device_path);
    free(backend);
}

int sensor_backend_get_fd(const sensor_backend_t *backend)
{
    if (!backend)
        return -EINVAL;

    return backend->fd;
}

const char *sensor_backend_get_device_path(
    const sensor_backend_t *backend)
{
    if (!backend)
        return NULL;

    return backend->device_path;
}

int sensor_backend_get_caps(const sensor_backend_t *backend,
                            struct presence_caps *caps)
{
    if (!backend || !caps)
        return -EINVAL;

    *caps = backend->caps;
    return 0;
}

int sensor_backend_wait(sensor_backend_t *backend,
                        int timeout_ms)
{
    struct pollfd poll_fd;
    int ret;

    if (!backend)
        return -EINVAL;

    if (timeout_ms < -1)
        return -EINVAL;

    poll_fd.fd = backend->fd;
    poll_fd.events = POLLIN;
    poll_fd.revents = 0;

    do {
        ret = poll(&poll_fd, 1, timeout_ms);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0)
        return -errno;

    if (ret == 0)
        return 0;

    /* 데이터와 HUP가 동시에 있으면 남은 이벤트를 먼저 읽게 한다. */
    if (poll_fd.revents & (POLLIN | POLLRDNORM))
        return 1;

    if (poll_fd.revents & POLLNVAL)
        return -EBADF;

    if (poll_fd.revents & POLLHUP)
        return -ENODEV;

    if (poll_fd.revents & POLLERR)
        return -EIO;

    return -EIO;
}

int sensor_backend_read_event(sensor_backend_t *backend,
                              struct presence_event *event)
{
    ssize_t read_size;

    if (!backend || !event)
        return -EINVAL;

    do {
        read_size = read(
            backend->fd,
            event,
            sizeof(*event));
    } while (read_size < 0 && errno == EINTR);

    if (read_size < 0)
        return -errno;

    if (read_size == 0)
        return -ENODEV;

    if ((size_t)read_size != sizeof(*event))
        return -EPROTO;

    if (event->api_version != PRESENCE_API_VERSION)
        return -EPROTO;

    if (event->sensor_type != backend->caps.sensor_type)
        return -EPROTO;

    if (!event_type_is_valid(event->event_type))
        return -EPROTO;

    return 0;
}

int sensor_backend_get_state(sensor_backend_t *backend,
                             struct presence_state *state)
{
    int ret;

    if (!backend || !state)
        return -EINVAL;

    if (!(backend->caps.capability_flags &
          PRESENCE_CAP_CURRENT_STATE))
        return -EOPNOTSUPP;

    memset(state, 0, sizeof(*state));

    ret = sensor_backend_ioctl(
        backend->fd,
        PRESENCE_IOC_GET_STATE,
        state);
    if (ret < 0)
        return ret;

    if (state->api_version != PRESENCE_API_VERSION ||
        state->sensor_type != backend->caps.sensor_type)
        return -EPROTO;

    return 0;
}

int sensor_backend_get_stats(sensor_backend_t *backend,
                             struct presence_stats *stats)
{
    int ret;

    if (!backend || !stats)
        return -EINVAL;

    if (!(backend->caps.capability_flags & PRESENCE_CAP_STATS))
        return -EOPNOTSUPP;

    memset(stats, 0, sizeof(*stats));

    ret = sensor_backend_ioctl(
        backend->fd,
        PRESENCE_IOC_GET_STATS,
        stats);
    if (ret < 0)
        return ret;

    if (stats->api_version != PRESENCE_API_VERSION)
        return -EPROTO;

    return 0;
}

int sensor_backend_clear_stats(sensor_backend_t *backend)
{
    if (!backend)
        return -EINVAL;

    if (!(backend->caps.capability_flags & PRESENCE_CAP_STATS))
        return -EOPNOTSUPP;

    return sensor_backend_ioctl(
        backend->fd,
        PRESENCE_IOC_CLEAR_STATS,
        NULL);
}

