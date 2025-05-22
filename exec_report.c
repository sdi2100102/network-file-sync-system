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

ExecReport exec_report;

void get_file_name(char *path, char *file_name)
{
    char *base_name = strrchr(path, '/');
    if (base_name)
        strcpy(file_name, base_name + 1);
    else
        strcpy(file_name, path);
}

void dir_error(char *dir)
{
    exec_report.copied = -1; // means directory error
    strcpy(exec_report.result, "FAILED");
    snprintf(exec_report.details, sizeof(exec_report.details), "Directory %s: %s\n", dir, strerror(errno));
    exec_report.error_num++;
}

void initialize_exec_report(char *source_path, char *target_path, char *file_name, char *operation)
{
    strcpy(exec_report.source_dir, source_path);
    strcpy(exec_report.target_dir, target_path);
    strcpy(exec_report.operation, operation);
    strcpy(exec_report.file_name, file_name);
    strcpy(exec_report.result, "SUCCESS");
    exec_report.copied = 0;
    exec_report.skipped = 0;
    exec_report.details[0] = '\0';
    exec_report.error_num = 0;
}

void file_error(char *path)
{
    strcpy(exec_report.result, "PARTIAL");
    exec_report.skipped++;
    char file_name[REPORT_FIELD_SIZE / 2];
    get_file_name(path, file_name);
    snprintf(exec_report.details, sizeof(exec_report.details), "File: %s - %s", file_name, strerror(errno));
    exec_report.error_num++;
}

void copy_success(char *path)
{
    exec_report.copied++;
    char file_name[REPORT_FIELD_SIZE / 2];
    get_file_name(path, file_name);
    snprintf(exec_report.details, sizeof(exec_report.details), "File: %s", file_name);
}

void complete_exec_report()
{
    if (strcmp(exec_report.operation, "FULL") == 0 && exec_report.copied != -1)
        if (exec_report.copied == 0)
            snprintf(exec_report.details, sizeof(exec_report.details), "No files copied. Files skipped: %d", exec_report.skipped);
        else if (exec_report.skipped == 0)
            snprintf(exec_report.details, sizeof(exec_report.details), "Files copied: %d", exec_report.copied);
        else
            snprintf(exec_report.details, sizeof(exec_report.details), "Files copied: %d. Files skipped: %d", exec_report.copied, exec_report.skipped);

    if (exec_report.copied == 0)
        strcpy(exec_report.result, "FAILED");

    exec_report.worker_pid = getpid();

    if (write(STDOUT_FILENO, &exec_report, sizeof(ExecReport)) != sizeof(ExecReport))
        perror_exit("write");
}
