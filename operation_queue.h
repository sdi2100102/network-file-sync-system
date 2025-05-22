#ifndef OPERATION_QUEUE_H
#define OPERATION_QUEUE_H

#define BUF_SIZE 1024

#include "sync_info_mem_store.h"

typedef struct QueueTag *OperationQueue;

typedef struct
{
    SyncInfo sync_info;
    char file_name[BUF_SIZE];
    char operation[BUF_SIZE];
} OperationInfo;

OperationQueue opq_init();
void opq_add(OperationQueue operation_queue, OperationInfo operation_info);
void opq_print(OperationQueue operation_queue);
void opq_free(OperationQueue operation_queue);
int opq_exists(OperationQueue operation_queue, char* key);
int opq_pop(OperationQueue operation_queue, OperationInfo *operation_info);
void opq_copy(OperationQueue src, OperationQueue dest);
int opq_is_empty(OperationQueue operation_queue);

#endif // OPERATION_QUEUE_H