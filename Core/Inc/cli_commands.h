#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include <stdint.h>

#define CLI_COMMAND_NAME_MAX       32
#define CLI_COMMAND_DESC_MAX       96

typedef struct
{
    const char *name;
} cli_command_t;

extern const cli_command_t cli_commands[];

extern const uint32_t cli_command_count;

#endif /* CLI_COMMANDS_H */
