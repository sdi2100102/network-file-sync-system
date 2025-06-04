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

#define END_OF_MESSAGE "__END__"

#define BUF_SIZE 1024

// todo: fix bug where it needs sleep(1) for reading or writing all data

void *client_run(void *arg);
int read_arguments(int argc, char *argv[]);
void command_list(char *source_dirint, int manager_socket);
void command_pull(char *path_name, int manager_socket);
void command_push(char *path_name, int chunk_size, char *data, int manager_socket);
int listening_socket_init(int port);
void send_string(int manager_socket, char string[]);
void send_error(int manager_socket, char string[]);
void pull_error(int manager_socket, char message[]);
void push_error(int manager_socket, char message[]);
void split_into_4(char *str, char **p1, char **p2, char **p3, char **p4);
void perror_exit(char *message);

int main(int argc, char *argv[])
{
    int manager_port = read_arguments(argc, argv);
    int listening_socket = listening_socket_init(manager_port);
    while (1) // todo add threads
    {
        /* Accept connection */
        int sock;
        struct sockaddr_in client;
        socklen_t clientlen = sizeof(client);
        if ((sock = accept(listening_socket, (struct sockaddr *)&client, &clientlen)) < 0)
            perror_exit("accept");

        /* Create detached tread */
        pthread_t thread;
        // Create the thread
        if (pthread_create(&thread, NULL, client_run, &sock) != 0)
            perror_exit("pthread_create");

        // Detach the thread
        if (pthread_detach(thread) != 0)
            perror_exit("pthread_detach");
    }

    close(listening_socket);
    return 0;
}

void *client_run(void *arg)
{
    int manager_socket = *(int *)arg;
    printf("Manager socket: %d\n", manager_socket); // todo remove

    /* Read command string from manager */
    char command_string[BUF_SIZE];
    command_string[0] = '\0';
    int bytes_read;
    if ((bytes_read = read(manager_socket, command_string, BUF_SIZE)) == -1)
        perror_exit("read socket");
    command_string[bytes_read] = '\0';

    if(bytes_read == 0)
        perror_exit("read socket");

    /* Parse command */
    char command_string_temp[BUF_SIZE]; // don't change client command string
    strcpy(command_string_temp, command_string);
    char *command_type = strtok(command_string_temp, " \n");
    if (strcmp(command_type, "list") == 0)
    {

        char *source_dir = strtok(NULL, " \n");
        if (source_dir == NULL)
            return NULL; // todo replace with exit thread
        printf("Command: list %s\n", source_dir);
        command_list(source_dir, manager_socket);
    }
    else if (strcmp(command_type, "pull") == 0)
    {

        char *path_name = strtok(NULL, " \n");
        if (path_name == NULL)
            return NULL;
        printf("Command: pull %s\n", path_name);
        command_pull(path_name, manager_socket);
    }
    else if (strcmp(command_type, "push") == 0)
    {

        char *command, *path_name, *chunk_size, *data;
        split_into_4(command_string, &command, &path_name, &chunk_size, &data); // split command arguments
        if (strlen(path_name) == 0 || strlen(chunk_size) == 0 || strlen(data) == 0)
        {
            return NULL;
        }
        printf("Command: push %s %s %s\n", path_name, chunk_size, data);
        command_push(path_name, atoi(chunk_size), data, manager_socket); // todo fix
    }

    close(manager_socket);
    return NULL; // todo replace with thread exit
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

    if (path_name[0] == '/')
        path_name++; // ignore first '\' in path name

    int source_fd;
    if ((source_fd = open(path_name, O_RDONLY)) == -1)
        pull_error(manager_socket, "open");

    /* Count file size */
    int bytes_read;
    int total_bytes_read = 0;
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
        total_bytes_read += bytes_read;

    if (bytes_read == -1)
        pull_error(manager_socket, "read");

    if (lseek(source_fd, 0, SEEK_SET) == -1)
        pull_error(manager_socket, "lseek");

    /* Send file size */
    char buf_size[BUF_SIZE];
    snprintf(buf_size, BUF_SIZE, "%d ", total_bytes_read);
    if (write(manager_socket, buf_size, strlen(buf_size)) == -1)
        pull_error(manager_socket, "write");

    /* Send file */
    printf("Data sent: ");
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
    {
        buf[bytes_read] = '\0';
        printf("%s", buf);
        fflush(stdout);
        if (write(manager_socket, buf, bytes_read) == -1)
            pull_error(manager_socket, "write");
    }
    if (bytes_read == -1)
        pull_error(manager_socket, "read");

    if (close(source_fd) == -1)
        pull_error(manager_socket, "close");

    if (write(manager_socket, END_OF_MESSAGE, strlen(END_OF_MESSAGE)) == -1)
        pull_error(manager_socket, "write");
}

void command_push(char *path_name, int chunk_size, char *data, int manager_socket)
{
    if (path_name[0] == '/')
        path_name++; // ignore first '\' in path name

    int dest_fd;
    switch (chunk_size)
    {
    case 0:
        break;
    case -1:
        /* Truncate file */
        if ((dest_fd = open(path_name, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
            push_error(manager_socket, "open");

        if (write(dest_fd, data, strlen(data)) == -1) // todo fix: write all data
            push_error(manager_socket, "write");

        if (close(dest_fd) == -1)
            push_error(manager_socket, "close");
        break;
    default:
        /* Append to file */
        if ((dest_fd = open(path_name, O_WRONLY | O_CREAT | O_APPEND, 0666)) == -1)
            push_error(manager_socket, "open");

        send_string(dest_fd, data); // write first chunk
        chunk_size -= strlen(data);

        /* Write remaining chunks */
        char buffer[1024];
        while (chunk_size > 0)
        {
            int bytes_read = read(manager_socket, buffer, sizeof(buffer));
            if (bytes_read == -1)
                push_error(manager_socket, "read");
            if (write(dest_fd, buffer, bytes_read) == -1)
                push_error(manager_socket, "write");
            chunk_size -= bytes_read;
        }

        if (close(dest_fd) == -1)
            push_error(manager_socket, "close");
        break;
    }
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
        perror_exit("write socket");
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
    send_string(manager_socket, END_OF_MESSAGE);
    perror_exit(message);
}

void push_error(int manager_socket, char message[])
{
    send_error(manager_socket, message);
    perror_exit(message);
}

void split_into_4(char *str, char **p1, char **p2, char **p3, char **p4)
{
    char *s = str;
    *p1 = *p2 = *p3 = *p4 = NULL;

    while (*s && isspace((unsigned char)*s))
        s++;

    if (!*s)
        return; // empty string

    // first word
    *p1 = s;
    while (*s && !isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    *s++ = '\0';

    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    // second word
    *p2 = s;
    while (*s && !isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    *s++ = '\0';

    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    // third word
    *p3 = s;
    while (*s && !isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    *s++ = '\0';

    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return;

    // the rest of the string
    *p4 = s;
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}