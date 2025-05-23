#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include "nfs_log.h"
#include "utils.h"
#include "command.h"

#define BUF_SIZE 1024
#define nfs_IN_PATH "nfs_in"
#define nfs_OUT_PATH "nfs_out"

typedef struct
{
    char logfile_path[BUF_SIZE];
    int logfile_fd;
    Command command;
    char command_string[BUF_SIZE];
    int nfs_in, nfs_out;
    char response[BUF_SIZE];
    int host_ip, host_port;
} ConsoleInfo;
ConsoleInfo console;

void console_init(int argc, char *argv[]);
void console_run();
void console_close();
void read_arguments(int argc, char *argv[]);
void nfs_in_init();
void nfs_out_init();
void nfs_in_close();
void nfs_out_close();
void nfs_in_send(char *message);
void nfs_out_read();

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

    nfs_in_init();
    nfs_out_init();
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

        nfs_in_send(console.command_string);

        /* log command */
        char message[BUF_SIZE];
        snprintf(message, sizeof(message), "Command %.*s\n", (int)strlen(console.command_string), console.command_string);
        log_timed_fd(message, console.logfile_fd);

        nfs_out_read();

        if (console.command.type == SHUTDOWN)
            break;
    }
}

void console_close()
{
    if (close(console.logfile_fd) == -1)
        perror_exit("close logfile");

    nfs_in_close();
    nfs_out_close();

    cmd_free(console.command);
}

void read_arguments(int argc, char *argv[])
{
    if (argc == 7 &&
        !strcmp(argv[1], "-l") &&
        !strcmp(argv[3], "-h") &&
        !strcmp(argv[5], "-p"))
    {
        strcpy(console.logfile_path, argv[2]);
        console.host_ip=atoi(argv[4]);
        console.host_port=atoi(argv[6]);
    }
    else
    {
        fprintf(stderr, "Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

void nfs_in_init()
{
    if ((console.nfs_in = open(nfs_IN_PATH, O_WRONLY)) < 0)
        perror_exit("open nfs_in");
}

void nfs_out_init()
{
    if ((console.nfs_out = open(nfs_OUT_PATH, O_RDONLY)) < 0)
        perror_exit("open nfs_out");
}

void nfs_in_close()
{
    if (close(console.nfs_in) == -1)
        perror_exit("close nfs_in");
}

void nfs_out_close()
{
    if (close(console.nfs_out) == -1)
        perror_exit("close nfs_out");
}

void nfs_in_send(char *message)
{
    if (write(console.nfs_in, message, strlen(message)) == -1)
        perror_exit("write nfs_in");
}

void nfs_out_read()
{
    char buffer[BUF_SIZE];
    while (1)
    {
        ssize_t n = read(console.nfs_out, buffer, sizeof(buffer) - 1);
        if (n < 0)
            perror_exit("read nfs_out");
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