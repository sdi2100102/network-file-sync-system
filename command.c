#include <string.h>
#include <stdlib.h>

#include "command.h"

#define MAX_ARGUMENTS 3

void reset(Command *cmd); // helper function

Command cmd_init()
{
    Command cmd;
    cmd.type = UKNOWN;
    cmd.arguments = malloc(MAX_ARGUMENTS * sizeof(char *));
    cmd.arguments[0] = NULL;
    return cmd;
}

void reset(Command *cmd)
{
    cmd->type = UKNOWN;
    for (int arg_count = 0; cmd->arguments[arg_count] != NULL; arg_count++)
    {
        cmd->arguments[arg_count] = NULL;
    }
}

void cmd_string_to_command(Command *cmd, char *string)
{
    reset(cmd);
    char *token = strtok(string, " \n"); // get first token
    if (token == NULL)
        return;
    cmd->arguments[0] = malloc(strlen(token) * sizeof(char));
    strcpy(cmd->arguments[0], token);

    int arg_count = 1;
    while ((token = strtok(NULL, " \n")) != NULL)
    {
        cmd->arguments[arg_count] = malloc(strlen(token) * sizeof(char));
        strcpy(cmd->arguments[arg_count], token);
        arg_count++;
        if (arg_count > MAX_ARGUMENTS)
        {
            cmd->arguments[arg_count] = NULL;
            cmd->type = UKNOWN;
            return;
        }
    }
    cmd->arguments[arg_count] = NULL; // set last argument to NULL

    if (strcmp(cmd->arguments[0], "add") == 0 && arg_count == 3)
    {
        cmd->type = ADD;
    }
    else if (strcmp(cmd->arguments[0], "status") == 0 && arg_count == 2)
    {
        cmd->type = STATUS;
    }
    else if (strcmp(cmd->arguments[0], "cancel") == 0 && arg_count == 2)
    {
        cmd->type = CANCEL;
    }
    else if (strcmp(cmd->arguments[0], "sync") == 0 && arg_count == 2)
    {
        cmd->type = SYNC;
    }
    else if (strcmp(cmd->arguments[0], "shutdown") == 0 && arg_count == 1)
    {
        cmd->type = SHUTDOWN;
    }
    else
    {
        cmd->type = UKNOWN;
    }
}

void cmd_free(Command cmd)
{
    for (int arg_count = 0; cmd.arguments[arg_count] != NULL; arg_count++)
    {
        free(cmd.arguments[arg_count]);
    }
    free(cmd.arguments);
}

void cmd_command_to_string(Command cmd, char *string)
{
    string[0] = '\0';
    for (int arg_count = 0; cmd.arguments[arg_count] != NULL; arg_count++)
    {
        strcat(string, cmd.arguments[arg_count]);
        strcat(string, " ");
    }
}
