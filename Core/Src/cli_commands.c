#include "cli_commands.h"

const cli_command_t cli_commands[] =
{
    {
        .name = "ip a"
    },
    {
        .name = "dhcp"
    },
    {
        .name = "setstatic"
    },
    {
        .name = "getmac"
    },
    {
        .name = "setlcgateext"
    },
    {
        .name = "getlcgateext"
    },
    {
        .name = "setippaext"
    },
    {
        .name = "getippaext"
    },
    {
        .name = "settrapip"
    },
    {
        .name = "gettrapip"
    },
    {
        .name = "get detail"
    },
    {
        .name = "help"
    }
};

const uint32_t cli_command_count = sizeof(cli_commands) / sizeof(cli_commands[0]);
