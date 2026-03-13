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
}

static int create_listen_socket(int port) {
    // TODO: create a TCP socket, set SO_REUSEADDR, bind to port, listen
    // Make it non-blocking before returning
    // Return the fd, or -1 on error
}

static void handle_new_connection(int epfd, int listenfd) {
    // TODO: accept all pending connections in a loop (because edge-triggered)
    // For each: make non-blocking, register with epoll for EPOLLIN|EPOLLET|EPOLLRDHUP
}

static void handle_client_data(int epfd, int clientfd) {
    // TODO: read all available data in a loop (because edge-triggered)
    // Echo it back. On EAGAIN/EWOULDBLOCK: stop. On EOF or error: close + deregister
}

int main(void) {
    int listenfd = create_listen_socket(PORT);
    if (listenfd < 0) { perror("listen"); return 1; }

    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    // TODO: register listenfd with epoll (EPOLLIN | EPOLLET)

    struct epoll_event events[MAX_EVENTS];
    printf("Reactor listening on port %d\n", PORT);

    for (;;) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            // TODO: dispatch to handle_new_connection or handle_client_data
            //       also handle EPOLLHUP and EPOLLERR
        }
    }

    return 0;
}