#ifndef SYNC_INFO_MEM_STORE_H
#define SYNC_INFO_MEM_STORE_H

#define BUF_SIZE 1024

#include <time.h>
#include <netinet/in.h>

typedef struct ListTag *SyncInfoMemStore;

typedef struct
{
    char source_dir[BUF_SIZE]; // acts as key for SyncInfo entries
    char source_ip[INET6_ADDRSTRLEN];
    int source_port;
    char target_dir[BUF_SIZE];
    int target_port;
    char target_ip[INET6_ADDRSTRLEN];
    int inotify_wd;
    time_t last_sync;
    int error_num;
    int status;
    int from_config;
} SyncInfo;

SyncInfoMemStore sims_init();
void sims_add(SyncInfoMemStore sync_info_mem_store, SyncInfo sync_info);
void sims_print(SyncInfoMemStore sync_info_mem_store);
void sims_free(SyncInfoMemStore sync_info_mem_store);
int sims_exists(SyncInfoMemStore sync_info_mem_store, SyncInfo sync_info);
int sims_pop(SyncInfoMemStore sync_info_mem_store, SyncInfo *sync_info);
void sims_copy(SyncInfoMemStore src, SyncInfoMemStore dest);
int sims_find(SyncInfoMemStore sync_info_mem_store, char* key, SyncInfo* sync_info);
int sims_remove(SyncInfoMemStore sync_info_mem_store, char* key);
int sims_update(SyncInfoMemStore sync_info_mem_store, char *key, SyncInfo new_sync_info);

#endif // SYNC_INFO_MEM_STORE_H
