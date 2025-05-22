#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "exec_report.h"

#define BUF_SIZE 1024

typedef enum
{
    FULL,
    ADDED,
    MODIFIED,
    DELETED
} Operation;

void sync_files(char *, char *);
void sync_dir_full(char *, char *);
void clean_dir(char *);
void delete_file(char *, char *);
Operation get_operation(char *);

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s <source_dir> <target_dir> <file_name> <operation>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *source_path = argv[1];
    char *target_path = argv[2];
    char *file_name = argv[3];
    Operation operation = get_operation(argv[4]);

    initialize_exec_report(source_path, target_path, file_name, argv[4]);
    switch (operation)
    {
    case FULL:
    case ADDED:
    case MODIFIED:
        if (!strcmp(file_name, "ALL"))
            sync_dir_full(source_path, target_path);
        else
        {
            char source_full_path[REPORT_FIELD_SIZE], target_full_path[REPORT_FIELD_SIZE];
            snprintf(source_full_path, sizeof(source_full_path), "%.*s/%.*s", (int)strlen(source_path), source_path, (int)strlen(file_name), file_name);
            snprintf(target_full_path, sizeof(target_full_path), "%.*s/%.*s", (int)strlen(target_path), target_path, (int)strlen(file_name), file_name);
            sync_files(source_full_path, target_full_path);
        }
        break;
    case DELETED:
        delete_file(target_path, file_name);
        copy_success(target_path);
        break;
    default:
        fprintf(stderr, "Invalid operation argument\n");
        exit(EXIT_FAILURE);
    }
    complete_exec_report();
    exit(EXIT_SUCCESS);
}

void sync_dir_full(char *source_path, char *target_path)
{
    /* Open source file */
    DIR *source_dir = opendir(source_path);
    if (source_dir == NULL)
    {
        dir_error(source_path);
        complete_exec_report();
        exit(EXIT_FAILURE);
    }

    /* Remove files from target dir */
    clean_dir(target_path);

    /* Add source files to target dir */
    struct dirent *file;
    while ((file = readdir(source_dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        char source_full_path[REPORT_FIELD_SIZE], target_full_path[REPORT_FIELD_SIZE];
        snprintf(source_full_path, sizeof(source_full_path), "%.*s/%.*s", (int)strlen(source_path), source_path, (int)strlen(file->d_name), file->d_name);
        snprintf(target_full_path, sizeof(target_full_path), "%.*s/%.*s", (int)strlen(target_path), target_path, (int)strlen(file->d_name), file->d_name);

        sync_files(source_full_path, target_full_path);
    }

    if (closedir(source_dir) == -1)
    {
        dir_error(source_path);
        complete_exec_report();
        exit(EXIT_FAILURE);
    }
}

void clean_dir(char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        dir_error(path);
        complete_exec_report();
        exit(EXIT_FAILURE);
    }

    struct dirent *file;
    while ((file = readdir(dir)) != NULL)
    {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;

        delete_file(path, file->d_name);
    }

    if (closedir(dir) == -1)
    {
        dir_error(path);
        complete_exec_report();
    }
}

void sync_files(char *source_path, char *target_path)
{
    /* Open source file */
    int source_fd;
    if ((source_fd = open(source_path, O_RDONLY)) == -1)
    {
        file_error(source_path);
        return;
    }

    /* Open target file */
    int target_fd;
    if ((target_fd = open(target_path, O_WRONLY | O_TRUNC | O_CREAT, 0777)) == -1)
    {
        file_error(target_path);
        return;
    }

    /* Sync source to target */
    char buf[BUF_SIZE];
    int bytes_read;
    while ((bytes_read = read(source_fd, buf, BUF_SIZE)) > 0)
    {
        if (write(target_fd, buf, bytes_read) == -1)
        {
            file_error(target_path);
            return;
        }
    }
    if (bytes_read == -1)
    {
        file_error(source_path);
        return;
    }

    /* Closing files */
    if (close(source_fd) == -1)
    {
        file_error(source_path);
        return;
    }
    if (close(target_fd) == -1)
    {
        file_error(target_path);
        return;
    }

    copy_success(source_path);
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

void delete_file(char *target_path, char *file_name)
{
    char target_full_path[REPORT_FIELD_SIZE];
    snprintf(target_full_path, sizeof(target_full_path), "%s/%s", target_path, file_name);

    if (unlink(target_full_path) == -1)
    {
        file_error(target_full_path);
        return;
    }
}
