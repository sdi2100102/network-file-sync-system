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

#include "utils.h"
#include "fss_log.h"
#include "operation_queue.h"
#include "sync_info_mem_store.h"
#include "command.h"
#include "fss_log.h"
#include "exec_report.h"
#include "worker_list.h"

#define BUF_SIZE 1024
#define READ 0
#define WRITE 1
#define DEFAULT_WORKER_LIMIT 5
#define WORKER_PATH "fss_worker"
#define FSS_IN_PATH "fss_in"
#define FSS_OUT_PATH "fss_out"

volatile sig_atomic_t got_sigchld = 0; // used by SIGCHLD handler

typedef struct
{
    char logfile_path[BUF_SIZE];
    char config_file_path[BUF_SIZE];
    int worker_limit;
    FILE *config_file;
    SyncInfoMemStore sync_info_mem_store;
    OperationQueue operation_queue;
    int inotify_fd;
    struct sigaction sa;
    int fss_in, fss_out;
    char command_string[BUF_SIZE];
    Command command;
    int shutdown;
    int logfile_fd;
    int worker_pipes_fd[2];
    int active_workers;
    WorkerList worker_list;
} ManagerInfo;

ManagerInfo *manager_init(int argc, char *argv[]);
void monitor(ManagerInfo *manger_info);
void close_manager(ManagerInfo *manager_info);
void manager(int argc, char *argv[]);
void read_arguments(int argc, char *argv[], ManagerInfo *manager_info);
void read_config(ManagerInfo *manager_info);
void fss_add(ManagerInfo *manager_info, SyncInfo sync_info);
void fss_sync(ManagerInfo *manager_info, SyncInfo sync_info);
void fss_status(ManagerInfo *manager_info, SyncInfo sync_info);
void fss_cancel(ManagerInfo *manager_info, SyncInfo sync_info);
void execute_operation(ManagerInfo *manager_info, OperationInfo operation_info);
void check_changes(ManagerInfo *manager_info);
void apply_changes(ManagerInfo *manager_info, struct inotify_event *event, SyncInfo sync_info);
int execute_next_operation(ManagerInfo *manager_info);
void execute_queued_operations(ManagerInfo *manager_info);
void sigaction_init(ManagerInfo *manager_info);
void sigchld_handler(int sig);
void check_commands(ManagerInfo *manager_info);
void fss_in_init(ManagerInfo *manager_info);
void fss_in_read(ManagerInfo *manager_info);
void fss_in_close(ManagerInfo *manager_info);
void fss_out_init(ManagerInfo *manager_info);
void fss_out_close(ManagerInfo *manager_info);
void execute_command(ManagerInfo *manager_info);
void collect_workers(ManagerInfo *manager_info);
void update_sync_info(ManagerInfo *manager_info, char *source_dir, ExecReport);

int main(int argc, char *argv[])
{
    manager(argc, argv);
    exit(EXIT_SUCCESS);
}

void manager(int argc, char *argv[])
{
    ManagerInfo *manager_info = manager_init(argc, argv);

    monitor(manager_info);

    close_manager(manager_info);
}

ManagerInfo *manager_init(int argc, char *argv[])
{
    ManagerInfo *manager_info = malloc(sizeof(ManagerInfo));
    manager_info->sync_info_mem_store = sims_init();
    manager_info->operation_queue = opq_init();
    manager_info->worker_list = wl_init();

    read_arguments(argc, argv, manager_info);

    if ((manager_info->inotify_fd = inotify_init1(O_NONBLOCK)) < 0) // initialize inotify
        perror_exit("inotify_init");

    if ((manager_info->logfile_fd = open(manager_info->logfile_path, O_CREAT | O_WRONLY | O_TRUNC, 0777)) == -1)
        perror_exit("open logfile");

    fss_in_init(manager_info);
    fss_out_init(manager_info);

    read_config(manager_info);

    execute_queued_operations(manager_info); // config pairs synced before monitoring starts

    sigaction_init(manager_info);

    manager_info->command = cmd_init();

    manager_info->shutdown = 0;

    manager_info->active_workers = 0;

    return manager_info;
}

void monitor(ManagerInfo *manager_info)
{
    while (!manager_info->shutdown)
    {
        check_changes(manager_info);
        check_commands(manager_info);
        execute_next_operation(manager_info);
        if (got_sigchld)
            collect_workers(manager_info);
    }
}

void close_manager(ManagerInfo *manager_info)
{
    int status;
    char message[BUF_SIZE];
    strcpy(message, "Waiting for all active worker_list to finish.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->fss_out);

    while (wait(&status) > 0) // wait for all active worker_list
        ;

    strcpy(message, "Processing remaining queued tasks.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->fss_out);

    while (execute_next_operation(manager_info) >= 0) // empty operation queue
        ;

    SyncInfo sync_info;
    while (sims_pop(manager_info->sync_info_mem_store, &sync_info) == 0) // empty sync_info_mem_store
        if (inotify_rm_watch(manager_info->inotify_fd, sync_info.inotify_wd) == -1)
            perror_exit("inotify_rm_watch");

    if (close(manager_info->inotify_fd) == -1)
        perror_exit("close inotify");
    sims_free(manager_info->sync_info_mem_store);
    opq_free(manager_info->operation_queue);
    wl_free(manager_info->worker_list);

    strcpy(message, "Manager shutdown complete.\n");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->fss_out);
    log_end_message(manager_info->fss_out);

    fss_in_close(manager_info);
    fss_out_close(manager_info);
    cmd_free(manager_info->command);
    if (close(manager_info->logfile_fd) == -1)
        perror_exit("close logfile");
    free(manager_info);
}

void read_arguments(int argc, char *argv[], ManagerInfo *manager_info)
{
    if (argc == 5 && !strcmp(argv[1], "-l") && !strcmp(argv[3], "-c")) // no worker limit given
    {
        strcpy(manager_info->logfile_path, argv[2]);
        strcpy(manager_info->config_file_path, argv[4]);
        manager_info->worker_limit = DEFAULT_WORKER_LIMIT;
    }
    else if (argc == 7 && !strcmp(argv[1], "-l") && !strcmp(argv[3], "-c") && !strcmp(argv[5], "-n")) // worker limit given
    {
        strcpy(manager_info->logfile_path, argv[2]);
        strcpy(manager_info->config_file_path, argv[4]);
        int worker_limit = atoi(argv[6]);
        if (worker_limit < 1)
            manager_info->worker_limit = DEFAULT_WORKER_LIMIT;
        else
            manager_info->worker_limit = worker_limit;
    }
    else
    {
        fprintf(stderr, "Usage: %s -l <manager_logfile> -c <manager_info->config_file> [-n <worker_limit>]\n", argv[0]);
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
    while (fscanf(manager_info->config_file, "%s %s", sync_info.source_dir, sync_info.target_dir) == 2) // read config entries
    {
        fss_add(manager_info, sync_info);
    }

    if (ferror(manager_info->config_file))
        perror_exit("fscanf manager config");

    if (fclose(manager_info->config_file) == EOF)
        perror_exit("fclose manager config");
}

void fss_add(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];
    if (sims_exists(manager_info->sync_info_mem_store, sync_info)) // directory already monitored
    {
        /* invalid result to stdout only */
        snprintf(message, sizeof(message), "Already in queue: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->fss_out);
        log_end_message(manager_info->fss_out);
        return;
    }

    if (!dir_exists(sync_info.source_dir))
        return;
    if (!dir_exists(sync_info.target_dir))
        return;

    /* Set sync info attributes */
    sync_info.error_num = 0;
    sync_info.last_sync = -1;
    sync_info.status = 1;

    /* Add directory watch */
    if ((sync_info.inotify_wd = inotify_add_watch(manager_info->inotify_fd, sync_info.source_dir, IN_CREATE | IN_DELETE | IN_MODIFY)) == -1)
        perror_exit("inotify_add_watch");

    sims_add(manager_info->sync_info_mem_store, sync_info); // add to sync_info_mem_store

    OperationInfo operation_info = {sync_info, "ALL", "FULL"};
    opq_add(manager_info->operation_queue, operation_info);

    /* log to stdout, logfile and fss_out */
    snprintf(message, sizeof(message), "Added directory: %.*s -> %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.target_dir), sync_info.target_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    if (!sync_info.from_config)
        log_timed_fd(message, manager_info->fss_out); // only send to console if not added from config

    snprintf(message, sizeof(message), "Monitoring started for %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    if (!sync_info.from_config)
    {
        log_timed_fd(message, manager_info->fss_out);
        log_end_message(manager_info->fss_out);
    }
}

void fss_sync(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];

    if (sims_find(manager_info->sync_info_mem_store, sync_info.source_dir, &sync_info) == -1)
    {
        log_end_message(manager_info->fss_out);
        return; // directory not monitored
    }

    if (opq_exists(manager_info->operation_queue, sync_info.source_dir) == 1)
    {
        snprintf(message, sizeof(message), "Sync already in progress: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->fss_out);
        log_end_message(manager_info->fss_out);
        return; // directory already in queue
    }

    /* log sync start */
    snprintf(message, sizeof(message), "Syncing directory: %.*s -> %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.target_dir), sync_info.target_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    log_timed_fd(message, manager_info->fss_out);

    OperationInfo operation_info = {sync_info, "ALL", "FULL"};
    opq_add(manager_info->operation_queue, operation_info); // sync directory

    /* log sync complete */
    snprintf(message, sizeof(message), "Sync complete: %.*s -> %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.target_dir), sync_info.target_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    log_timed_fd(message, manager_info->fss_out);
    log_end_message(manager_info->fss_out);
}

void fss_status(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];
    if (sims_find(manager_info->sync_info_mem_store, sync_info.source_dir, &sync_info) == -1)
    {
        /* invalid result to stdout only */
        snprintf(message, sizeof(message), "Directory not monitored: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->fss_out);
        log_end_message(manager_info->fss_out);
        return;
    }

    /* log status */

    char time_buf[BUF_SIZE];
    struct tm tm = *localtime((const time_t *)&sync_info.last_sync);
    strftime(time_buf, TIME_FORMAT_SIZE, "%Y-%m-%d %H:%M:%S", &tm); // set to correct format

    snprintf(message, sizeof(message), "Status requested for %.*s\nDirectory: %.*s\nTarget: %.*s\nLast Sync: %.*s\nErrors: %d\nStatus: %s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.source_dir), sync_info.source_dir,
             (int)strlen(sync_info.target_dir), sync_info.target_dir,
             (int)strlen(time_buf), time_buf,
             sync_info.error_num,
             (sync_info.status == 1) ? "Active" : "Inactive");
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->fss_out);
    log_end_message(manager_info->fss_out);
}

void fss_cancel(ManagerInfo *manager_info, SyncInfo sync_info)
{
    char message[BUF_SIZE];
    if (sims_find(manager_info->sync_info_mem_store, sync_info.source_dir, &sync_info) == -1)
    {
        /* invalid result to stdout only */
        snprintf(message, sizeof(message), "Directory not monitored: %.*s\n",
                 (int)strlen(sync_info.source_dir), sync_info.source_dir);
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->fss_out);
        log_end_message(manager_info->fss_out);
        return;
    }

    sync_info.status = 0;
    sims_remove(manager_info->sync_info_mem_store, sync_info.source_dir);
    if (inotify_rm_watch(manager_info->inotify_fd, sync_info.inotify_wd) == -1)
        perror_exit("inotify_rm_watch cancel");

    /* log to stdout, logfile and fss_out */
    snprintf(message, sizeof(message), "Monitoring stopped for %.*s\n",
             (int)strlen(sync_info.source_dir), sync_info.source_dir);
    log_timed_stdout(message);
    log_timed_fd(message, manager_info->logfile_fd);
    log_timed_fd(message, manager_info->fss_out);
    log_end_message(manager_info->fss_out);
}

void execute_operation(ManagerInfo *manager_info, OperationInfo operation_info)
{
    pid_t pid;
    int status;

    WorkerInfo worker;

    if (pipe(worker.pipe_fd) == -1) // worker to manager pipe
        perror_exit("worker pipe");

    if ((worker.pid = fork()) < 0) // spawn worker
        perror_exit("fork worker");

    /* Start worker */
    if (worker.pid == 0)
    {
        close(worker.pipe_fd[READ]); // worker closes read end
        dup2(worker.pipe_fd[WRITE], 1);
        close(worker.pipe_fd[WRITE]);

        int retval = 0;
        retval = execl(WORKER_PATH, WORKER_PATH,
                       operation_info.sync_info.source_dir,
                       operation_info.sync_info.target_dir,
                       operation_info.file_name,
                       operation_info.operation, NULL);
        if (retval == -1)
            perror_exit("exec worker");
    }

    close(worker.pipe_fd[WRITE]); // manager closes write end
    wl_add(manager_info->worker_list, worker);
    manager_info->active_workers++;
}

void check_changes(ManagerInfo *manager_info)
{
    char buffer[BUF_SIZE];
    int length = read(manager_info->inotify_fd, buffer, BUF_SIZE);
    if (length == -1 && errno == EAGAIN)
        return;
    else if (length < 0)
    {
        perror("read");
        return;
    }

    SyncInfoMemStore temp_store = sims_init(); // temp store to traverse entries
    SyncInfo temp_sync_info;
    sims_copy(manager_info->sync_info_mem_store, temp_store);
    int i = 0;
    while (i < length)
    {
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        while (sims_pop(temp_store, &temp_sync_info) == 0) // traverse each sync_info entry
            apply_changes(manager_info, event, temp_sync_info);
        i += sizeof(struct inotify_event) + event->len; // check next event
    }
    free(temp_store);
}

void apply_changes(ManagerInfo *manager_info, struct inotify_event *event, SyncInfo sync_info)
{
    if (event->wd == sync_info.inotify_wd)
    {
        OperationInfo operation_info;
        operation_info.sync_info = sync_info;
        strcpy(operation_info.file_name, event->name);
        if (event->mask & IN_CREATE)
            strcpy(operation_info.operation, "ADDED");
        if (event->mask & IN_DELETE)
            strcpy(operation_info.operation, "DELETED");
        if (event->mask & IN_MODIFY)
            strcpy(operation_info.operation, "MODIFIED");
        opq_add(manager_info->operation_queue, operation_info);
    }
}

int execute_next_operation(ManagerInfo *manager_info)
{
    if (manager_info->active_workers >= manager_info->worker_limit)
        return 1;

    /* Pop from queue and execute syncing operation */
    OperationInfo next_operation;
    if (opq_pop(manager_info->operation_queue, &next_operation) == -1)
        return -1; // queue is empty
    execute_operation(manager_info, next_operation);
    return 0;
}

void execute_queued_operations(ManagerInfo *manager_info)
{
    OperationInfo next_operation;
    while (!opq_is_empty(manager_info->operation_queue))
    {
        opq_pop(manager_info->operation_queue, &next_operation);
        execute_operation(manager_info, next_operation);
    }
}

void sigaction_init(ManagerInfo *manager_info)
{
    manager_info->sa.sa_handler = sigchld_handler;
    sigemptyset(&(manager_info->sa.sa_mask));
    manager_info->sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_SIGINFO;

    if (sigaction(SIGCHLD, &(manager_info->sa), NULL) == -1)
        perror_exit("sigaction");
}

void sigchld_handler(int sig)
{
    got_sigchld = 1;
}

void check_commands(ManagerInfo *manager_info)
{
    manager_info->command_string[0] = '\0'; // reset previous command
    fss_in_read(manager_info);

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

void fss_in_init(ManagerInfo *manager_info)
{
    if (mkfifo(FSS_IN_PATH, 0666) == -1)
        if (errno != EEXIST)
            perror_exit("mkfifo fss_in");

    if ((manager_info->fss_in = open(FSS_IN_PATH, O_RDONLY | O_NONBLOCK)) < 0)
        perror_exit("open fss_in");
}

void fss_in_read(ManagerInfo *manager_info)
{
    ssize_t bytes_read = read(manager_info->fss_in, manager_info->command_string, sizeof(manager_info->command_string) - 1);
    if (bytes_read == -1)
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return; // no data available
        }
        else
        {
            perror_exit("read fss_in");
        }

    manager_info->command_string[bytes_read] = '\0';
}

void fss_in_close(ManagerInfo *manager_info)
{
    if (close(manager_info->fss_in) == -1)
        perror_exit("close fss_in");
}

void execute_command(ManagerInfo *manager_info)
{
    SyncInfo sync_info;
    sync_info.from_config = 0; // not added from config
    char message[BUF_SIZE];
    switch (manager_info->command.type)
    {
    case ADD:
        strcpy(sync_info.source_dir, manager_info->command.arguments[1]);
        strcpy(sync_info.target_dir, manager_info->command.arguments[2]);
        fss_add(manager_info, sync_info);
        break;
    case STATUS:
        strcpy(sync_info.source_dir, manager_info->command.arguments[1]);
        fss_status(manager_info, sync_info);
        break;
    case CANCEL:
        strcpy(sync_info.source_dir, manager_info->command.arguments[1]);
        fss_cancel(manager_info, sync_info);
        break;
    case SYNC:
        strcpy(sync_info.source_dir, manager_info->command.arguments[1]);
        fss_sync(manager_info, sync_info);
        break;
    case SHUTDOWN:
        strcpy(message, "Shutting down manager...\n");
        log_timed_stdout(message);
        log_timed_fd(message, manager_info->fss_out);
        manager_info->shutdown = 1;
        break;
    default:
        break;
    }
}

void fss_out_init(ManagerInfo *manager_info)
{
    if (mkfifo(FSS_OUT_PATH, 0666) == -1)
        if (errno != EEXIST)
            perror_exit("mkfifo fss_out");

    if ((manager_info->fss_out = open(FSS_OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        perror_exit("open fss_out");
}

void fss_out_close(ManagerInfo *manager_info)
{
    if (close(manager_info->fss_out) == -1)
        perror_exit("close fss_out");
}

void collect_workers(ManagerInfo *manager_info)
{
    WorkerList temp_worker_list = wl_init(); // temp list to traverse workers
    wl_copy(manager_info->worker_list, temp_worker_list);

    WorkerInfo worker;
    while (wl_pop(temp_worker_list, &worker) == 0)
    {
        if (waitpid(worker.pid, NULL, WNOHANG) > 0)
        {
            ExecReport exec_report = {0};
            manager_info->active_workers--;
            if (read(worker.pipe_fd[READ], &exec_report, sizeof(ExecReport)) == -1)
                perror_exit("read exec report");

            char message[BUF_SIZE];
            snprintf(message, sizeof(message), "[%.*s] [%.*s] [%d] [%.*s] [%.*s] [%.*s]\n",
                     (int)strlen(exec_report.source_dir), exec_report.source_dir,
                     (int)strlen(exec_report.target_dir), exec_report.target_dir,
                     exec_report.worker_pid,
                     (int)strlen(exec_report.operation), exec_report.operation,
                     (int)strlen(exec_report.result), exec_report.result,
                     (int)strlen(exec_report.details), exec_report.details);
            log_timed_fd(message, manager_info->logfile_fd); // logged only in logfile

            update_sync_info(manager_info, exec_report.source_dir, exec_report); // update last sync timestamp and errors

            wl_remove(manager_info->worker_list, worker.pid); // remove from active workers
        }
    }
    got_sigchld = 0; // reset SIGCHLD handler
    wl_free(temp_worker_list);
}

void update_sync_info(ManagerInfo *manager_info, char *source_dir, ExecReport exec_report)
{
    SyncInfo sync_info;
    if (sims_find(manager_info->sync_info_mem_store, source_dir, &sync_info) == -1)
        return;

    sync_info.last_sync = time(NULL);
    sync_info.error_num += exec_report.error_num;

    sims_update(manager_info->sync_info_mem_store, sync_info.source_dir, sync_info);
}