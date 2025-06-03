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
#include "file_operation.h"

#define BUF_SIZE 1024

typedef enum
{
    FULL,
    ADDED,
    MODIFIED,
    DELETED
} Operation;

void sync_files(ExecReport *, char *, char *);
void sync_dir_full(ExecReport *, char *, char *);
void clean_dir(ExecReport *, char *);
void delete_file(ExecReport *, char *, char *);
Operation get_operation(char *);

void file_operation(char *source_path, char *target_path, char *file_name, char *operation_string)
{
    Operation operation = get_operation(operation_string);

    ExecReport exec_report = initialize_exec_report(source_path, target_path, file_name, operation_string);

    printf("sync: %s operation: %s -> %s: %s\n", operation_string, source_path, target_path, file_name); // todo remove 

    switch (operation)
    {
    case FULL:
    case ADDED:
    case MODIFIED:
        if (!strcmp(file_name, "ALL"))
            sync_dir_full(&exec_report, source_path, target_path);
        else
        { 
            char source_full_path[REPORT_FIELD_SIZE], target_full_path[REPORT_FIELD_SIZE];
            snprintf(source_full_path, sizeof(source_full_path), "%.*s/%.*s", (int)strlen(source_path), source_path, (int)strlen(file_name), file_name);
            snprintf(target_full_path, sizeof(target_full_path), "%.*s/%.*s", (int)strlen(target_path), target_path, (int)strlen(file_name), file_name);
            sync_files(&exec_report, source_full_path, target_full_path);
        }
        break;
    case DELETED:
        delete_file(&exec_report, target_path, file_name);
        copy_success(&exec_report, target_path);
        break;
    default:
        fprintf(stderr, "Invalid operation argument\n");
        exit(EXIT_FAILURE);
    }
    complete_exec_report(&exec_report);
}

void sync_dir_full(ExecReport *exec_report, char *source_path, char *target_path)
{
    /* Open source file */
    DIR *source_dir = opendir(source_path);
    if (source_dir == NULL)
    {
        dir_error(exec_report, source_path);
        complete_exec_report(exec_report);
        exit(EXIT_FAILURE);
    }

    /* Remove files from target dir */
    clean_dir(exec_report, target_path);

    /* Add source files to target dir */
    struct dirent *file;
    while ((file = readdir(source_dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        char source_full_path[REPORT_FIELD_SIZE], target_full_path[REPORT_FIELD_SIZE];
        snprintf(source_full_path, sizeof(source_full_path), "%.*s/%.*s", (int)strlen(source_path), source_path, (int)strlen(file->d_name), file->d_name);
        snprintf(target_full_path, sizeof(target_full_path), "%.*s/%.*s", (int)strlen(target_path), target_path, (int)strlen(file->d_name), file->d_name);

        sync_files(exec_report, source_full_path, target_full_path);
    }

    if (closedir(source_dir) == -1)
    {
        dir_error(exec_report, source_path);
        complete_exec_report(exec_report);
        exit(EXIT_FAILURE);
    }
}

void clean_dir(ExecReport *exec_report, char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        dir_error(exec_report, path);
        complete_exec_report(exec_report);
        exit(EXIT_FAILURE);
    }

    struct dirent *file;
    while ((file = readdir(dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        delete_file(exec_report, path, file->d_name);
    }

    if (closedir(dir) == -1)
    {
        dir_error(exec_report, path);
        complete_exec_report(exec_report);
    }
}

void sync_files(ExecReport *exec_report, char *source_path, char *target_path)
{ 
    /* Open source file */
    int source_fd;
    if ((source_fd = open(source_path, O_RDONLY)) == -1)
    {
        file_error(exec_report, source_path);
        return;
    }

    /* Open target file */
    int target_fd;
    if ((target_fd = open(target_path, O_WRONLY | O_TRUNC | O_CREAT, 0777)) == -1)
    {
        file_error(exec_report, target_path);
        return;
    }

    /* Sync source to target */
    char buf[BUF_SIZE];
    int bytes_read;
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
    {
        if (write(target_fd, buf, bytes_read) == -1)
        {
            file_error(exec_report, target_path);
            return;
        }
    }
    if (bytes_read == -1)
    {
        file_error(exec_report, source_path);
        return;
    }

    /* Closing files */
    if (close(source_fd) == -1)
    {
        file_error(exec_report, source_path);
        return;
    }
    if (close(target_fd) == -1)
    {
        file_error(exec_report, target_path);
        return;
    }

    copy_success(exec_report, source_path);
}

Operation get_operation(char *operation)
{
    if (strcmp(operation, "FULL") == 0)
        return FULL;
    else if (strcmp(operation, "ADDED") == 0)
        return ADDED;
    else if (strcmp(operation, "MODIFIED") == 0)
        return MODIFIED;
    else if (strcmp(operation, "DELETED") == 0)
        return DELETED;
    else
        return -1;
}

void delete_file(ExecReport *exec_report, char *target_path, char *file_name)
{
    char target_full_path[REPORT_FIELD_SIZE];
    snprintf(target_full_path, sizeof(target_full_path), "%s/%s", target_path, file_name);

    if (unlink(target_full_path) == -1)
    {
        file_error(exec_report, target_full_path);
        return;
    }
}
