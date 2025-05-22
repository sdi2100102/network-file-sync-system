#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "worker_list.h"

#define SUCCESS 0
#define FAILURE -1

typedef WorkerInfo Data;

typedef struct NodeTag
{
    Data data;
    struct NodeTag *next;
} Node;

typedef struct ListTag
{
    Node *head;
} List;

List *wl_init()
{
    List *list = (List *)malloc(sizeof(List));
    if (list == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    list->head = NULL;
    return list;
}

void wl_add(List *list, Data data)
{
    Node *newNode = (Node *)malloc(sizeof(Node));
    if (newNode == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;
    if (list->head == NULL)
    {
        list->head = newNode;
    }
    else
    {
        Node *current = list->head;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = newNode;
    }
}

void wl_print(List *list)
{
    Node *current = list->head;
    while (current != NULL)
    {
        printf("%d %d %d\n", current->data.pid, current->data.pipe_fd[0], current->data.pipe_fd[1]);
        current = current->next;
    }
}

void wl_free(List *list)
{
    Node *current = list->head;
    while (current != NULL)
    {
        Node *next = current->next;
        free(current);
        current = next;
    }
    free(list);
}

int wl_exists(List *list, Data data)
{
    Node *current = list->head;
    while (current != NULL)
    {
        if (current->data.pid == data.pid)
        {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

int wl_pop(List *list, Data *data)
{
    if (list->head == NULL)
    {
        return FAILURE; // list is empty
    }

    Node *temp = list->head;
    *data = temp->data;
    list->head = list->head->next;
    free(temp);

    return SUCCESS; // successful pop
}

void wl_copy(List *src, List *dest)
{
    Node *current = src->head;
    while (current != NULL)
    {
        wl_add(dest, current->data);
        current = current->next;
    }
}

int wl_find(List *list, int key, Data *data)
{
    Node *current = list->head;
    while (current != NULL)
    {
        if (current->data.pid == key)
        {
            *data = (*current).data;
            return SUCCESS;
        }
        current = current->next;
    }
    return FAILURE;
}

int wl_remove(List *list, int key)
{
    Node *current = list->head;
    Node *previous = NULL;
    while (current != NULL)
    {
        if (current->data.pid == key)
        {
            if (previous == NULL)
                list->head = current->next;
            else
                previous->next = current->next;
            free(current);
            return SUCCESS;
        }
        previous = current;
        current = current->next;
    }
    return FAILURE;
}
