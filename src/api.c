#define _GNU_SOURCE
#include "rinha.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_EVENTS 1024
#define MAX_CONNECTIONS 8192
#define IN_BUF 16384
#define OUT_BUF 256

typedef struct {
    int fd;
    size_t in_len;
    size_t out_len;
    size_t out_pos;
    char in[IN_BUF];
    char out[OUT_BUF];
} conn_t;

static const char READY[] = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
static const char NOT_FOUND[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static const char BAD[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static const char *RESP[6] = {
    "HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.0}",
    "HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.2}",
    "HTTP/1.1 200 OK\r\nContent-Length: 35\r\n\r\n{\"approved\":true,\"fraud_score\":0.4}",
    "HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":0.6}",
    "HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":0.8}",
    "HTTP/1.1 200 OK\r\nContent-Length: 36\r\n\r\n{\"approved\":false,\"fraud_score\":1.0}",
};

static conn_t *connections[MAX_CONNECTIONS];

static const char *env_or(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value && *value ? value : fallback;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int listen_unix(const char *path) {
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    chmod(path, 0777);
    if (listen(fd, 4096) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static ssize_t header_end(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static size_t content_length(const char *headers, size_t len) {
    for (size_t i = 0; i + 15 < len; ++i) {
        char c = headers[i];
        if ((c == 'c' || c == 'C') && i + 15 <= len &&
            strncasecmp(headers + i, "content-length", 14) == 0) {
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

static int starts_with(const char *s, size_t len, const char *prefix) {
    size_t plen = strlen(prefix);
    return len >= plen && memcmp(s, prefix, plen) == 0;
}

static void set_response(conn_t *conn, const char *data) {
    conn->out_len = strlen(data);
    if (conn->out_len > OUT_BUF) conn->out_len = OUT_BUF;
    memcpy(conn->out, data, conn->out_len);
    conn->out_pos = 0;
}

static uint8_t classify(const char *body, size_t body_len, const rinha_index *index, const rinha_search_config *config) {
    rinha_payload payload;
    rinha_vector query;
    if (!rinha_parse_payload(body, body_len, &payload)) return 0;
    rinha_vectorize(&payload, &query);
    return rinha_index_fraud_count(index, &query, config);
}

static int process_conn(conn_t *conn, const rinha_index *index, const rinha_search_config *config) {
    while (1) {
        ssize_t h = header_end(conn->in, conn->in_len);
        if (h < 0) return conn->in_len < IN_BUF;
        size_t header_len = (size_t)h + 4;
        char *line_end = memchr(conn->in, '\n', (size_t)h);
        if (!line_end) {
            set_response(conn, BAD);
            return 1;
        }
        size_t first_len = (size_t)(line_end - conn->in);
        size_t clen = content_length(conn->in, (size_t)h);
        size_t total = header_len + clen;
        if (conn->in_len < total) return total <= IN_BUF;
        const char *body = conn->in + header_len;
        if (starts_with(conn->in, first_len, "GET /ready ")) {
            set_response(conn, READY);
        } else if (starts_with(conn->in, first_len, "POST /fraud-score ")) {
            uint8_t frauds = classify(body, clen, index, config);
            if (frauds > 5) frauds = 5;
            set_response(conn, RESP[frauds]);
        } else {
            set_response(conn, NOT_FOUND);
        }
        size_t rest = conn->in_len - total;
        memmove(conn->in, conn->in + total, rest);
        conn->in_len = rest;
        return 1;
    }
}

static void close_conn(int ep, conn_t *conn) {
    if (!conn) return;
    epoll_ctl(ep, EPOLL_CTL_DEL, conn->fd, NULL);
    if (conn->fd >= 0 && conn->fd < MAX_CONNECTIONS) connections[conn->fd] = NULL;
    close(conn->fd);
    free(conn);
}

static int add_conn(int ep, int fd) {
    if (fd < 0 || fd >= MAX_CONNECTIONS) {
        close(fd);
        return 0;
    }
    conn_t *conn = (conn_t *)calloc(1, sizeof(conn_t));
    if (!conn) {
        close(fd);
        return 0;
    }
    conn->fd = fd;
    connections[fd] = conn;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.ptr = conn;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) != 0) {
        close_conn(ep, conn);
        return 0;
    }
    return 1;
}

static int flush_conn(conn_t *conn) {
    while (conn->out_pos < conn->out_len) {
        ssize_t n = send(conn->fd, conn->out + conn->out_pos, conn->out_len - conn->out_pos, MSG_NOSIGNAL);
        if (n > 0) {
            conn->out_pos += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        return 0;
    }
    conn->out_len = 0;
    conn->out_pos = 0;
    return 1;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    rinha_index index;
    rinha_index_init(&index);
    if (!rinha_index_load(&index, env_or("IVF_INDEX_PATH", "/app/data/index.bin"))) {
        fprintf(stderr, "failed to load index\n");
        return 1;
    }

    rinha_search_config config = {
        .fast_nprobe = rinha_env_u32("IVF_FAST_NPROBE", 1),
        .full_nprobe = rinha_env_u32("IVF_FULL_NPROBE", 1),
        .boundary_full = rinha_env_bool("IVF_BOUNDARY_FULL", 0),
        .bbox_repair = rinha_env_bool("IVF_BBOX_REPAIR", 1),
        .repair_min_frauds = (uint8_t)rinha_env_u32("IVF_REPAIR_MIN_FRAUDS", 1),
        .repair_max_frauds = (uint8_t)rinha_env_u32("IVF_REPAIR_MAX_FRAUDS", 4),
    };

    const char *socket_path = env_or("UNIX_SOCKET_PATH", "/tmp/rinha-api.sock");
    int listen_fd = listen_unix(socket_path);
    if (listen_fd < 0) {
        perror("listen_unix");
        return 1;
    }

    int ep = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(ep, EPOLL_CTL_ADD, listen_fd, &ev);

    struct epoll_event events[MAX_EVENTS];
    for (;;) {
        int n = epoll_wait(ep, events, MAX_EVENTS, -1);
        if (n < 0 && errno == EINTR) continue;
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == listen_fd) {
                for (;;) {
                    int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        continue;
                    }
                    set_nonblock(fd);
                    add_conn(ep, fd);
                }
                continue;
            }
            conn_t *conn = (conn_t *)events[i].data.ptr;
            int alive = 1;
            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) alive = 0;
            if (alive && (events[i].events & EPOLLIN)) {
                for (;;) {
                    if (conn->in_len == IN_BUF) {
                        alive = 0;
                        break;
                    }
                    ssize_t r = recv(conn->fd, conn->in + conn->in_len, IN_BUF - conn->in_len, 0);
                    if (r > 0) {
                        conn->in_len += (size_t)r;
                        if (!process_conn(conn, &index, &config)) alive = 0;
                        continue;
                    }
                    if (r == 0) alive = 0;
                    if (r < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) alive = 0;
                    break;
                }
            }
            if (alive && conn->out_len) alive = flush_conn(conn);
            if (!alive) {
                close_conn(ep, conn);
            } else {
                struct epoll_event cev;
                memset(&cev, 0, sizeof(cev));
                cev.events = EPOLLIN | EPOLLRDHUP | (conn->out_len ? EPOLLOUT : 0);
                cev.data.ptr = conn;
                epoll_ctl(ep, EPOLL_CTL_MOD, conn->fd, &cev);
            }
        }
    }
}
