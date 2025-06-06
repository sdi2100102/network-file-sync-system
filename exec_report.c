#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "exec_report.h"
#include "utils.h"

#include "nfs_log.h"

#define BUF_SIZE 1024

void log_exec_report(ExecReport *exec_report);

ExecReport initialize_exec_report(OperationInfo op, pthread_t thread_id, char *operation_type)
{
    ExecReport exec_report = {.thread_id = thread_id, .op = op};
    strcpy(exec_report.operation_type, operation_type);
    strcpy(exec_report.result, "SUCCESS");
    exec_report.details[0] = '\0';

    return exec_report;
}

void complete_exec_report_failure(ExecReport *exec_report, char *error_message)
{
    /* Create details string */
    strcpy(exec_report->result, "FAILED");
    snprintf(exec_report->details, sizeof(exec_report->details), "File: %.*s - %.*s",
             (int)strlen(exec_report->op.file_name), exec_report->op.file_name,
             (int)strlen(error_message), error_message);

    log_exec_report(exec_report);
}

void complete_exec_report_success(ExecReport *exec_report, int bytes_copied)
{

    /* Create details string */
    char details[BUF_SIZE];
    if (strcmp(exec_report->operation_type, "PULL") == 0)
    {
        snprintf(details, sizeof(details), "%d bytes pulled", bytes_copied);
    }
    else if (strcmp(exec_report->operation_type, "PUSH") == 0)
    {
        snprintf(details, sizeof(details), "%d bytes pushed", bytes_copied);
    }
    strcpy(exec_report->details, details);

    /* Log the report */
    log_exec_report(exec_report);
}

void log_exec_report(ExecReport *exec_report)
{

    /* Create source and target strings */
    char source[BUF_SIZE];
    snprintf(source, sizeof(source), "%.*s/%.*s@%.*s:%d",
             (int)strlen(exec_report->op.sync_info.source_dir), exec_report->op.sync_info.source_dir,
             (int)strlen(exec_report->op.file_name), exec_report->op.file_name,
             (int)strlen(exec_report->op.sync_info.source_ip), exec_report->op.sync_info.source_ip,
             exec_report->op.sync_info.source_port);
    char target[BUF_SIZE];
    snprintf(target, sizeof(target), "%.*s/%.*s@%.*s:%d",
             (int)strlen(exec_report->op.sync_info.target_dir), exec_report->op.sync_info.target_dir,
             (int)strlen(exec_report->op.file_name), exec_report->op.file_name,
             (int)strlen(exec_report->op.sync_info.target_ip), exec_report->op.sync_info.target_ip,
             exec_report->op.sync_info.target_port);

    /* Create final message */
    char message[BUF_SIZE];
    snprintf(message, sizeof(message), "[%.*s] [%.*s] [%ld] [%.*s] [%.*s] [%.*s]\n",
             (int)strlen(source), source,
             (int)strlen(target), target,
             exec_report->thread_id,
             (int)strlen(exec_report->operation_type), exec_report->operation_type,
             (int)strlen(exec_report->result), exec_report->result,
             (int)strlen(exec_report->details), exec_report->details);

    /* Log the message */
    log_timed_fd(message, exec_report->op.logfile_fd);
}