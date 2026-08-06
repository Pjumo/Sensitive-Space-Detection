#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#define MAX_MSG_SIZE 4096
#define MAX_USERS 100
#define MAX_ID_LEN 16
#define MAX_PW_LEN 32
#define MAX_CLIENTS 64
#define MAX_EVENTS 32
#define HEARTBEAT_TIMEOUT_SEC 90

struct user_entry {
    char id[MAX_ID_LEN];
    char passwd[MAX_PW_LEN];
};

static struct user_entry users[MAX_USERS];
static int user_count = 0;

typedef enum {
    STATE_AUTH,
    STATE_READY
} client_state_t;

typedef struct {
    int fd;
    int in_use;
    client_state_t state;
    char id[MAX_ID_LEN];
    char ip_str[INET_ADDRSTRLEN];

    unsigned char buf[4 + MAX_MSG_SIZE];
    size_t buf_len;

    time_t last_seen;
} client_t;

static client_t clients[MAX_CLIENTS];

static int load_users(const char *filename);
static int check_auth(const char *id, const char *passwd);
static int set_nonblocking(int fd);
static client_t *find_free_slot(void);
static void remove_client(int epfd, client_t *c);
static int send_framed(int fd, const char *msg);
static void handle_message(int epfd, client_t *c, const char *data, uint32_t len);
static void try_process_buffer(int epfd, client_t *c);
static void handle_client_readable(int epfd, client_t *c);
static void check_heartbeat_timeouts(int epfd);

int main(int argc, char *argv[])
{
    int server_fd;
    struct sockaddr_in server_addr;
    int opt = 1;

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    if (load_users("idpasswd.txt") != 0) {
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 생성 실패");
        return 1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 실패");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen 실패");
        close(server_fd);
        return 1;
    }

    set_nonblocking(server_fd);

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 실패");
        close(server_fd);
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);

    memset(clients, 0, sizeof(clients));

    printf("[server] 포트 %s에서 대기 중... (epoll 멀티클라이언트)\n", argv[1]);

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 5000);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait 실패");
            break;
        }

        check_heartbeat_timeouts(epfd);

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);

                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept 실패");
                        break;
                    }

                    client_t *c = find_free_slot();
                    if (!c) {
                        fprintf(stderr, "[server] 클라이언트 슬롯 초과, 연결 거부\n");
                        close(client_fd);
                        continue;
                    }

                    set_nonblocking(client_fd);

                    c->fd = client_fd;
                    c->in_use = 1;
                    c->state = STATE_AUTH;
                    c->buf_len = 0;
                    c->id[0] = '\0';
                    c->last_seen = time(NULL);
                    inet_ntop(AF_INET, &client_addr.sin_addr, c->ip_str, sizeof(c->ip_str));

                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev);

                    printf("[server] 클라이언트 연결됨: %s (fd=%d)\n", c->ip_str, client_fd);
                }
            } else {
                client_t *c = NULL;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].in_use && clients[j].fd == events[i].data.fd) {
                        c = &clients[j];
                        break;
                    }
                }
                if (!c)
                    continue;

                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    remove_client(epfd, c);
                    continue;
                }

                handle_client_readable(epfd, c);
            }
        }
    }

    close(server_fd);
    close(epfd);
    return 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static client_t *find_free_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use)
            return &clients[i];
    }
    return NULL;
}

static void remove_client(int epfd, client_t *c)
{
    printf("[server] 클라이언트 연결 종료: %s (fd=%d)\n", c->ip_str, c->fd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    c->in_use = 0;
    c->fd = -1;
    c->buf_len = 0;
}

static void check_heartbeat_timeouts(int epfd)
{
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].in_use)
            continue;

        if (now - clients[i].last_seen >= HEARTBEAT_TIMEOUT_SEC) {
            printf("[server] fd=%d (id=%s) heartbeat 타임아웃, 연결 강제 종료\n",
                   clients[i].fd, clients[i].id);
            remove_client(epfd, &clients[i]);
        }
    }
}

static int send_framed(int fd, const char *msg)
{
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t len_be = htonl(len);

    if (write(fd, &len_be, sizeof(len_be)) != sizeof(len_be))
        return -1;
    if (write(fd, msg, len) != (ssize_t)len)
        return -1;
    return 0;
}

static void handle_client_readable(int epfd, client_t *c)
{
    while (1) {
        ssize_t space = (ssize_t)sizeof(c->buf) - (ssize_t)c->buf_len;
        if (space <= 0) {
            fprintf(stderr, "[server] fd=%d 버퍼 초과, 연결 종료\n", c->fd);
            remove_client(epfd, c);
            return;
        }

        ssize_t r = read(c->fd, c->buf + c->buf_len, (size_t)space);

        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            remove_client(epfd, c);
            return;
        }

        if (r == 0) {
            remove_client(epfd, c);
            return;
        }

        c->buf_len += (size_t)r;
        c->last_seen = time(NULL);

        try_process_buffer(epfd, c);
        if (!c->in_use)
            return;
    }
}

static void try_process_buffer(int epfd, client_t *c)
{
    while (1) {
        if (c->buf_len < 4)
            return;

        uint32_t len_be;
        memcpy(&len_be, c->buf, 4);
        uint32_t len = ntohl(len_be);

        if (len == 0 || len > MAX_MSG_SIZE) {
            fprintf(stderr, "[server] fd=%d 비정상 길이값(%u), 연결 종료\n", c->fd, len);
            remove_client(epfd, c);
            return;
        }

        if (c->buf_len < 4 + len)
            return;

        char msg[MAX_MSG_SIZE + 1];
        memcpy(msg, c->buf + 4, len);
        msg[len] = '\0';

        size_t consumed = 4 + len;
        memmove(c->buf, c->buf + consumed, c->buf_len - consumed);
        c->buf_len -= consumed;

        handle_message(epfd, c, msg, len);
        if (!c->in_use)
            return;
    }
}

static void handle_message(int epfd, client_t *c, const char *data, uint32_t len)
{
    (void)len;

    if (c->state == STATE_AUTH) {
        char id[MAX_ID_LEN], passwd[MAX_PW_LEN];

        if (sscanf(data, "%15s %31s", id, passwd) != 2) {
            printf("[server] fd=%d 인증 메시지 형식 오류, 연결 종료\n", c->fd);
            remove_client(epfd, c);
            return;
        }

        if (!check_auth(id, passwd)) {
            printf("[server] 인증 실패 (id=%s), 연결 종료\n", id);
            send_framed(c->fd, "AUTH_FAIL");
            remove_client(epfd, c);
            return;
        }

        strncpy(c->id, id, sizeof(c->id) - 1);
        c->id[sizeof(c->id) - 1] = '\0';
        c->state = STATE_READY;

        printf("[server] 인증 성공 (id=%s, %s)\n", c->id, c->ip_str);
        send_framed(c->fd, "AUTH_OK");
        return;
    }

    printf("[server] 수신 (id=%s): %s\n", c->id, data);
}

static int load_users(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("idpasswd.txt 열기 실패");
        return -1;
    }

    user_count = 0;
    while (user_count < MAX_USERS &&
           fscanf(fp, "%15s %31s",
                  users[user_count].id,
                  users[user_count].passwd) == 2) {
        user_count++;
    }

    fclose(fp);
    printf("[server] %d명의 사용자 정보 로드 완료\n", user_count);
    return 0;
}

static int check_auth(const char *id, const char *passwd)
{
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].id, id) == 0 &&
            strcmp(users[i].passwd, passwd) == 0) {
            return 1;
        }
    }
    return 0;
}
