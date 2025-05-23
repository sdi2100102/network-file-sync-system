#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "nfs_log.h"
#include "utils.h"

void time_message(const char message[], char output[], size_t output_size)
{
    char time_buf[TIME_FORMAT_SIZE];
    format_time(time(NULL), time_buf);
    snprintf(output, output_size, "%s %s", time_buf, message);
}

void log_timed_stdout(char message[])
{
    char output[strlen(message) + TIME_FORMAT_SIZE + 2]; 
    time_message(message, output, sizeof(output));
    printf("%s", output);
}

void log_timed_fd(char message[], int fd)
{
    char output[strlen(message) + TIME_FORMAT_SIZE + 2]; 
    time_message(message, output, sizeof(output));
    if (write(fd, output, strlen(output)) == -1)
        perror_exit("write message");
}

void log_untimed_fd(char message[], int fd)
{
    if (write(fd, message, strlen(message)) == -1)
        perror_exit("write message");
}

void log_end_message(int fd)
{
    if(write(fd, END_OF_MESSAGE, strlen(END_OF_MESSAGE)) == -1)
        perror_exit("write message");
}