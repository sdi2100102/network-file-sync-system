#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

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

void perror_exit(char* message)
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
