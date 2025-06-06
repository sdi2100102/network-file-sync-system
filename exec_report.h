#ifndef EXEC_REPORT_H
#define EXEC_REPORT_H

#include <time.h>
#include <pthread.h>

#include "nfs_workers.h"

#define REPORT_FIELD_SIZE 1024

typedef struct
{
    OperationInfo op;
    pthread_t thread_id;
    char operation_type[REPORT_FIELD_SIZE];

    char result[REPORT_FIELD_SIZE];
    char details[REPORT_FIELD_SIZE];
} ExecReport;

ExecReport initialize_exec_report(OperationInfo op, pthread_t thread_id, char *operation_type);
void complete_exec_report_success(ExecReport *exec_report, int bytes_copied);
void complete_exec_report_failure(ExecReport *exec_report, char *error_message);

#endif // EXEC_REPORT_H
