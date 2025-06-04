#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include "command.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "nfs_log.h"
#include "utils.h"

#define BUF_SIZE 1024

typedef struct
{
    char logfile_path[BUF_SIZE];
    int logfile_fd;
    Command command;
    char command_string[BUF_SIZE];
    char response[BUF_SIZE];
    int manager_port;
    char manager_ip[INET_ADDRSTRLEN];
    int manager_socket;
} ConsoleInfo;
ConsoleInfo console;

void console_init(int argc, char *argv[]);
void console_run();
void console_close();
void read_arguments(int argc, char *argv[]);
int client_socket_init(const char *server_ip, int server_port);
void manager_remote_read();

int main(int argc, char *argv[])
{
    console_init(argc, argv);
    console_run();
    console_close();
    exit(EXIT_SUCCESS);
}

void console_init(int argc, char *argv[])
{
    read_arguments(argc, argv);
    console.command = cmd_init();

    /* Open log file */
    if ((console.logfile_fd = open(console.logfile_path, O_CREAT | O_WRONLY | O_TRUNC, 0777)) == -1)
        perror_exit("open logfile");

    console.manager_socket = client_socket_init(console.manager_ip, console.manager_port);
}

void console_run()
{
    while (1)
    {
        printf("> ");
        if (fgets(console.command_string, BUF_SIZE, stdin) == NULL)
            break;
        console.command_string[strcspn(console.command_string, "\n")] = '\0';

        char command_string_temp[BUF_SIZE]; // don't change console command string
        strcpy(command_string_temp, console.command_string);
        cmd_string_to_command(&(console.command), command_string_temp);
        if (console.command.type == UKNOWN)
            continue;

        /* Send command to manager */
        if (write(console.manager_socket, console.command_string, strlen(console.command_string) + 1) < 0)
            perror_exit("write");

        /* log command */
        char message[BUF_SIZE];
        snprintf(message, sizeof(message), "Command %.*s\n", (int)strlen(console.command_string), console.command_string);
        log_timed_fd(message, console.logfile_fd);

        manager_remote_read();

        if (console.command.type == SHUTDOWN)
            break;
    }
}

void console_close()
{
    if (close(console.logfile_fd) == -1)
        perror_exit("close logfile");

    close(console.manager_socket);

    cmd_free(console.command);
}

void read_arguments(int argc, char *argv[])
{
    if (argc == 7 &&
        !strcmp(argv[1], "-l") &&
        !strcmp(argv[3], "-h") &&
        !strcmp(argv[5], "-p"))
    {
        strcpy(console.logfile_path, argv[2]);
        strcpy(console.manager_ip, argv[4]);
        console.manager_port = atoi(argv[6]);
    }
    else
    {
        fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_ip> -p <host_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

int client_socket_init(const char *server_ip, int server_port)
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

void manager_remote_read()
{
    char buffer[BUF_SIZE];
    while (1)
    {
        ssize_t n = read(console.manager_socket, buffer, sizeof(buffer) - 1);
        if (n < 0)
            perror_exit("read manager socket");
        if (n == 0)
            break;
        buffer[n] = '\0';

        if (strstr(buffer, END_OF_MESSAGE) != NULL)
        {
            *strstr(buffer, END_OF_MESSAGE) = '\0'; // remove END_OF_MESSAGE marker
            printf("%s", buffer);
            break;
        }
        log_untimed_fd(buffer, console.logfile_fd);
        printf("%s", buffer);
    }
}