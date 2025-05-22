#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include "fss_log.h"
#include "utils.h"
#include "command.h"

#define BUF_SIZE 1024
#define FSS_IN_PATH "fss_in"
#define FSS_OUT_PATH "fss_out"

typedef struct
{
    char logfile_path[BUF_SIZE];
    int logfile_fd;
    Command command;
    char command_string[BUF_SIZE];
    int fss_in, fss_out;
    char response[BUF_SIZE];
} ConsoleInfo;
ConsoleInfo console;

void console_init(int argc, char *argv[]);
void console_run();
void console_close();
void read_arguments(int argc, char *argv[]);
void fss_in_init();
void fss_out_init();
void fss_in_close();
void fss_out_close();
void fss_in_send(char *message);
void fss_out_read();

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

    fss_in_init();
    fss_out_init();
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

        fss_in_send(console.command_string);

        /* log command */
        char message[BUF_SIZE];
        snprintf(message, sizeof(message), "Command %.*s\n", (int)strlen(console.command_string), console.command_string);
        log_timed_fd(message, console.logfile_fd);

        fss_out_read();

        if (console.command.type == SHUTDOWN)
            break;
    }
}

void console_close()
{
    if (close(console.logfile_fd) == -1)
        perror_exit("close logfile");

    fss_in_close();
    fss_out_close();

    cmd_free(console.command);
}

void read_arguments(int argc, char *argv[])
{
    if (argc == 3 && !strcmp(argv[1], "-l"))
        strcpy(console.logfile_path, argv[2]);
    else
    {
        fprintf(stderr, "Usage: %s -l <console-logfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

void fss_in_init()
{
    if ((console.fss_in = open(FSS_IN_PATH, O_WRONLY)) < 0)
        perror_exit("open fss_in");
}

void fss_out_init()
{
    if ((console.fss_out = open(FSS_OUT_PATH, O_RDONLY)) < 0)
        perror_exit("open fss_out");
}

void fss_in_close()
{
    if (close(console.fss_in) == -1)
        perror_exit("close fss_in");
}

void fss_out_close()
{
    if (close(console.fss_out) == -1)
        perror_exit("close fss_out");
}

void fss_in_send(char *message)
{
    if (write(console.fss_in, message, strlen(message)) == -1)
        perror_exit("write fss_in");
}

void fss_out_read()
{
    char buffer[BUF_SIZE];
    while (1)
    {
        ssize_t n = read(console.fss_out, buffer, sizeof(buffer) - 1);
        if (n < 0)
            perror_exit("read fss_out");
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