#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "nfs_workers.h"
#include "exec_report.h"
#include "utils.h"

#include "debug.h"

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

static void *worker_function(void *arg);
void place_operation(OperationInfo op);
void perform_operation(OperationInfo op);
int send_pull_command(ExecReport *report, OperationInfo op, int source_sock);
int read_file_size(ExecReport *report, int source_sock);
int recieve_ack(ExecReport *report, int manager_socket);
int send_push_command(ExecReport *report, OperationInfo op, int target_sock);
void send_file_size(int manager_socket, int file_size);
void worker_error(ExecReport *exec_report, char *error_message);
int worker_to_client_socket_init(char *server_ip, int server_port);
int worker_send_string(int socket, const char *string);

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

void perform_operation(OperationInfo op)
{
    /* Initialize exec reports */
    ExecReport pull_report = initialize_exec_report(op, pthread_self(), "PULL");
    ExecReport push_report = initialize_exec_report(op, pthread_self(), "PUSH");

    /* Open sockets */
    int source_sock = worker_to_client_socket_init(op.sync_info.source_ip, op.sync_info.source_port);
    if (source_sock < 0)
    {
        worker_error(&pull_report, "Failed to connect to source");
        return;
    }
    int target_sock = worker_to_client_socket_init(op.sync_info.target_ip, op.sync_info.target_port);
    if (target_sock < 0)
    {
        worker_error(&push_report, "Failed to connect to target");
        return;
    }

    /* Write to source: pull <path> */
    if (send_pull_command(&pull_report, op, source_sock) < 0)
        return;

    DEBUG_PRINT("5. pull command sent | thread id: %ld | sock fd %d", pthread_self(), source_sock);

    /* Read from source: <chunk_size> */
    int file_size = read_file_size(&pull_report, source_sock);
    if (file_size < 0)
        return;

    DEBUG_PRINT("6. file size from source: %d | thread id: %ld | sock fd %d", file_size, pthread_self(), source_sock);

    /* Write to source: ACK */
    if (worker_send_string(source_sock, "ACK") < 0)
    {
        worker_error(&pull_report, "Failed to send ACK to source");
        return;
    }

    DEBUG_PRINT("7. ack sent to source | thread id: %ld | sock fd %d", pthread_self(), source_sock);

    /* Write to target: push <path> */
    if (send_push_command(&push_report, op, target_sock) < 0)
        return;

    DEBUG_PRINT("8. push command sent:\n");

    /* Read from target: ACK */
    if (recieve_ack(&push_report, target_sock) < 0)
        return;

    DEBUG_PRINT("9. ack recieved from target | thread id: %ld | sock fd %d", pthread_self(), target_sock);

    /* Write to target: <file_size> */
    send_file_size(target_sock, file_size);

    DEBUG_PRINT("10. file size sent: %d\n", file_size);

    /* Read from target: ACK */
    if (recieve_ack(&push_report, target_sock) < 0)
        return;

    DEBUG_PRINT("11. ack recieved from target | thread id: %ld | sock fd %d", pthread_self(), target_sock);

    /* Transfer data from source to target */
    int bytes_to_read = file_size;
    char buf[BUF_SIZE];
    while (bytes_to_read > 0)
    {
        int bytes_read = read(source_sock, buf, bytes_to_read);
        if (bytes_read == -1)
        {
            worker_error(&pull_report, "Failed to read from source");
            return;
        }

        if (write(target_sock, buf, bytes_read) == -1)
        {
            worker_error(&push_report, "Failed to write to target");
            return;
        }
        bytes_to_read -= bytes_read;

        DEBUG_PRINT("12. bytes read: %d | bytes to read: %d | thread id: %ld | sock fd %d", bytes_read, bytes_to_read, pthread_self(), target_sock);
    }

    /* Close sockets */
    if (close(target_sock) == -1)
    {
        worker_error(&push_report, "Failed to close target socket");
        return;
    }
    if (close(source_sock) == -1)
    {
        worker_error(&pull_report, "Failed to close source socket");
        return;
    }

    /* Finish exec reports */
    complete_exec_report_success(&pull_report, file_size);
    complete_exec_report_success(&push_report, file_size);
}

int send_pull_command(ExecReport *report, OperationInfo op, int source_sock)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "pull %.*s/%.*s",
             (int)strlen(op.sync_info.source_dir), op.sync_info.source_dir,
             (int)strlen(op.file_name), op.file_name);
    if (worker_send_string(source_sock, buf) < 0)
    {
        worker_error(report, "Failed to send pull command");
        return -1;
    }
    return 0;
}

int read_file_size(ExecReport *report, int source_sock)
{
    char buf[BUF_SIZE];
    int bytes_read = read(source_sock, buf, sizeof(buf) - 1);
    if (bytes_read == -1)
    {
        worker_error(report, "Failed to read file size");
        return -1;
    }
    buf[bytes_read] = '\0';
    return atoi(buf);
}

int recieve_ack(ExecReport *report, int manager_socket)
{
    char buf[BUF_SIZE];
    int bytes_read;
    if ((bytes_read = read(manager_socket, buf, strlen("ACK"))) == -1)
    {
        worker_error(report, "Failed to read ACK");
        return -1;
    }
    buf[bytes_read] = '\0';
    if (strcmp(buf, "ACK") != 0)
    {
        worker_error(report, "ACK not received");
        return -1;
    }
}

int send_push_command(ExecReport *report, OperationInfo op, int target_sock)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "push %.*s/%.*s ",
             (int)strlen(op.sync_info.target_dir), op.sync_info.target_dir,
             (int)strlen(op.file_name), op.file_name);
    if (worker_send_string(target_sock, buf) < 0)
    {
        worker_error(report, "Failed to send push command");
        return -1;
    }
    return 0;
}

void send_file_size(int manager_socket, int file_size)
{
    char buf[BUF_SIZE];
    snprintf(buf, BUF_SIZE, "%d ", file_size);
    client_socket_send(manager_socket, buf);
}

int worker_to_client_socket_init(char *server_ip, int server_port)
{
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
        return -1;

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        return -1;

    return sockfd;
}

void worker_error(ExecReport *exec_report, char *error_message)
{
    /* Debug message */
    char message[BUF_SIZE];
    strcat(message, error_message);
    strcat(message, ": ");
    strcat(message, strerror(errno));
    DEBUG_PRINT("Error in worker thread %ld: %s", pthread_self(), message);

    /* Log message */
    complete_exec_report_failure(exec_report, strerror(errno));
    return;
}

int worker_send_string(int sock, const char *string)
{
    ssize_t bytes_sent = write(sock, string, strlen(string));
    if (bytes_sent == -1)
    {
        return -1;
    }
    return 0;
}
