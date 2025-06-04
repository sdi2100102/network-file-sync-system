CC=gcc

CFLAGS = -g -O0

LDFLAGS += -lpthread -O0 -g

all: main client clients

MANAGER = main

MANAGER_OBJS = main.o

$(MANAGER): $(MANAGER_OBJS)
	$(CC) $(CFLAGS) $(MANAGER_OBJS) $(LDFLAGS) -o $(MANAGER)

run:
	./$(MANAGER)

test:
	$(MAKE)
	$(MAKE) run

full:
	$(MAKE)
	$(MAKE) run

clean:
	rm -f $(MANAGER) $(MANAGER_OBJS) nfs_client

client:
	gcc -g -O0 -o nfs_client nfs_client.c -lpthread

client_run:
	./nfs_client -p 8088

clients: source_client target_client

source_client:
	gcc -g -O0 -o source/nfs_client nfs_client.c -lpthread

source_run:
	cd source; ./nfs_client -p 8001

target_run:
	cd target; ./nfs_client -p 8002

target_client:
	gcc -g -O0 -o target/nfs_client nfs_client.c -lpthread

client_full:
	$(MAKE)
	$(MAKE) client_run