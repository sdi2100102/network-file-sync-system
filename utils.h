#ifndef UTILS_H
#define UTILS_H

#define TIME_FORMAT_SIZE 22

#define BUF_SIZE 1024

char *find_between(const char *str, const char *start, const char *end, char *result, size_t result_size);
void format_time(time_t t, char *buf);
void perror_exit(char* message);
int dir_exists(const char *path);
void perror_exit2(char *message, int error);

int client_socket_init(char *server_ip, int server_port);
void client_socket_send(int socket, char *buffer);
void split_first_word(const char *input, char *first_word, char *rest);

void wait_for_ack(int sockfd);

#endif // UTILS_H