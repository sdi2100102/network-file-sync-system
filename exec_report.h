#ifndef EXEC_REPORT_H
#define EXEC_REPORT_H

#include <time.h>

#define REPORT_FIELD_SIZE 128

typedef struct
{
    pid_t worker_pid;
    char source_dir[REPORT_FIELD_SIZE];
    char target_dir[REPORT_FIELD_SIZE];
    char file_name[REPORT_FIELD_SIZE];
    char operation[REPORT_FIELD_SIZE];
    char result[REPORT_FIELD_SIZE];
    char details[REPORT_FIELD_SIZE];
    int copied, skipped;
    int error_num;
} ExecReport;

void complete_exec_report();
void initialize_exec_report(char *source_path, char *target_path, char *file_name, char *operation);
void copy_success(char *);
void file_error(char *);
void dir_error(char *);

#endif // EXEC_REPORT_H

