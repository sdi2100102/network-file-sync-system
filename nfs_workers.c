#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "nfs_workers.h"
#include "exec_report.h"

#include "debug.h"

#include "utils.h"
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
void pull_push(OperationInfo op);
void send_pull_command(OperationInfo op, int source_sock);
int read_file_size(int source_sock);
void recieve_ack(int manager_socket);
void send_push_command(OperationInfo op, int target_sock);
void send_file_size(int manager_socket, int file_size);

void perform_operation(OperationInfo op)
{
    pull_push(op);
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
    DEBUG_PRINT("4. placed operation for threads: %s, %s, %s", op.sync_info.target_dir, op.sync_info.source_dir, op.file_name);

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

void pull_push(OperationInfo op)
{
    /* Open sockets */
    int source_sock = client_socket_init(op.sync_info.source_ip, op.sync_info.source_port);
    int target_sock = client_socket_init(op.sync_info.target_ip, op.sync_info.target_port);

    /* Write to source: pull <path> */
    send_pull_command(op, source_sock);

    DEBUG_PRINT("5. pull command sent | thread id: %ld | sock fd %d", pthread_self(), source_sock);

    /* Read from source: <chunk_size> */
    int file_size = read_file_size(source_sock);

    DEBUG_PRINT("6. file size from source: %d | thread id: %ld | sock fd %d", file_size, pthread_self(), source_sock);

    /* Write to source: ACK */
    client_socket_send(source_sock, "ACK");

    DEBUG_PRINT("7. ack sent to source | thread id: %ld | sock fd %d", pthread_self(), source_sock);

    /* Write to target: push <path> */
    send_push_command(op, target_sock);

    DEBUG_PRINT("8. push command sent:\n");

    /* Read from target: ACK */
    recieve_ack(target_sock);

    DEBUG_PRINT("9. ack recieved from target | thread id: %ld | sock fd %d", pthread_self(), target_sock);

    /* Write to target: <file_size> */
    send_file_size(target_sock, file_size);

    DEBUG_PRINT("10. file size sent: %d\n", file_size);

    /* Read from target: ACK */
    recieve_ack(target_sock);

    DEBUG_PRINT("11. ack recieved from target | thread id: %ld | sock fd %d", pthread_self(), target_sock);

    /* Transfer data from source to target */
    int bytes_to_read = file_size;
    char buf[BUF_SIZE];
    while (bytes_to_read > 0)
    {
        int bytes_read = read(source_sock, buf, bytes_to_read);
        if (bytes_read == -1)
            perror_exit("read");
        client_socket_send(target_sock, buf);
        bytes_to_read -= bytes_read;

        DEBUG_PRINT("12. bytes read: %d | bytes to read: %d | thread id: %ld | sock fd %d", bytes_read, bytes_to_read, pthread_self(), target_sock);
    }

    /* Close sockets */
    if (close(target_sock) == -1)
        perror_exit("close");
    if (close(source_sock) == -1)
        perror_exit("close");
}

void pull_push_pull(OperationInfo op)
{
    // PULL TEST

    long int thread_id = pthread_self(); // todo remove

    /* Open socket */
    int source_sock = client_socket_init(op.sync_info.source_ip, op.sync_info.source_port);

    /* Send pull command */
    send_pull_command(op, source_sock);

    /* Read file size */
    int file_size = read_file_size(source_sock);

    DEBUG_PRINT("6. FILE SIZE: %d | thread id: %ld | sock fd %d", file_size, pthread_self(), source_sock); // todo remove

    /* Send ACK before reading data */
    client_socket_send(source_sock, "ACK");

    DEBUG_PRINT("7. ACK SENT | thread id: %ld | sock fd %d", pthread_self(), source_sock); // todo remove

    /* Read data */
    int bytes_to_read = file_size;
    char buf[BUF_SIZE];
    while (bytes_to_read > 0)
    {
        int bytes_read = read(source_sock, buf, sizeof(buf) - 1);
        buf[bytes_read] = '\0';
        bytes_to_read -= bytes_read;
        // client_socket_send(source_sock, buf);

        DEBUG_PRINT("8. DATA READ: %s | thread id: %ld | sock fd %d", buf, pthread_self(), source_sock); // todo remove
    }

    /* Close socket */
    if (close(source_sock) == -1)
        perror_exit("close");
}

void pull_push_push(OperationInfo op)
{
    // PUSH TEST

    char *test_string = "This is a test string";

    /* Open socket */
    int target_sock = client_socket_init(op.sync_info.target_ip, op.sync_info.target_port);

    /* Send push command */
    send_push_command(op, target_sock);

    /* Read ack */
    recieve_ack(target_sock);

    DEBUG_PRINT("6. ACK RECEIVED | thread id: %ld | sock fd %d", pthread_self(), target_sock); // todo remove

    /* Send file size */
    send_file_size(target_sock, strlen(test_string)); // todo replace with file size

    /* Read ack */
    recieve_ack(target_sock);

    /* Send data */
    for (int i = 0; i < 10; i++)
    {
        client_socket_send(target_sock, test_string);
        DEBUG_PRINT("8. SENT: %s", test_string); // todo remove
    }

    /* Close socket */
    if (close(target_sock) == -1)
        perror_exit("close");
}

void send_pull_command(OperationInfo op, int source_sock)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "pull %.*s/%.*s",
             (int)strlen(op.sync_info.source_dir), op.sync_info.source_dir,
             (int)strlen(op.file_name), op.file_name);
    client_socket_send(source_sock, buf);
}

int read_file_size(int source_sock)
{
    char buf[BUF_SIZE];
    int bytes_read = read(source_sock, buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';
    return atoi(buf);
}

void recieve_ack(int manager_socket)
{
    char buf[BUF_SIZE];
    int bytes_read;
    if ((bytes_read = read(manager_socket, buf, strlen("ACK"))) == -1)
        perror_exit("read socket");
    buf[bytes_read] = '\0';
    if (strcmp(buf, "ACK") != 0)
    {
        printf("Instead of ack, Recieved (in %d bytes): %s", bytes_read, buf); // todo remove
        perror_exit("ACK not received");
    }
}

void send_push_command(OperationInfo op, int target_sock)
{

    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "push %.*s/%.*s ",
             (int)strlen(op.sync_info.target_dir), op.sync_info.target_dir,
             (int)strlen(op.file_name), op.file_name);
    client_socket_send(target_sock, buf);
}

void send_file_size(int manager_socket, int file_size)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "%d ", file_size);
    client_socket_send(manager_socket, buf);
}