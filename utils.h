#ifndef UTILS_H
#define UTILS_H

#define TIME_FORMAT_SIZE 22

char *find_between(const char *str, const char *start, const char *end, char *result, size_t result_size);
void format_time(time_t t, char *buf);
void perror_exit(char* message);
int dir_exists(const char *path);

#endif // UTILS_H