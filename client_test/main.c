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

void sync_dir(char *source_dir, char *target_dir);
void pull(int sockfd, char *file_name);
int init_socket(char *server_ip, int server_port);
void socket_send(int socket, char *buffer);
void push(int sockfd);
void split_first_word(const char *input, char *first_word, char *rest);
void pull_push(char *source_dir, char *target_dir, char *file_name);

void list(int source_sock, char *source_dir);
void read_filenames_from_socket(int sockfd, char *source_dir);

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

int main(void)
{
    // int source_sock = init_socket("127.0.0.1", 8088);

    // int source_port = 8001;
    // char *source_ip = "127.0.0.1";
    // int source_sock = init_socket(source_ip, source_port);

    // // int target_port = 8002;
    // // char *target_ip = "127.0.0.1";
    // // int target_sock = init_socket(target_ip, target_port);

    // sleep(1);
    // char *source_dir = "/dir";
    // char *target_dir = "/dir";
    // sync_dir(source_dir, target_dir);

    // read_filenames_from_socket(source_sock, source_dir);
    // // pull(source_sock, "dir/file1.txt");
    // // push(source_sock);

    // // push(target_sock);

    sync_dir("/dir", "/dir");
    // pull_push("/dir", "/dir", "file1.txt");

    // close(source_sock);
    // close(target_sock);
    return 0;
}

void read_filenames_from_socket(int sockfd, char *source_dir)
{

    char buffer[BUF_SIZE];
    strcpy(buffer, "list ");
    strcat(buffer, source_dir);
    printf("SENT: %s\n", buffer);
    fflush(stdout);
    socket_send(sockfd, buffer);

    char line[FILENAME_MAX];
    int bytes_read, i, line_pos = 0;
    int done = 0;

    while (!done && (bytes_read = read(sockfd, buffer, sizeof(buffer))) > 0)
    {
        for (i = 0; i < bytes_read; ++i)
        {
            char c = buffer[i];

            // Check for overflow
            if (line_pos >= FILENAME_MAX - 1)
                perror_exit("Filename too long");

            if (c == '\n')
            {
                line[line_pos] = '\0'; // Null-terminate the line

                if (strcmp(line, ".") == 0)
                {
                    done = 1;
                    break;
                }

                printf("%s\n", line);
                line_pos = 0; // Reset for next line
            }
            else
            {
                line[line_pos++] = c;
            }
        }
    }

    if (bytes_read < 0)
        perror_exit("read sockfd");
}

void list(int source_sock, char *source_dir)
{
    char buffer[1024];
    strcpy(buffer, "list ");
    strcat(buffer, source_dir);
    printf("SENT: %s\n", buffer);
    socket_send(source_sock, buffer);

    int bytes_read;
    char file_names[1024];
    while (1)
    {
        if ((bytes_read = read(source_sock, buffer, 1024)) == -1)
            perror_exit("read sockfd");
    }

    buffer[bytes_read] = '\0';
    char *file_name = strtok(buffer, " \n");
    while (file_name != NULL)
    {
        printf("Operation on: %s\n", file_name);
        file_name = strtok(NULL, " \n");
    }
}

void sync_dir(char *source_dir, char *target_dir)
{
    int source_sock = init_socket("127.0.0.1", 8001);

    /* list files from source */
    char buffer[BUF_SIZE];
    strcpy(buffer, "list ");
    strcat(buffer, source_dir);
    printf("SENT: %s\n", buffer);
    socket_send(source_sock, buffer);

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
                pull_push(source_dir, target_dir, line); // todo replace with thread
                line_pos = 0;                            // Reset for next line
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

void pull_old(int sockfd, char *file_name)
{
    char buf[BUF_SIZE];
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

            char message[BUF_SIZE];
            strcpy(message, buf);

            break;
        }
        printf("%s", buf);
        fflush(stdout);
    }
}

void pull_push(char *source_dir, char *target_dir, char *file_name)
{
    int source_sock = init_socket("127.0.0.1", 8001);
    int target_sock = init_socket("127.0.0.1", 8002);

    /* Send pull command */
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "pull %.*s/%.*s",
             (int)strlen(source_dir), source_dir,
             (int)strlen(file_name), file_name);
    printf("SENT: %s\n", buf);
    socket_send(source_sock, buf);
    sleep(1);

    /* Read data */
    int bytes_read = read(source_sock, buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';

    char chunk_size[BUF_SIZE];
    char rest[BUF_SIZE];
    split_first_word(buf, chunk_size, rest);

    char message[BUF_SIZE];
    snprintf(message, BUF_SIZE, "push %.*s/%.*s %.*s %.*s",
             (int)strlen(target_dir), target_dir,
             (int)strlen(file_name), file_name,
             (int)strlen(chunk_size), chunk_size,
             (int)strlen(rest), rest);
    socket_send(target_sock, message);
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
        socket_send(target_sock, buf);
        bytes_to_read -= bytes_read;
    }

    close(source_sock);
    close(target_sock);
}

void pull(int sockfd, char *file_name)
{
    /* Send pull command */
    char buf[BUF_SIZE];
    strcpy(buf, "pull ");
    strcat(buf, file_name);
    socket_send(sockfd, buf);
    sleep(1);

    /* Read data */
    int bytes_read = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';

    char first_word[BUF_SIZE];
    char rest[BUF_SIZE];
    split_first_word(buf, first_word, rest);

    printf("Size: %s\n", first_word);
    printf("Data first written: %s\n", rest);
    int bytes_to_read = atoi(first_word);
    bytes_to_read -= strlen(rest);
    while (bytes_to_read > 0)
    {
        bytes_read = read(sockfd, buf, sizeof(buf) - 1);
        if (bytes_read == -1)
            perror_exit("read socket");
        buf[bytes_read] = '\0';
        printf("%s", buf);
        bytes_to_read -= bytes_read;
    }
}

void push(int sockfd)
{
    char buf[1024];
    strcpy(buf, "push /dir/file1.txt 15 aaaaa");
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