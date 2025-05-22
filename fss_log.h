#ifndef FSS_LOG_H
#define FSS_LOG_H

#define END_OF_MESSAGE "__END__"

void log_timed_stdout(char message[]);
void log_timed_fd(char message[], int fd);
void log_untimed_fd(char message[], int fd);
void log_end_message(int fd);

#endif // FSS_LOG_H
