#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "utils.h"

#define perror2(s, e) fprintf(stderr, "%s: %s\n", s, strerror(e))

char *find_between(const char *str, const char *start, const char *end, char *result, size_t result_size)
{
    char *start_ptr = strstr(str, start);
    if (!start_ptr)
        return NULL;

    start_ptr += strlen(start);
    char *end_ptr = strstr(start_ptr, end);
    if (!end_ptr)
        return NULL;

    size_t len = end_ptr - start_ptr;
    if (len >= result_size)
        return NULL;

    strncpy(result, start_ptr, len);
    result[len] = '\0';
    return result;
}

void format_time(time_t t, char *buf)
{
    struct tm tm = *localtime(&t);
    strftime(buf, TIME_FORMAT_SIZE, "[%Y-%m-%d %H:%M:%S]", &tm);
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

int dir_exists(const char *path)
{
    struct stat info;

    if (stat(path, &info) != 0)
    {
        return 0; // unable to access the path
    }
    else if (S_ISDIR(info.st_mode))
    {
        return 1; // it's a directory
    }
    else
    {
        return 0; // it's not a directory
    }
}

void perror_exit2(char *message, int error)
{
    perror2(message, error);
    exit(EXIT_FAILURE);
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