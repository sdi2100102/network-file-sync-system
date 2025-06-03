#ifndef NFS_WORKERS_H
#define NFS_WORKERS_H

#include "sync_info_mem_store.h"

#define BUF_SIZE 1024

typedef struct
{
    SyncInfo sync_info;
    char file_name[BUF_SIZE];
    char operation[BUF_SIZE];
} OperationInfo;

void workers_init(int num_workers, int queue_size);
void workers_close();
void place_operation(OperationInfo operation);
void remove_operations_by_source_dir(const char *source_dir);

#endif // NFS_WORKERS_H