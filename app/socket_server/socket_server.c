#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_MSG_SIZE 4096
#define MAX_USERS 100
#define MAX_ID_LEN 16
#define MAX_PW_LEN 32

struct user_entry {
    char id[MAX_ID_LEN];
    char passwd[MAX_PW_LEN];
};

static struct user_entry users[MAX_USERS];
static int user_count = 0;

static int read_exact(int fd, void *buf, size_t n);
static int load_users(const char *filename);
static int check_auth(const char *id, const char *passwd);

int main(int argc, char *argv[])
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 생성 실패");
        return 1;
    }

    int opt = 1;
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

    if (listen(server_fd, 5) < 0) {
        perror("listen 실패");
        close(server_fd);
        return 1;
    }

    if (load_users("idpasswd.txt") != 0){ 
        close(server_fd);
        return 1;
    }

    printf("[server] 포트 %s에서 대기 중...\n", argv[1]);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept 실패");
            continue;
        }

        printf("[server] 클라이언트 연결됨: %s\n", inet_ntoa(client_addr.sin_addr));

        uint32_t auth_len_be;
        if (read_exact(client_fd, &auth_len_be, sizeof(auth_len_be)) != 0) {
            printf("[server] 인증 메시지 수신 실패, 연결 종료\n");
            close(client_fd);
            continue;
        }

        uint32_t auth_len = ntohl(auth_len_be);
        if (auth_len == 0 || auth_len > MAX_MSG_SIZE) {
            fprintf(stderr, "[server] 비정상 인증 메시지 길이, 연결 종료\n");
            close(client_fd);
            continue;
        }

        char auth_msg[MAX_MSG_SIZE + 1];
        if (read_exact(client_fd, auth_msg, auth_len) != 0) {
            printf("[server] 인증 메시지 수신 중 연결 종료\n");
            close(client_fd);
            continue;
        }
        auth_msg[auth_len] = '\0';

        char id[MAX_ID_LEN], passwd[MAX_PW_LEN];
        if (sscanf(auth_msg, "%15s %31s", id, passwd) != 2) {
            printf("[server] 인증 메시지 형식 오류, 연결 종료\n");
            close(client_fd);
            continue;
        }

        if (!check_auth(id, passwd)) {
            printf("[server] 인증 실패 (id=%s), 연결 종료\n", id);
            const char *fail_msg = "AUTH_FAIL";
            uint32_t fail_len = htonl(strlen(fail_msg));
            write(client_fd, &fail_len, sizeof(fail_len));
            write(client_fd, fail_msg, strlen(fail_msg));
            close(client_fd);
            continue;
        }

        printf("[server] 인증 성공 (id=%s)\n", id);
        const char *ok_msg = "AUTH_OK";
        uint32_t ok_len = htonl(strlen(ok_msg));
        write(client_fd, &ok_len, sizeof(ok_len));
        write(client_fd, ok_msg, strlen(ok_msg));

        while (1) {
            uint32_t len_be;
            if (read_exact(client_fd, &len_be, sizeof(len_be)) != 0) {
                printf("[server] 클라이언트 연결 종료\n");
                break;
            }

            uint32_t len = ntohl(len_be);
            if (len == 0 || len > MAX_MSG_SIZE) {
                fprintf(stderr, "[server] 비정상 길이값(%u), 연결 종료\n", len);
                break;
            }

            char msg[MAX_MSG_SIZE + 1];
            if (read_exact(client_fd, msg, len) != 0) {
                printf("[server] 메시지 수신 중 연결 종료\n");
                break;
            }
            msg[len] = '\0';

            printf("[server] 수신: %s\n", msg);
        }

        close(client_fd);
    }

    close(server_fd);
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
