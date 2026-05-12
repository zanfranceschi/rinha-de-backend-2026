#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUF_SIZE 32768

static const char *env_or(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value && *value ? value : fallback;
}

static int env_int(const char *key, int fallback) {
    const char *value = getenv(key);
    return value && *value ? atoi(value) : fallback;
}

static int listen_tcp(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 4096) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static ssize_t header_end(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return (ssize_t)i;
    }
    return -1;
}

static size_t content_length(const char *headers, size_t len) {
    for (size_t i = 0; i + 15 < len; ++i) {
        if (strncasecmp(headers + i, "content-length", 14) == 0) {
            i += 14;
            while (i < len && headers[i] != ':') ++i;
            if (i == len) return 0;
            ++i;
            while (i < len && (headers[i] == ' ' || headers[i] == '\t')) ++i;
            size_t value = 0;
            while (i < len && headers[i] >= '0' && headers[i] <= '9') {
                value = value * 10 + (size_t)(headers[i] - '0');
                ++i;
            }
            return value;
        }
    }
    return 0;
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return 0;
    }
    return 1;
}

static void handle_client(int client, const char *upstream) {
    char request[BUF_SIZE];
    size_t len = 0;
    size_t total = 0;
    while (len < sizeof(request)) {
        ssize_t n = recv(client, request + len, sizeof(request) - len, 0);
        if (n <= 0) return;
        len += (size_t)n;
        ssize_t h = header_end(request, len);
        if (h >= 0) {
            total = (size_t)h + 4 + content_length(request, (size_t)h);
            if (len >= total) break;
        }
    }
    if (!total || len < total) return;
    int api = connect_unix(upstream);
    if (api < 0) return;
    if (!write_all(api, request, total)) {
        close(api);
        return;
    }
    char response[4096];
    for (;;) {
        ssize_t n = recv(api, response, sizeof(response), 0);
        if (n > 0) {
            if (!write_all(client, response, (size_t)n)) break;
            if ((size_t)n < sizeof(response)) break;
            continue;
        }
        break;
    }
    close(api);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    const char *upstreams_env = env_or("UPSTREAMS", "/sockets/api1.sock,/sockets/api2.sock");
    char upstreams_buf[256];
    snprintf(upstreams_buf, sizeof(upstreams_buf), "%s", upstreams_env);
    const char *upstreams[8];
    int count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(upstreams_buf, ",", &save); tok && count < 8; tok = strtok_r(NULL, ",", &save)) {
        upstreams[count++] = tok;
    }
    if (count == 0) return 1;

    int fd = listen_tcp(env_int("PORT", 9999));
    if (fd < 0) {
        perror("listen");
        return 1;
    }
    unsigned rr = 0;
    for (;;) {
        int client = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        const char *upstream = upstreams[rr++ % (unsigned)count];
        handle_client(client, upstream);
        close(client);
    }
}
