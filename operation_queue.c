#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operation_queue.h"

#define SUCCESS 0
#define FAILURE -1

typedef OperationInfo Data;

typedef struct NodeTag
{
    Data data;
    struct NodeTag *next;
} Node;

typedef struct QueueTag
{
    Node *head;
    Node *tail;
} Queue;

Queue *opq_init()
{
    Queue *queue = (Queue *)malloc(sizeof(Queue));
    if (queue == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    queue->head = NULL;
    return queue;
}

void opq_add(Queue *queue, Data data)
{
    Node *newNode = (Node *)malloc(sizeof(Node));
    if (newNode == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;

    if (queue->head == NULL)
    {
        queue->head = newNode;
        queue->tail = newNode;
    }
    else
    {
        queue->tail->next = newNode;
        queue->tail = newNode;
    }
}

void opq_print(Queue *queue)
{
    Node *current = queue->head;
    while (current != NULL)
    {
        printf("%s %s %d %s %s\n",
               current->data.sync_info.source_dir,
               current->data.sync_info.target_dir,
               current->data.sync_info.inotify_wd,
               current->data.file_name,
               current->data.operation);
        current = current->next;
    }
}

void opq_free(Queue *queue)
{
    Node *current = queue->head;
    while (current != NULL)
    {
        Node *next = current->next;
        free(current);
        current = next;
    }
    free(queue);
}

int opq_exists(Queue *queue, char* key)
{
    Node *current = queue->head;
    while (current != NULL)
    {
        if (!strcmp(current->data.sync_info.source_dir, key)) // source dir used as key
        {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

int opq_pop(Queue *queue, Data *data)
{
    if (queue->head == NULL)
    {
        return FAILURE; // queue is empty
    }

    Node *temp = queue->head;
    *data = temp->data;
    queue->head = queue->head->next;

    if (queue->head == NULL)
    {
        queue->tail = NULL; // queue is now empty
    }

    free(temp);
    return SUCCESS; // successful pop
}

void opq_copy(Queue *src, Queue *dest)
{
    Node *current = src->head;
    while (current != NULL)
    {
        opq_add(dest, current->data);
        current = current->next;
    }
}

int opq_is_empty(OperationQueue operation_queue)
{
    return operation_queue->head == NULL;
}
