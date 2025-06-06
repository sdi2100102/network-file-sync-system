CC=gcc

CFLAGS = -g

LDFLAGS += -lpthread

all: nfs_manager nfs_console source_client target_client

MANAGER = nfs_manager
CONSOLE = nfs_console

SOURCE_CLIENT = client_test/source/nfs_client
TARGET_CLIENT = client_test/target/nfs_client

MANAGER_OBJS = nfs_manager.o utils.o nfs_log.o sync_info_mem_store.o command.o file_operation.o nfs_workers.o exec_report.o
CONSOLE_OBJS = nfs_console.o utils.o nfs_log.o command.o

$(MANAGER): $(MANAGER_OBJS)
	$(CC) $(CFLAGS) $(MANAGER_OBJS) $(LDFLAGS) -o $(MANAGER)

$(CONSOLE): $(CONSOLE_OBJS)
	$(CC) $(CFLAGS) $(CONSOLE_OBJS) $(LDFLAGS) -o $(CONSOLE)

clean:
	rm -f $(MANAGER) $(CONSOLE) $(MANAGER_OBJS) $(CONSOLE_OBJS) $(SOURCE_CLIENT) $(TARGET_CLIENT) nfs_client.o

manager_run:
	./$(MANAGER) -l manager_log_file.txt -c config_file.txt -n 2 -p 8000 -b 20 

console_run:
	./$(CONSOLE) -l console_log_file.txt -h 127.0.0.1 -p 8000

manager_full: # todo remove
	$(MAKE)
	$(MAKE) manager_run

debug:
	$(MAKE) CFLAGS='-g -DDEBUG'

manager_debug:
	$(MAKE) CFLAGS='-g -DDEBUG'
	$(MAKE) manager_run

clients: source_client target_client

source_client: nfs_client.o
	$(CC) $(CFLAGS) nfs_client.o $(LDFLAGS) -o ${SOURCE_CLIENT}

target_client: nfs_client.o
	$(CC) $(CFLAGS) nfs_client.o $(LDFLAGS) -o ${TARGET_CLIENT}

source_run: source_client
	cd client_test/source; ./nfs_client -p 8001

target_run: target_client
	cd client_test/target; ./nfs_client -p 8002