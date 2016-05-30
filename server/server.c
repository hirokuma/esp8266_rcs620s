#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int main(int argc, char *argv[])
{
    int ret;
    int sock;
    int acc;
    struct sockaddr_in addr_svr;
    struct sockaddr_in addr_cli;
    socklen_t len_addr_cli;
    char buf[4096];
    ssize_t n;
    int optval = 1;

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (ret < 0) {
        perror("setsockopt");
        return -2;
    }

    memset(&addr_svr, 0, sizeof(addr_svr));
    addr_svr.sin_family = AF_INET;
    addr_svr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_svr.sin_port = htons(12345);
    ret = bind(sock, (struct sockaddr *)&addr_svr, sizeof(addr_svr));
    if (ret < 0) {
        perror("bind");
        return -3;
    }

    ret = listen(sock, SOMAXCONN);
    if (ret < 0) {
        perror("listen");
        return -4;
    }

    printf("listen...\n");

    len_addr_cli = sizeof(addr_cli);
    acc = accept(sock, (struct sockaddr *)&addr_cli, &len_addr_cli);
    if (acc < 0) {
        perror("accept");
        return -5;
    }

    printf("addr=%s, port=%d\n", inet_ntoa(addr_cli.sin_addr), ntohs(addr_cli.sin_port));

    while ((n = read(acc, buf, sizeof(buf))) > 0) {
        //recv
        //write(1, buf, n);
        int i;
        for (i = 0; i < n; i++) {
            printf("%02X", (uint8_t)buf[i]);
        }
        printf("\n");
    }
    if (n < 0) {
        perror("read");
        return -6;
    }

    ret = shutdown(acc, SHUT_RDWR);
    if (ret < 0) {
        perror("shutdown");
        return -7;
    }

    return 0;
}
