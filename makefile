CC=gcc

CFLAGS = -g

LDFLAGS += -lpthread

all: nfs_manager nfs_console

MANAGER = nfs_manager
CONSOLE = nfs_console

MANAGER_OBJS = nfs_manager.o utils.o nfs_log.o sync_info_mem_store.o operation_queue.o command.o file_operation.o nfs_workers.o exec_report.o
CONSOLE_OBJS = nfs_console.o utils.o nfs_log.o command.o

$(MANAGER): $(MANAGER_OBJS)
	$(CC) $(CFLAGS) $(MANAGER_OBJS) $(LDFLAGS) -o $(MANAGER)

$(CONSOLE): $(CONSOLE_OBJS)
	$(CC) $(CFLAGS) $(CONSOLE_OBJS) $(LDFLAGS) -o $(CONSOLE)

clean:
	rm -f $(MANAGER) $(CONSOLE) $(MANAGER_OBJS) $(CONSOLE_OBJS)

manager_run:
	./$(MANAGER) -l manager_log_file.txt -c config_file.txt -n 10 -p 8000 -b 20 

console_run:
	./$(CONSOLE) -l console_log_file.txt -h 127.0.0.1 -p 8000

