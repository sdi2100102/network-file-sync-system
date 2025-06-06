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
#include <pthread.h>

#include "debug.h"

#define BUF_SIZE 1024

#define perror2(s, e) fprintf(stderr, "%s: %s\n", s, strerror(e))

void *client_run(void *arg);
int read_arguments(int argc, char *argv[]);
void command_list(char *source_dirint, int manager_socket);
void command_pull(char *path_name, int manager_socket);
void command_push(char *path_name, int manager_socket);
int listening_socket_init(int port);
void send_string(int manager_socket, char string[]);
void send_error(int manager_socket, char string[]);
void pull_error(int manager_socket, char message[]);
void push_error(int manager_socket, char message[]);
void perror_exit(char *message);
void perror_exit2(char *message, int error);
int count_file_size(int source_fd);
void send_file_size(int manager_socket, int file_size);
void read_command_string(char command_string[], int manager_socket);
void recieve_ack(int manager_socket);
int read_file_size(int manager_socket);

int main(int argc, char *argv[])
{
    int manager_port = read_arguments(argc, argv);
    int listening_socket = listening_socket_init(manager_port);
    while (1)
    {
        /* Accept connection */
        int sock;
        struct sockaddr_in client;
        socklen_t clientlen = sizeof(client);
        if ((sock = accept(listening_socket, (struct sockaddr *)&client, &clientlen)) < 0)
            perror_exit2("accept", errno);

        /* Create detached tread */
        pthread_t thread;
        // Create the thread
        if (pthread_create(&thread, NULL, client_run, (void *)(long)sock) != 0)
            perror_exit2("pthread_create", errno);

        // Detach the thread
        if (pthread_detach(thread) != 0)
            perror_exit2("pthread_detach", errno);
    }

    close(listening_socket);
    return 0;
}

void *client_run(void *arg)
{
    int manager_socket = (int)(long)arg;

    DEBUG_PRINT("1. New manager socket with fd: %d", manager_socket);

    /* Read command string from manager */
    char command_string[BUF_SIZE];
    read_command_string(command_string, manager_socket);

    DEBUG_PRINT("1.5. Read command string from manager: %s", command_string);

    /* Parse command */
    char *command_type = strtok(command_string, " \n");
    char *path_name = strtok(NULL, " \n");
    if (path_name == NULL)
        perror_exit2("command parsing", errno);

    /* Execute command */
    if (strcmp(command_type, "list") == 0)
    {
        DEBUG_PRINT("2. Command: list %s", path_name);
        command_list(path_name, manager_socket);
    }
    else if (strcmp(command_type, "pull") == 0)
    {
        DEBUG_PRINT("2. Command: pull %s", path_name);
        command_pull(path_name, manager_socket);
    }
    else if (strcmp(command_type, "push") == 0)
    {
        DEBUG_PRINT("2. Command: push %s", path_name);
        command_push(path_name, manager_socket);
    }

    if (close(manager_socket) == -1)
        perror_exit2("close", errno);
    return NULL;
}

void command_list(char *source_dir_name, int manager_socket)
{
    if (source_dir_name[0] == '/')
        source_dir_name++; // ignore first '\' in path name

    /* Open directory */
    DIR *source_dir = opendir(source_dir_name);
    if (source_dir == NULL)
        return;

    /* Send file names */
    struct dirent *file;
    while ((file = readdir(source_dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        strcat(file->d_name, "\n");
        send_string(manager_socket, file->d_name);
    }

    if (closedir(source_dir) == -1)
        perror_exit("closedir");
}

void command_pull(char *path_name, int manager_socket)
{
    char buf[BUF_SIZE];

    /* Ignore first '\' in path name */
    if (path_name[0] == '/')
        path_name++;

    /* Open file */
    int source_fd;
    if ((source_fd = open(path_name, O_RDONLY)) == -1)
        pull_error(manager_socket, "open");

    /* Count file size */
    int file_size = count_file_size(source_fd);

    /* Reset file descriptor */
    if (lseek(source_fd, 0, SEEK_SET) == -1)
        pull_error(manager_socket, "lseek");

    /* Send file size */
    send_file_size(manager_socket, file_size);

    DEBUG_PRINT("3. Send file size %d", file_size);

    /* Receive ACK */
    recieve_ack(manager_socket);

    DEBUG_PRINT("4. Received ACK");

    DEBUG_PRINT("5. Start sending file");

    /* Send file */
    int bytes_read;
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
    {
        buf[bytes_read] = '\0';
        DEBUG_PRINT("%s", buf);
        fflush(stdout);
        if (write(manager_socket, buf, bytes_read) == -1)
            pull_error(manager_socket, "write");
    }
    if (bytes_read == -1)
        pull_error(manager_socket, "read");

    DEBUG_PRINT("\n");
}

void command_push(char *path_name, int manager_socket)
{
    /* Ignore first '\' in path name */
    if (path_name[0] == '/')
        path_name++;

    /* Open file */
    int dest_fd;
    if ((dest_fd = open(path_name, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
        push_error(manager_socket, "open");

    /* Send first ACK */
    send_string(manager_socket, "ACK");

    /* Read file size */
    int file_size = read_file_size(manager_socket);

    DEBUG_PRINT("3. Recieved file size %d", file_size);

    /* Send second ACK */
    send_string(manager_socket, "ACK");

    DEBUG_PRINT("4. Recieved second ACK");

    DEBUG_PRINT("5. Start recieving file: %s", path_name);

    /* Recieve file and write to dest*/
    char buf[BUF_SIZE];
    int bytes_read;
    while ((bytes_read = read(manager_socket, buf, BUF_SIZE)) > 0)
    {
        buf[bytes_read] = '\0';

        DEBUG_PRINT("%s", buf);

        if (write(dest_fd, buf, bytes_read) == -1)
            push_error(manager_socket, "write");
    }
    if (bytes_read == -1)
        push_error(manager_socket, "read");
}

int read_arguments(int argc, char *argv[])
{
    int manager_port;
    if (argc == 3 && !strcmp(argv[1], "-p"))
    {
        manager_port = atoi(argv[2]);
    }
    else
    {
        fprintf(stderr, "Usage: %s -p <port_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    return manager_port;
}

int listening_socket_init(int port)
{
    /* Create socket */
    int listening_socket;
    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");

    /* Override TCP socket reuse */
    int opt = 1;
    setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind socket to address */
    struct sockaddr_in server;
    server.sin_family = AF_INET; /* Internet domain */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);
    if (bind(listening_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
        perror_exit("bind");

    /* Listen for connections */
    if (listen(listening_socket, 1) < 0)
        perror_exit("listen");

    return listening_socket;
}

void send_string(int fd, char string[])
{
    if (write(fd, string, strlen(string)) == -1)
        perror_exit2("write socket", errno);
}

void send_error(int manager_socket, char string[])
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "%d %s", -1, strerror(errno));
    send_string(manager_socket, buf);
}

void pull_error(int manager_socket, char message[])
{
    send_error(manager_socket, message);
    perror_exit(message);
}

void push_error(int manager_socket, char message[])
{
    send_error(manager_socket, message);
    perror_exit(message);
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

void perror_exit2(char *message, int error)
{
    perror2(message, error);
    exit(EXIT_FAILURE);
}

int count_file_size(int source_fd)
{
    /* Count file size */
    char buf[BUF_SIZE];
    int bytes_read;
    int total_bytes_read = 0;
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
        total_bytes_read += bytes_read;
    return total_bytes_read;
}

void send_file_size(int manager_socket, int file_size)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "%d", file_size);
    send_string(manager_socket, buf);
}

void read_command_string(char command_string[], int manager_socket)
{
    int bytes_read;
    if ((bytes_read = read(manager_socket, command_string, BUF_SIZE)) == -1)
        perror_exit("read socket");
    command_string[bytes_read] = '\0';

    if (bytes_read == 0)
    {
        DEBUG_PRINT("Connection closed | manager socket: %d", manager_socket);
        close(manager_socket);
        perror_exit2("read manager command", errno);
    }
}

void recieve_ack(int manager_socket)
{
    char buf[BUF_SIZE];
    int bytes_read;
    if ((bytes_read = read(manager_socket, buf, strlen("ACK"))) == -1)
        perror_exit("read socket");
    buf[bytes_read] = '\0';
    if (strcmp(buf, "ACK") != 0)
    {
        DEBUG_PRINT("Instead of ack, Recieved (in %d bytes): %s", bytes_read, buf);
        perror_exit("ACK not received");
    }
}

int read_file_size(int manager_socket)
{
    char buf[BUF_SIZE];
    int bytes_read;
    if ((bytes_read = read(manager_socket, buf, BUF_SIZE)) == -1)
        perror_exit("read socket");
    buf[bytes_read] = '\0';
    return atoi(buf);
}
