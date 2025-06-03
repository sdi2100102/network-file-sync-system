#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "nfs_workers.h"
#include "exec_report.h"

#include "file_operation.h"

typedef struct
{
    OperationInfo *buffer;
    int size;
    int front;
    int rear;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool shutdown;
} OperationQueue;

static OperationQueue queue;
static pthread_t *worker_threads;
static int worker_count;

void perform_operation(OperationInfo op);
static void *worker_function(void *arg);
void place_operation(OperationInfo op);

void perform_operation(OperationInfo op)
{
    file_operation(op.sync_info.source_dir, op.sync_info.target_dir, op.file_name, op.operation); // todo replace with client communication
}

static void *worker_function(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&queue.mutex);

        while (queue.count == 0 && !queue.shutdown)
        {
            pthread_cond_wait(&queue.not_empty, &queue.mutex);
        }

        if (queue.count == 0 && queue.shutdown)
        {
            pthread_mutex_unlock(&queue.mutex);
            break;
        }

        OperationInfo op = queue.buffer[queue.front];
        queue.front = (queue.front + 1) % queue.size;
        queue.count--;

        pthread_cond_signal(&queue.not_full);
        pthread_mutex_unlock(&queue.mutex);

        perform_operation(op);
    }

    return NULL;
}

void workers_init(int num_workers, int queue_size)
{
    worker_count = num_workers;
    queue.size = queue_size;
    queue.buffer = malloc(sizeof(OperationInfo) * queue_size);
    queue.front = 0;
    queue.rear = 0;
    queue.count = 0;
    queue.shutdown = false;

    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.not_empty, NULL);
    pthread_cond_init(&queue.not_full, NULL);

    worker_threads = malloc(sizeof(pthread_t) * num_workers);
    for (int i = 0; i < num_workers; i++)
    {
        pthread_create(&worker_threads[i], NULL, worker_function, NULL);
    }
}

void place_operation(OperationInfo op)
{
    pthread_mutex_lock(&queue.mutex);

    while (queue.count == queue.size)
    {
        pthread_cond_wait(&queue.not_full, &queue.mutex);
    }

    queue.buffer[queue.rear] = op;
    queue.rear = (queue.rear + 1) % queue.size;
    queue.count++;

    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);
}

void workers_close()
{
    pthread_mutex_lock(&queue.mutex);
    queue.shutdown = true;
    pthread_cond_broadcast(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);

    for (int i = 0; i < worker_count; i++)
    {
        pthread_join(worker_threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.not_empty);
    pthread_cond_destroy(&queue.not_full);
    free(queue.buffer);
    free(worker_threads);
}

void remove_operations_by_source_dir(const char *source_dir)
{
    pthread_mutex_lock(&queue.mutex);

    int new_count = 0;
    int i = queue.front;
    OperationInfo *new_buffer = malloc(sizeof(OperationInfo) * queue.size);

    for (int processed = 0; processed < queue.count; processed++)
    {
        OperationInfo *op = &queue.buffer[i];
        if (strcmp(op->sync_info.source_dir, source_dir) != 0)
        {
            new_buffer[new_count++] = *op;
        }
        i = (i + 1) % queue.size;
    }

    // Replace old buffer content with new one
    memcpy(queue.buffer, new_buffer, sizeof(OperationInfo) * new_count);
    free(new_buffer);

    queue.front = 0;
    queue.rear = new_count % queue.size;
    queue.count = new_count;

    pthread_cond_broadcast(&queue.not_full); // signal in case we were full
    pthread_mutex_unlock(&queue.mutex);
}
