#ifndef SENSOR_BACKEND_H
#define SENSOR_BACKEND_H

#include <stdbool.h>

#include "presence_uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sensor_backend sensor_backend_t;

/*
 * device_path 예시:
 *   /dev/pir_presence
 *   /dev/radar_presence
 *
 * nonblocking이 true이면 O_NONBLOCK으로 장치를 연다.
 * 성공 시 0, 실패 시 음수 errno를 반환한다.
 */
int sensor_backend_open(sensor_backend_t **out_backend,
                        const char *device_path,
                        bool nonblocking);

void sensor_backend_close(sensor_backend_t *backend);

/* poll/epoll 통합이 필요한 상위 애플리케이션용 */
int sensor_backend_get_fd(const sensor_backend_t *backend);

const char *sensor_backend_get_device_path(
    const sensor_backend_t *backend);

/* open 시 조회·검증한 capability의 복사본을 반환한다. */
int sensor_backend_get_caps(const sensor_backend_t *backend,
                            struct presence_caps *caps);

/*
 * timeout_ms:
 *   -1 : 무한 대기
 *    0 : 즉시 확인
 *   >0 : 지정한 밀리초 동안 대기
 *
 * 반환값:
 *    1 : 읽을 이벤트가 있음
 *    0 : timeout
 *   <0 : 음수 errno
 */
int sensor_backend_wait(sensor_backend_t *backend,
                        int timeout_ms);

/* read 1회로 presence_event 1개를 수신한다. */
int sensor_backend_read_event(sensor_backend_t *backend,
                              struct presence_event *event);

int sensor_backend_get_state(sensor_backend_t *backend,
                             struct presence_state *state);

int sensor_backend_get_stats(sensor_backend_t *backend,
                             struct presence_stats *stats);

int sensor_backend_clear_stats(sensor_backend_t *backend);

#ifdef __cplusplus
}
#endif
#endif
