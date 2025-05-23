CC=gcc

CFLAGS = -g

LDFLAGS +=

all: fss_manager fss_worker fss_console

MANAGER = fss_manager
WORKER = fss_worker
CONSOLE = fss_console

MANAGER_OBJS = fss_manager.o utils.o fss_log.o sync_info_mem_store.o operation_queue.o command.o worker_list.o
WORKER_OBJS = fss_worker.o exec_report.o utils.o
CONSOLE_OBJS = fss_console.o utils.o fss_log.o command.o

$(MANAGER): $(MANAGER_OBJS)
	$(CC) $(CFLAGS) $(MANAGER_OBJS) $(LDFLAGS) -o $(MANAGER)

$(WORKER): $(WORKER_OBJS)
	$(CC) $(CFLAGS) $(WORKER_OBJS) $(LDFLAGS) -o $(WORKER)

$(CONSOLE): $(CONSOLE_OBJS)
	$(CC) $(CFLAGS) $(CONSOLE_OBJS) $(LDFLAGS) -o $(CONSOLE)

clean:
	rm -f $(MANAGER) $(WORKER) $(CONSOLE) $(MANAGER_OBJS) $(WORKER_OBJS) $(CONSOLE_OBJS)

manager_run:
	./$(MANAGER) -l manager_log_file.txt -c config_file.txt -n 10 -p 8000 -b 20 

console_run:
	./$(CONSOLE) -l console_log_file.txt -h 127.0.0.1 -p 8000

