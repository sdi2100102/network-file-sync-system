# Network file synchronization system

Project implements a distributed file synchronization service in C using TCP sockets and multithreading. A central nfs_manager coordinates sync tasks between source and target nfs_client instances, while nfs_console provides an interactive CLI for commands like add/cancel/shutdown. The manager discovers files on the source, queues transfer operations, and worker threads execute pull/push file-copy pipelines with logging and execution reports.

## Compilation and Execution Instructions

- Use `make` to compile all source files.
- Use `make clean` to remove all object and executable files.
- Use `make manager_run` and `make console_run` to run with default arguments.
- Use `make source_run` and `make target_run` to run test clients located in `client_test` directory.

## Project Description

### Modules used

- `nfs_manager.c` for the manager process.
  - The manager process acts as a server that uses TCP sockets for communication with the console.
  - For communication with `nfs_client`, the manager acts as a client, while `nfs_client` is the server.
  - For placing file transfer operations, the manager uses a producer consumer pattern using condition variables, where the consumers are the thread workers.
- `nfs_console.c` for the console process.
  - The console acts as a TCP client, while the manager is a TCP server.
- `nfs_client.c` for the client process.
  - `nfs_client` is multithreaded in order to support parallel file transfer operations. Specifically, it uses detached threads for file transfer operations.
  - The client's main thread listens for connections from the manager. When a connection is established, the client creates a thread for file transfer operations.
  - Each thread is responsible for one list, pull or push operation.
- `nfs_workers.c` for the thread workers.
- `command.c` for the console command parser.
- `exec_report.c` for the creation of a worker's log message.
- `nfs_log.c`
- `sync_info_mem_store.c`
- `utils.c`
- `debug.h`

### Manager-Client communication for file transfer operations

The manager follows this protocol for file transfer operations:

- The manager sends a pull command to the source client.
- The source client responds with a chunk size.
- The manager sends an ACK message to the source client. Now the source client is ready to send data.
- The manager sends a push command to the target client.
- The target client responds with an ACK message.
- The manager sends a size message to the target client.
- The target client responds with an ACK message.
- The manager reads a data buffer from the source client and sends the data to the target client.
