// Compile: gcc -Wall -Wextra -O2 -o reactor reactor.c

#define _GNU_SOURCE // for accept4
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#define PORT        8080
#define MAX_EVENTS  64
#define BUF_SIZE    4096

static int make_nonblocking(int fd) {
    // TODO: use fcntl to set O_NONBLOCK on fd
    // Return -1 on failure, 0 on success

    int flag = fcntl(fd, F_GETFL, 0);
    if(-1 == flag) { perror("read flags"); return -1; }
    if(-1 == fcntl(fd, F_SETFL, flag | O_NONBLOCK)) { perror("set flags"); return -1; }

    return 0;
}

static int create_listen_socket(int port) {
    // TODO: create a TCP socket, set SO_REUSEADDR, bind to port, listen
    // Make it non-blocking before returning
    // Return the fd, or -1 on error

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == fd) { perror("create socket"); return -1; }

    if(-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))
        { perror("set socket options"); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (-1 == bind(fd, (struct sockaddr *)&addr, sizeof(addr)))
        { perror("bind"); return -1; }

    if(-1 == listen(fd, SOMAXCONN)) 
        { perror("listen"); return -1; }

    return (make_nonblocking(fd) == -1) ? -1 : fd;  
}

static void handle_new_connection(int epfd, int listenfd) {
    // TODO: accept all pending connections in a loop (because edge-triggered)
    // For each: make non-blocking, register with epoll for EPOLLIN|EPOLLET|EPOLLRDHUP
    for (;;) {
        int clientfd = accept4(listenfd, NULL, NULL, SOCK_NONBLOCK);
        if (clientfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; /* queue drained - no error */
            perror("accept4");
            break;
        }
        struct epoll_event ev = {0};
        ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = clientfd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev) < 0) {
            perror("epoll_ctl add client");
            close(clientfd);
        }
    }
}

static void handle_client_data(int epfd, int clientfd) {
    // TODO: read all available data in a loop (because edge-triggered)
    // Echo it back. On EAGAIN/EWOULDBLOCK: stop. On EOF or error: close + deregister
    for (;;) {
        char buf[BUF_SIZE];
        ssize_t n = read(clientfd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; /* buffer drained */
            perror("read");
            epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, NULL);
            close(clientfd);
            break;
        }
        if (n == 0) { /* EOF — peer closed write end */
            epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, NULL);
            close(clientfd);
            break;
        }
        printf("Recv: %s\n", buf);
        if (write(clientfd, buf, n) < 0) {
            perror("write");
            epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, NULL);
            close(clientfd);
            break;
        }
        //printf("Sent: %s\n", buf);
    }
}

int main(void) {
    int listenfd = create_listen_socket(PORT);
    if (listenfd < 0) { perror("listen"); return 1; }

    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    // TODO: register listenfd with epoll (EPOLLIN | EPOLLET)
    struct epoll_event ev = {0};
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = listenfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) { perror("epoll_ctl"); }

    struct epoll_event events[MAX_EVENTS];
    printf("Reactor listening on port %d\n", PORT);

    for (;;) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            // TODO: dispatch to handle_new_connection or handle_client_data
            //       also handle EPOLLHUP and EPOLLERR
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                close(events[i].data.fd);
                continue;
            }

            if (events[i].data.fd == listenfd) {
                handle_new_connection(epfd, events[i].data.fd);
            } else {
                handle_client_data(epfd, events[i].data.fd);
            }
        }
    }

    return 0;
}