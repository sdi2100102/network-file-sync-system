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

#define perror2(s, e) fprintf(stderr, "%s: %s\n", s, strerror(e))

#define BUF_SIZE 1024

typedef struct
{
    char source_dir[BUF_SIZE]; // acts as key for SyncInfo entries
    char source_ip[INET6_ADDRSTRLEN];
    int source_port;
    char target_dir[BUF_SIZE];
    int target_port;
    char target_ip[INET6_ADDRSTRLEN];
    int inotify_wd;
    time_t last_sync;
    int error_num;
    int status;
    int from_config;
} SyncInfo;

typedef struct
{
    SyncInfo sync_info;
    char file_name[BUF_SIZE];
    char operation[BUF_SIZE];
} OperationInfo;

int client_socket_init(char *server_ip, int server_port);
void client_socket_send(int socket, char *buffer);
void split_first_word(const char *input, char *first_word, char *rest);
void pull_push(OperationInfo op);
void queue_operation(OperationInfo operation_info);
void place_operation(OperationInfo op);
void perror_exit(char *message);
void perror_exit2(char *message, int error);
void wait_for_ack(int sockfd);
void send_string(int fd, char string[]);

int main(void)
{
    // SyncInfo sync_info;
    // strcpy(sync_info.source_dir, "/dir");
    // strcpy(sync_info.source_ip, "127.0.0.1");
    // sync_info.source_port = 8001;
    // strcpy(sync_info.target_dir, "/dir");
    // sync_info.target_port = 8002;
    // strcpy(sync_info.target_ip, "127.0.0.1");
    // OperationInfo operation_info = {sync_info, "ALL", "FULL"};
    while (1)
    {
        int source_sock1 = client_socket_init("127.0.0.1", 8001);
        int source_sock2 = client_socket_init("127.0.0.1", 8001);
        send_string(source_sock1, "pull /dir/file1.txt");
        send_string(source_sock2, "pull /dir/file2.txt");

        char buf[BUF_SIZE];
        int bytes_read = read(source_sock1, buf, sizeof(buf) - 1);
        buf[bytes_read] = '\0';
        printf("Received1: %s\n", buf);

        bytes_read = read(source_sock2, buf, sizeof(buf) - 1);
        buf[bytes_read] = '\0';
        printf("Received2: %s\n", buf);
    }
    return 0;
}

void queue_operation(OperationInfo op)
{
    int source_sock = client_socket_init(op.sync_info.source_ip, op.sync_info.source_port);

    /* list files from source */
    char buffer[BUF_SIZE];
    strcpy(buffer, "list ");
    strcat(buffer, op.sync_info.source_dir);
    printf("SENT: %s\n", buffer);
    client_socket_send(source_sock, buffer);

    char line[FILENAME_MAX];
    int bytes_read, i, line_pos = 0;
    int done = 0;
    while (!done && (bytes_read = read(source_sock, buffer, sizeof(buffer))) > 0)
    {
        for (i = 0; i < bytes_read; ++i)
        {
            char c = buffer[i];

            // Check for overflow
            if (line_pos >= FILENAME_MAX - 1)
                perror_exit("Filename too long");

            if (c == '\n')
            {
                line[line_pos] = '\0'; // null terminate the line

                if (strcmp(line, ".") == 0)
                {
                    done = 1;
                    break;
                }

                printf("%s\n", line);
                strcpy(op.file_name, line);
                place_operation(op);
                line_pos = 0; // Reset for next line
            }
            else
            {
                line[line_pos++] = c;
            }
        }
    }

    if (bytes_read < 0)
        perror_exit("read source socket");

    close(source_sock);
}

void place_operation(OperationInfo op)
{
    //...
    // thread stuff
    //...

    pull_push(op);
}

void pull_push(OperationInfo op)
{
    int source_sock = client_socket_init(op.sync_info.source_ip, op.sync_info.source_port);
    int target_sock = client_socket_init(op.sync_info.target_ip, op.sync_info.target_port);

    /* Send pull command */
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "pull %.*s/%.*s",
             (int)strlen(op.sync_info.source_dir), op.sync_info.source_dir,
             (int)strlen(op.file_name), op.file_name);
    printf("SENT: %s\n", buf);
    client_socket_send(source_sock, buf);

    /* Read data */
    int bytes_read = read(source_sock, buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';

    char chunk_size[BUF_SIZE];
    char rest[BUF_SIZE];
    split_first_word(buf, chunk_size, rest);

    char message[BUF_SIZE];
    snprintf(message, BUF_SIZE, "push %.*s/%.*s %.*s %.*s",
             (int)strlen(op.sync_info.target_dir), op.sync_info.target_dir,
             (int)strlen(op.file_name), op.file_name,
             (int)strlen(chunk_size), chunk_size,
             (int)strlen(rest), rest);
    client_socket_send(target_sock, message);
    printf("SENT: %s\n", message);

    printf("Size: %s\n", chunk_size);
    printf("Data first written: %s\n", rest);
    int bytes_to_read = atoi(chunk_size);
    bytes_to_read -= strlen(rest);
    while (bytes_to_read > 0)
    {
        bytes_read = read(source_sock, buf, sizeof(buf) - 1);
        if (bytes_read == -1)
            perror_exit("read socket");
        buf[bytes_read] = '\0';
        printf("SENT: %s", buf);
        client_socket_send(target_sock, buf);
        bytes_to_read -= bytes_read;
    }

    close(source_sock);
    close(target_sock);
}

int client_socket_init(char *server_ip, int server_port)
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

void client_socket_send(int socket, char *buffer)
{
    ssize_t bytes_read = write(socket, buffer, strlen(buffer));
    if (bytes_read == -1)
        perror_exit("write socket");
}

void split_first_word(const char *input, char *first_word, char *rest)
{
    int i = 0, j = 0;

    while (isspace(input[i]))
        i++;

    // extract first word
    while (input[i] && !isspace(input[i]))
    {
        first_word[j++] = input[i++];
    }
    first_word[j] = '\0';

    while (isspace(input[i]))
        i++;

    // rest of the string
    strcpy(rest, input + i);
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

void wait_for_ack(int sockfd)
{
    char response[BUF_SIZE];
    response[0] = '\0';
    int bytes_read;
    if ((bytes_read = read(sockfd, response, BUF_SIZE)) == -1)
        perror_exit("read socket");
    response[bytes_read] = '\0';
    if (strcmp(response, "ACK") != 0)
        perror_exit("ACK not received");
}

void send_string(int fd, char string[])
{
    if (write(fd, string, strlen(string)) == -1)
        perror_exit2("write socket", errno);
}