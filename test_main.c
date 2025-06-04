#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>

#define END_OF_MESSAGE "__END__"

#define BUF_SIZE 1024

void sync_dir(int source_sock, int target_sock, char *source_dir);
void pull(int sockfd, char *file_name);
int init_socket(char *server_ip, int server_port);
void socket_send(int socket, char *buffer);
void push(int sockfd);

void list(int source_sock, char *source_dir);

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

int main(void)
{
    int source_port = 8088;
    char *source_ip = "127.0.0.1";

    int target_port = 8089;
    char *target_ip = "127.0.0.1";

    int source_sock = init_socket(source_ip, source_port);
    // int target_sock = init_socket(target_ip, target_port);

    sleep(1);
    char *source_dir = "dir";
    // sync_dir(source_sock, target_sock, source_dir);

    list(source_sock, source_dir);
    // pull(source_sock, "dir/file1.txt");
    // push(source_sock);

    while (1)
        ;

    close(source_sock);
    // close(target_sock);
    return 0;
}

void list(int source_sock, char *source_dir)
{
    char buffer[1024];
    strcpy(buffer, "list ");
    strcat(buffer, source_dir);
    printf("SENT: %s\n", buffer);
    socket_send(source_sock, buffer);

    int bytes_read;
    if ((bytes_read = read(source_sock, buffer, 1024)) == -1)
        perror_exit("read sockfd");

    buffer[bytes_read] = '\0';
    char *file_name = strtok(buffer, " \n");
    while (file_name != NULL)
    {
        printf("Operation on: %s\n", file_name);
        file_name = strtok(NULL, " \n");
    }
}

void sync_dir(int source_sock, int target_sock, char *source_dir)
{
    char buffer[1024];
    strcpy(buffer, "list ");
    strcat(buffer, source_dir);
    socket_send(source_sock, buffer);

    int bytes_read;
    if ((bytes_read = read(source_sock, buffer, 1024)) == -1)
        perror_exit("read sockfd");

    buffer[bytes_read] = '\0';
    char *file_name = strtok(buffer, " \n");
    while (file_name != NULL)
    {
        printf("Operation on: %s\n", file_name);
        pull(source_sock, file_name);
        file_name = strtok(NULL, " \n");
    }
}

void pull(int sockfd, char *file_name)
{
    char buf[1024];
    strcpy(buf, "pull ");
    strcat(buf, file_name);
    socket_send(sockfd, buf);
    while (1)
    {
        ssize_t n = read(sockfd, buf, sizeof(buf) - 1);
        if (n < 0)
            perror_exit("read manager socket");
        if (n == 0)
            break;
        buf[n] = '\0';

        if (strstr(buf, END_OF_MESSAGE) != NULL)
        {
            *strstr(buf, END_OF_MESSAGE) = '\0'; // remove END_OF_MESSAGE marker
            printf("%s", buf);
            fflush(stdout);
            break;
        }
        printf("%s", buf);
        fflush(stdout);
    }
}

void push(int sockfd)
{
    char buf[1024];
    strcpy(buf, "push /dir/file1.txt 20 aaaaa");
    socket_send(sockfd, buf);
    strcpy(buf, "bbbbb");
    socket_send(sockfd, buf);
    strcpy(buf, "ccccc");
    socket_send(sockfd, buf);
}

int init_socket(char *server_ip, int server_port)
{
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        perror_exit("socket");

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
        perror_exit("inet_pton");

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        perror_exit("connect");

    return sockfd;
}

void socket_send(int socket, char *buffer)
{
    ssize_t bytes_read = write(socket, buffer, strlen(buffer));
    if (bytes_read == -1)
        perror_exit("write socket");
}