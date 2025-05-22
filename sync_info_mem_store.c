#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sync_info_mem_store.h"

#define SUCCESS 0
#define FAILURE -1

typedef SyncInfo Data;

typedef struct NodeTag
{
    Data data;
    struct NodeTag *next;
} Node;

typedef struct ListTag
{
    Node *head;
} List;

List *sims_init()
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

void sims_add(List *list, Data data)
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

void sims_print(List *list)
{
    Node *current = list->head;
    while (current != NULL)
    {
        printf("%s %s %d\n", current->data.source_dir, current->data.target_dir, current->data.inotify_wd);
        current = current->next;
    }
}

void sims_free(List *list)
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

int sims_exists(List *list, Data data)
{
    Node *current = list->head;
    while (current != NULL)
    {
        if (!strcmp(current->data.source_dir, data.source_dir))
        {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

int sims_pop(List *list, Data *data)
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

void sims_copy(List *src, List *dest)
{
    Node *current = src->head;
    while (current != NULL)
    {
        sims_add(dest, current->data);
        current = current->next;
    }
}

int sims_find(List *list, char *key, Data *data)
{
    Node *current = list->head;
    while (current != NULL)
    {
        if (!strcmp(current->data.source_dir, key))
        {
            *data = (*current).data;
            return SUCCESS;
        }
        current = current->next;
    }
    return FAILURE;
}

int sims_remove(List *list, char *key)
{
    Node *current = list->head;
    Node *previous = NULL;
    while (current != NULL)
    {
        if (!strcmp(current->data.source_dir, key))
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

int sims_update(List *list, char *key, Data new_data)
{
    Node *current = list->head;
    while (current != NULL)
    {
        if (!strcmp(current->data.source_dir, key))
        {
            current->data = new_data;
            return SUCCESS;
        }
        current = current->next;
    }
    return FAILURE;
}
