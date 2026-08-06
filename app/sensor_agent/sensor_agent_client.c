#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "real/real_backend.h"

#define OCCUPANCY_TIMEOUT_SEC 5
#define MAX_MSG_SIZE 4096
#define CLIENT_PASSWD "PASSWD"

static int read_exact(int fd, void *buf, size_t n);
static int send_msg(int fd, const char *msg);

int main(int argc, char *argv[])
{
    if (argc != 4) {
        printf("Usage: %s <IP> <port> <id>\n", argv[0]);
        exit(1);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    const char *client_id = argv[3];

    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket 생성 실패");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "잘못된 IP 주소: %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect 실패");
        close(sock);
        return 1;
    }

    printf("[client] 서버(%s:%d)에 연결됨\n", server_ip, server_port);

    char auth_msg[MAX_MSG_SIZE];
    snprintf(auth_msg, sizeof(auth_msg), "%s %s", client_id, CLIENT_PASSWD);

    if (send_msg(sock, auth_msg) != 0) {
        fprintf(stderr, "[client] 인증 메시지 전송 실패\n");
        close(sock);
        return 1;
    }

    uint32_t resp_len_be;
    if (read_exact(sock, &resp_len_be, sizeof(resp_len_be)) != 0) {
        fprintf(stderr, "[client] 서버 응답 수신 실패\n");
        close(sock);
        return 1;
    }

    uint32_t resp_len = ntohl(resp_len_be);
    if (resp_len == 0 || resp_len > MAX_MSG_SIZE) {
        fprintf(stderr, "[client] 비정상 응답 길이\n");
        close(sock);
        return 1;
    }

    char resp[MAX_MSG_SIZE + 1];
    if (read_exact(sock, resp, resp_len) != 0) {
        fprintf(stderr, "[client] 서버 응답 수신 중 오류\n");
        close(sock);
        return 1;
    }
    resp[resp_len] = '\0';

    if (strcmp(resp, "AUTH_OK") != 0) {
        printf("[client] 인증 실패: %s\n", resp);
        close(sock);
        return 1;
    }

    printf("[client] 인증 성공\n");

    if (real_backend_init() != 0) {
        fprintf(stderr, "backend 초기화 실패\n");
        close(sock);
        return 1;
    }

    printf("[client] 재실 감지 시작 (occupancy timeout = %d초)\n", OCCUPANCY_TIMEOUT_SEC);

    struct presence_event ev;
    int occupied = 0;
    int waiting_for_timeout = 0;
    time_t low_since = 0;

    while (1) {
        int ret = real_backend_wait_read(&ev, 1000);   /* 1초마다 깨어나서 타이머 체크 */

        if (ret < 0) {
            fprintf(stderr, "read 실패\n");
            break;
        }

        if (ret == 1) {
            if (ev.event_type == PRESENCE_EVENT_ASSERTED) {
                waiting_for_timeout = 0;   /* HIGH 다시 옴, 타이머 취소 */

                if (!occupied) {
                    occupied = 1;
                    printf("[client] >>> 재실 있음으로 전환\n");

                    char status_msg[MAX_MSG_SIZE];
                    snprintf(status_msg, sizeof(status_msg),
                             "{\"id\":\"%s\",\"occupied\":true}", client_id);
                    send_msg(sock, status_msg);
                }
            }
            else if (ev.event_type == PRESENCE_EVENT_DEASSERTED) {
                if (occupied) {
                    waiting_for_timeout = 1;
                    low_since = time(NULL);   /* LOW로 떨어진 시각부터 타이머 시작 */
                }
            }
        }

        if (waiting_for_timeout && occupied) {
            time_t now = time(NULL);
            if (now - low_since >= OCCUPANCY_TIMEOUT_SEC) {
                occupied = 0;
                waiting_for_timeout = 0;
                printf("[client] >>> 재실 아님으로 전환 (LOW 후 %ld초 경과)\n",
                       (long)(now - low_since));

                char status_msg[MAX_MSG_SIZE];
                snprintf(status_msg, sizeof(status_msg),
                         "{\"id\":\"%s\",\"occupied\":false}", client_id);
                send_msg(sock, status_msg);
            }
        }
    }

    real_backend_close();
    close(sock);
    return 0;
}

static int send_msg(int fd, const char *msg)
{
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t len_be = htonl(len);

    if (write(fd, &len_be, sizeof(len_be)) != sizeof(len_be)) {
        return -1;
    }
    if (write(fd, msg, len) != (ssize_t)len) {
        return -1;
    }
    return 0;
}

static int read_exact(int fd, void *buf, size_t n)
{
    size_t got = 0;
    char *p = (char *)buf;

    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r <= 0) {
            return -1;
        }
        got += r;
    }

    return 0;
}
