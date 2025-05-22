#ifndef COMMAND_H
#define COMMAND_H

typedef enum
{
    ADD,
    STATUS,
    CANCEL,
    SYNC,
    SHUTDOWN,
    UKNOWN
} CommandType;

typedef struct
{
    CommandType type;
    char **arguments;
} Command;

Command cmd_init();
void cmd_free(Command cmd);
void cmd_string_to_command(Command *cmd, char *string);
void cmd_command_to_string(Command cmd, char *string);

#endif // COMMAND_H