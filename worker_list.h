#ifndef WORKER_LIST_H
#define WORKER_LIST_H

#define BUF_SIZE 1024

typedef struct ListTag *WorkerList;

typedef struct
{
    int pipe_fd[2];
    int pid;
} WorkerInfo;

WorkerList wl_init();
void wl_add(WorkerList worker_list, WorkerInfo worker_info);
void wl_print(WorkerList worker_list);
void wl_free(WorkerList worker_list);
int wl_exists(WorkerList worker_list, WorkerInfo worker_info);
int wl_pop(WorkerList worker_list, WorkerInfo *worker_info);
void wl_copy(WorkerList src, WorkerList dest);
int wl_find(WorkerList worker_list, int key, WorkerInfo* worker_info);
int wl_remove(WorkerList worker_list, int key);

#endif // WORKER_LIST_H


