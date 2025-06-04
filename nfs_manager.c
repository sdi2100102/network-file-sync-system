#define _XOPEN_SOURCE 700 // required for sigaction
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/signal.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>

#include "utils.h"
#include "nfs_log.h"
#include "sync_info_mem_store.h"
#include "command.h"
#include "nfs_log.h"
#include "exec_report.h"
#include "nfs_workers.h"
#include "file_operation.h"

#define BUF_SIZE 1024
#define READ 0
#define WRITE 1
#define DEFAULT_WORKER_LIMIT 5
#define DEFAULT_WORKER_BUFFER_SIZE 10

typedef struct
{
    char logfile_path[BUF_SIZE];
    char config_file_path[BUF_SIZE];
    int worker_limit;
    FILE *config_file;
    SyncInfoMemStore sync_info_mem_store;
    int inotify_fd;
    struct sigaction sa;
    char command_string[BUF_SIZE];
    Command command;
    int shutdown;
    int logfile_fd;
    int worker_pipes_fd[2];
    int console_port_number;
    int worker_buffer_size;
    int console_socket;
} ManagerInfo;

ManagerInfo *manager_init(int argc, char *argv[]);
void close_manager(ManagerInfo *manager_info);
void manager(int argc, char *argv[]);
void read_arguments(int argc, char *argv[], ManagerInfo *manager_info);
void read_config(ManagerInfo *manager_info);
void nfs_add(ManagerInfo *manager_info, SyncInfo sync_info);
void nfs_cancel(ManagerInfo *manager_info, SyncInfo sync_info);
void check_commands(ManagerInfo *manager_info);
void execute_command(ManagerInfo *manager_info);
int server_socket_init(int port);
void console_remote_read(ManagerInfo *manager_info);
void queue_operation(ManagerInfo *manager_info, OperationInfo operation_info);
void string_args_to_sync_info(char *source_string, char *target_string, SyncInfo *sync_info);

int main(int argc, char *argv[])
{
    manager(argc, argv);
    exit(EXIT_SUCCESS);
}

void manager(int argc, char *argv[])
{
    ManagerInfo *manager_info = manager_init(argc, argv);

    while (!manager_info->shutdown)
    {
        check_commands(manager_info);
    }

    close_manager(manager_info);
}

ManagerInfo *manager_init(int argc, char *argv[])
{
    ManagerInfo *manager_info = malloc(sizeof(ManagerInfo));
    manager_info->sync_info_mem_store = sims_init();

    read_arguments(argc, argv, manager_info);

    if ((manager_info->logfile_fd = open(manager_info->logfile_path, O_CREAT | O_WRONLY | O_TRUNC, 0777)) == -1)
        perror_exit("open logfile");

    manager_info->console_socket = server_socket_init(manager_info->console_port_number);

    workers_init(manager_info->worker_limit, manager_info->worker_buffer_size);

    read_config(manager_info);

    manager_info->command = cmd_init();

    manager_info->shutdown = 0;

    return manager_info;
}

void close_manager(ManagerInfo *manager_info)
{
    int status;
    char message[BUF_SIZE];
    strcpy(message, "Waiting for all active worker_list to finish.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->console_socket);

    strcpy(message, "Processing remaining queued tasks.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->console_socket);

    sims_free(manager_info->sync_info_mem_store);

    workers_close();

    strcpy(message, "Manager shutdown complete.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->console_socket);
    log_end_message(manager_info->console_socket);

    close(manager_info->console_socket);

    cmd_free(manager_info->command);
    if (close(manager_info->logfile_fd) == -1)
        perror_exit("close logfile");
    free(manager_info);
}

void read_arguments(int argc, char *argv[], ManagerInfo *manager_info)
{
    if (argc == 11 &&
        !strcmp(argv[1], "-l") &&
        !strcmp(argv[3], "-c") &&
        !strcmp(argv[5], "-n") &&
        !strcmp(argv[7], "-p") &&
        !strcmp(argv[9], "-b"))
    {
        strcpy(manager_info->logfile_path, argv[2]);
        strcpy(manager_info->config_file_path, argv[4]);
        int worker_limit = atoi(argv[6]);
        if (worker_limit < 1)
            manager_info->worker_limit = DEFAULT_WORKER_LIMIT;
        else
            manager_info->worker_limit = worker_limit;

        manager_info->console_port_number = atoi(argv[8]);

        int buffer_size = atoi(argv[10]);
        if (buffer_size > 0)
            manager_info->worker_buffer_size = buffer_size;
        else
            manager_info->worker_buffer_size = DEFAULT_WORKER_BUFFER_SIZE;
    }
    else
    {
        fprintf(stderr, "Usage: %s -l <manager_logfile> -c <manager_info->config_file> -n <worker_limit> -p <port_number> -b <bufferSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

void read_config(ManagerInfo *manager_info)
{
    manager_info->config_file = fopen(manager_info->config_file_path, "r");
    if (manager_info->config_file == NULL)
        perror_exit("fopen manager config");

    SyncInfo sync_info;
    sync_info.from_config = 1;

    char source_string[BUF_SIZE];
    char target_string[BUF_SIZE];
    while (fscanf(manager_info->config_file, "%s %s", source_string, target_string) == 2) // read config entries
    {
        string_args_to_sync_info(source_string, target_string, &sync_info);
        printf("=========%s %s\n", source_string, target_string);
        printf("%s %s %d\n", sync_info.source_dir, sync_info.source_ip, sync_info.source_port);
        printf("%s %s %d\n", sync_info.target_dir, sync_info.target_ip, sync_info.target_port);
        nfs_add(manager_info, sync_info);
    }

    if (ferror(manager_info->config_file))
        perror_exit("fscanf manager config");

    if (fclose(manager_info->config_file) == EOF)
        perror_exit("fclose manager config");
}

void nfs_add(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];
    if (sims_exists(manager_info->sync_info_mem_store, sync_info)) // directory already monitored
    {
        /* invalid result to stdout only */
        snprintf(message, sizeof(message), "Already in queue: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->console_socket);
        log_end_message(manager_info->console_socket);
        return;
    }

    if (!dir_exists(sync_info.source_dir) || !dir_exists(sync_info.target_dir)) // todo remove
    {
        log_end_message(manager_info->console_socket); // if no end message is sent, console hangs
        return;
    }

    /* Set sync info attributes */
    sync_info.error_num = 0;
    sync_info.last_sync = -1;
    sync_info.status = 1;

    sims_add(manager_info->sync_info_mem_store, sync_info); // add to sync_info_mem_store

    OperationInfo operation_info = {sync_info, "ALL", "FULL"};
    queue_operation(manager_info, operation_info);

    /* log to stdout, logfile and console */
    snprintf(message, sizeof(message), "Added directory: %.*s -> %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.target_dir), sync_info.target_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    if (!sync_info.from_config)
        log_timed_fd(message, manager_info->console_socket); // only send to console if not added from config

    snprintf(message, sizeof(message), "Monitoring started for %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    if (!sync_info.from_config)
    {
        log_timed_fd(message, manager_info->console_socket);
        log_end_message(manager_info->console_socket);
    }
}

void nfs_cancel(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];
    if (sims_find(manager_info->sync_info_mem_store, sync_info.source_dir, &sync_info) == -1)
    {
        /* invalid result to stdout only */
        snprintf(message, sizeof(message), "Directory not monitored: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->console_socket);
        log_end_message(manager_info->console_socket);
        return;
    }

    sync_info.status = 0;
    sims_remove(manager_info->sync_info_mem_store, sync_info.source_dir);

    /* Cancel queued operations */
    remove_operations_by_source_dir(sync_info.source_dir);

    /* log to stdout, logfile and console */
    snprintf(message, sizeof(message), "Monitoring stopped for %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    log_timed_fd(message, manager_info->console_socket);
    log_end_message(manager_info->console_socket);
}

void check_commands(ManagerInfo *manager_info)
{
    manager_info->command_string[0] = '\0'; // reset previous command

    console_remote_read(manager_info);

    char command_string_temp[BUF_SIZE]; // don't change manager command string
    strcpy(command_string_temp, manager_info->command_string);
    cmd_string_to_command(&(manager_info->command), command_string_temp);

    if (!(manager_info->command.type == UKNOWN))
    {
        execute_command(manager_info);
        char message[BUF_SIZE];
        snprintf(message, sizeof(message), "Command %.*s\n", (int)strlen(manager_info->command_string), manager_info->command_string);
        log_timed_fd(message, manager_info->logfile_fd); // log command to log file
    }
}

void execute_command(ManagerInfo *manager_info)
{
    SyncInfo sync_info;
    sync_info.from_config = 0; // not added from config
    char message[BUF_SIZE];
    switch (manager_info->command.type)
    {
    case ADD:
        printf("ADD: argument 1: %s, argument 2: %s\n", manager_info->command.arguments[1], manager_info->command.arguments[2]);
        string_args_to_sync_info(manager_info->command.arguments[1], manager_info->command.arguments[2], &sync_info);
        nfs_add(manager_info, sync_info);
        break;
    case CANCEL:
        strcpy(sync_info.source_dir, manager_info->command.arguments[1]);
        nfs_cancel(manager_info, sync_info);
        break;
    case SHUTDOWN:
        strcpy(message, "Shutting down manager...\n");
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->console_socket);
        manager_info->shutdown = 1;
        break;
    default:
        break;
    }
}

void console_remote_read(ManagerInfo *manager_info)
{
    ssize_t bytes_read = read(manager_info->console_socket, manager_info->command_string, sizeof(manager_info->command_string) - 1);
    if (bytes_read == -1)
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return; // no data available
        }
        else
            perror_exit("read console remote");
    if (bytes_read == 0)
    {
        return; // no data available
    }

    manager_info->command_string[bytes_read] = '\0';
}

int server_socket_init(int port)
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

    /* Accept connection */
    int sock;
    struct sockaddr_in client;
    socklen_t clientlen;
    if ((sock = accept(listening_socket, (struct sockaddr *)&client, &clientlen)) < 0)
        perror_exit("accept");
    close(listening_socket);

    fcntl(sock, F_SETFL, O_NONBLOCK);

    return sock;
}

void queue_operation(ManagerInfo *manager_info, OperationInfo operation_info)
{
    DIR *source_dir = opendir(operation_info.sync_info.source_dir);
    if (source_dir == NULL)
        perror_exit("opendir");

    struct dirent *file;
    while ((file = readdir(source_dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        char source_full_path[REPORT_FIELD_SIZE], target_full_path[REPORT_FIELD_SIZE];
        snprintf(source_full_path, sizeof(source_full_path), "%.*s/%.*s", (int)strlen(operation_info.sync_info.source_dir), operation_info.sync_info.source_dir, (int)strlen(file->d_name), file->d_name);

        OperationInfo op;
        strcpy(op.file_name, file->d_name);
        strcpy(op.operation, operation_info.operation);
        strcpy(op.sync_info.source_dir, operation_info.sync_info.source_dir);
        strcpy(op.sync_info.target_dir, operation_info.sync_info.target_dir);
        printf("operation: %s\n", op.operation == ADD ? "add" : "full");
        printf("source: %.*s\n", (int)strlen(op.sync_info.source_dir), op.sync_info.source_dir);
        printf("target: %.*s\n", (int)strlen(op.sync_info.target_dir), op.sync_info.target_dir); //todo remove
        // file_operation(op.sync_info.source_dir, op.sync_info.target_dir, op.file_name, op.operation);
        place_operation(op);
    }

    if (closedir(source_dir) == -1)
        perror_exit("closedir");
}

void parse_arg_string(const char *arg_string, char *dir, char *ip, int *port)
{
    char temp[BUF_SIZE];
    strncpy(temp, arg_string, BUF_SIZE - 1);
    temp[BUF_SIZE - 1] = '\0';

    char *at = strchr(temp, '@');
    char *colon = strchr(temp, ':');

    if (!at || !colon || colon < at)
    {
        fprintf(stderr, "Invalid argument format: %s\n", arg_string);
        dir[0] = ip[0] = '\0';
        *port = -1;
        return;
    }

    *at = '\0';
    *colon = '\0';

    strncpy(dir, temp, BUF_SIZE - 1);
    dir[BUF_SIZE - 1] = '\0';

    strncpy(ip, at + 1, INET6_ADDRSTRLEN - 1);
    ip[INET6_ADDRSTRLEN - 1] = '\0';

    *port = atoi(colon + 1);
}

void string_args_to_sync_info(char *source_string, char *target_string, SyncInfo *sync_info)
{
    parse_arg_string(source_string, sync_info->source_dir, sync_info->source_ip, &sync_info->source_port);
    parse_arg_string(target_string, sync_info->target_dir, sync_info->target_ip, &sync_info->target_port);
}