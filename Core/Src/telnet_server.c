#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/timeouts.h"
#include "ip_persist.h"
#include "lwip/dhcp.h"
#include "lwip/apps/snmp.h"
#include <string.h>
#include <stdio.h>
#include "main.h"
#include "cli_commands.h"

#define TELNET_PORT 23

#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254
#define TELNET_ECHO  1
#define TELNET_LINEMODE 34
#define TELNET_SGA   3

#define TELNET_STATE_DATA       0
#define TELNET_STATE_IAC        1
#define TELNET_STATE_COMMAND    2
#define TELNET_STATE_ESC        3
#define TELNET_STATE_ESC_BRACKET 4

#define TELNET_LOGIN_USERNAME      0
#define TELNET_LOGIN_PASSWORD      1
#define TELNET_LOGIN_AUTHENTICATED 2

extern struct netif gnetif;

static uint32_t lcgateext = 0;
static uint32_t ippaext = 0;

struct telnet_client
{
    uint8_t telnet_state;
    uint8_t telnet_command;

    uint8_t login_state;

    char username[32];
    uint8_t username_len;

    char password[32];
    uint8_t password_len;

    char command[64];
    uint8_t command_len;
    uint8_t cursor_pos;

    char completion_prefix[64];
    uint8_t completion_prefix_len;
    uint8_t completion_index;
    uint8_t completion_active;

    uint8_t last_was_cr;
};

static void telnet_show_completion(
    struct tcp_pcb *tpcb,
    struct telnet_client *client
)
{
    size_t i;
    size_t match_count = 0;
    size_t matches[cli_command_count];

    char prefix[64];
    char response[512];

    size_t prefix_len;
    size_t response_len = 0;

    if (client->cursor_pos != client->command_len)
    {
        return;
    }

    prefix_len = client->command_len;
    memcpy(prefix, client->command, prefix_len);
    prefix[prefix_len] = '\0';

    for (i = 0; i < cli_command_count; i++)
    {
        if (strncmp(prefix, cli_commands[i].name, prefix_len) == 0)
        {
            matches[match_count] = i;
            match_count++;
        }
    }

    if (match_count == 0)
    {
        return;
    }

    if (match_count == 1)
    {
        const char *selected_command;
        size_t selected_len;

        selected_command = cli_commands[matches[0]].name;
        selected_len = strlen(selected_command);

        if (selected_len == client->command_len)
        {
            return;
        }

        for (i = 0; i < client->command_len; i++)
        {
            static const char backspace[] = "\b \b";

            tcp_write(tpcb, backspace, sizeof(backspace) - 1, TCP_WRITE_FLAG_COPY);
        }

        memcpy(client->command, selected_command, selected_len);
        client->command_len = selected_len;
        client->cursor_pos = selected_len;
        client->command[selected_len] = '\0';
        tcp_write(tpcb, selected_command, selected_len, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        return;
    }

    response_len += snprintf(&response[response_len], sizeof(response) - response_len, "\r\n");

    for (i = 0; i < match_count; i++)
    {
        response_len += snprintf(&response[response_len], sizeof(response) - response_len, "%-20s", cli_commands[matches[i]].name);

        if ((i + 1) % 3 == 0)
        {
            response_len += snprintf(&response[response_len], sizeof(response) - response_len, "\r\n");
        }
    }

    if (match_count % 3 != 0)
    {
        response_len += snprintf(&response[response_len], sizeof(response) - response_len, "\r\n");
    }

    response_len += snprintf(&response[response_len], sizeof(response) - response_len, ">>> %s", client->command);
    tcp_write(tpcb, response, response_len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
}

static void get_default_network_config(const ip4_addr_t *ip, ip4_addr_t *mask, ip4_addr_t *gateway)
{
    uint8_t first_octet = ip4_addr1(ip);

    //Class A 1-126
    if (first_octet >= 1 && first_octet <= 126)
    {
        IP4_ADDR(mask, 255, 0, 0, 0);
        IP4_ADDR(gateway, first_octet, 255, 255, 254);
    }

    // Class B 128-191
    else if (first_octet >= 128 && first_octet <= 191)
    {
        IP4_ADDR(mask, 255, 255, 0, 0);
        IP4_ADDR(gateway, first_octet, ip4_addr2(ip), 255, 254);
    }

    // Class C 192-223
    else if (first_octet >= 192 && first_octet <= 223)
    {
        IP4_ADDR(mask, 255, 255, 255, 0);
        IP4_ADDR(gateway, first_octet, ip4_addr2(ip), ip4_addr3(ip), 254);
    }
}

static err_t telnet_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct telnet_client *client = (struct telnet_client *)arg;
    uint8_t *data;
    u16_t i;

    if (p == NULL)
    {
        if (client != NULL)
        {
            mem_free(client);
        }
        tcp_arg(tpcb, NULL);
        tcp_close(tpcb);

        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        pbuf_free(p);
        return err;
    }

    if (client == NULL)
    {
        pbuf_free(p);
        return ERR_ARG;
    }

    data = (uint8_t *)p->payload;

    for (i = 0; i < p->len; i++)
    {
        switch (client->telnet_state)
        {
           case TELNET_STATE_ESC:

            if (data[i] == '[')
            {
                client->telnet_state = TELNET_STATE_ESC_BRACKET;
            }
            else
            {
                client->telnet_state = TELNET_STATE_DATA;
            }

            break;

           case TELNET_STATE_ESC_BRACKET:
           {
               static const char left_arrow[]  = "\x1B[1D";
               static const char right_arrow[] = "\x1B[1C";

               if (data[i] == 'D')
               {
                   if (client->cursor_pos > 0)
                   {
                       client->cursor_pos--;
                       tcp_write(tpcb, left_arrow, sizeof(left_arrow) - 1, TCP_WRITE_FLAG_COPY);
                       tcp_output(tpcb);
                   }
               }
               else if (data[i] == 'C')
               {
                   if (client->cursor_pos < client->command_len)
                   {
                       client->cursor_pos++;
                       tcp_write(tpcb, right_arrow, sizeof(right_arrow) - 1, TCP_WRITE_FLAG_COPY);
                       tcp_output(tpcb);
                   }
               }
               else if (data[i] == 'A' || data[i] == 'B')
               {
               }

               client->telnet_state = TELNET_STATE_DATA;
               break;
           }
            case TELNET_STATE_DATA:

                if (data[i] == TELNET_IAC)
                {
                    client->telnet_state = TELNET_STATE_IAC;
                }
                else if (data[i] == 0x1B)
                {
                    client->telnet_state = TELNET_STATE_ESC;
                }
                else
                {
                    if (data[i] == '\n' && client->last_was_cr)
                    {
                        client->last_was_cr = 0;
                        continue;
                    }

                    if (data[i] == '\r')
                    {
                        client->last_was_cr = 1;
                    }
                    else
                    {
                        client->last_was_cr = 0;
                    }

                    if (client->login_state == TELNET_LOGIN_USERNAME)
                    {
                        if (data[i] == '\r' || data[i] == '\n')
                        {
                            static const char password_prompt[] = "\r\nPassword: ";

                            client->username[client->username_len] = '\0';
                            client->password_len = 0;

                            uint8_t echo_off[] =
                            {
                                TELNET_IAC,
                                TELNET_WILL,
                                TELNET_ECHO
                            };

                            tcp_write(tpcb, echo_off, sizeof(echo_off), TCP_WRITE_FLAG_COPY);
                            tcp_write(tpcb, password_prompt, sizeof(password_prompt) - 1, TCP_WRITE_FLAG_COPY);
                            tcp_output(tpcb);
                            client->login_state = TELNET_LOGIN_PASSWORD;
                        }
                        else if (client->username_len < sizeof(client->username) - 1)
                        {
                            client->username[client->username_len++] = data[i];

                            tcp_write(tpcb, &data[i], 1, TCP_WRITE_FLAG_COPY);
                            tcp_output(tpcb);
                        }
                    }
                    else if (client->login_state == TELNET_LOGIN_PASSWORD)
                    {
                        if (data[i] == '\r' || data[i] == '\n')
                        {
                            client->password[client->password_len] = '\0';

                            if (strcmp(client->username, "admin") == 0 && strcmp(client->password, "admin") == 0)
                            {
                            	static const char welcome_screen[] =
                            	    "\r\n\r\n"
                            	    "================================\r\n"
                            	    " LC GATE COMMAND LINE INTERFACE\r\n"
                            	    "================================\r\n"
                            	    "\r\n"
                            	    ">>> ";

                            	uint8_t echo_restore[] =
                            	{
                            	    TELNET_IAC,
                            	    TELNET_WILL,
                            	    TELNET_ECHO
                            	};

                                tcp_write(tpcb, echo_restore, sizeof(echo_restore), TCP_WRITE_FLAG_COPY);
                                tcp_write(tpcb, welcome_screen, sizeof(welcome_screen) - 1, TCP_WRITE_FLAG_COPY);
                                client->login_state = TELNET_LOGIN_AUTHENTICATED;
                            }
                            else
                            {
                            	uint8_t echo_restore[] =
                            	{
                            	    TELNET_IAC,
                            	    TELNET_WILL,
                            	    TELNET_ECHO
                            	};

                            	static const char login_failed[] =
                            	        "\r\n"
                            	        "Login Failed\r\n"
                            	        "\r\n"
                            	        "Username: ";

                                client->username_len = 0;
                                client->password_len = 0;

                                tcp_write(tpcb, echo_restore, sizeof(echo_restore), TCP_WRITE_FLAG_COPY);
                                tcp_write(tpcb, login_failed, sizeof(login_failed) - 1, TCP_WRITE_FLAG_COPY);
                                client->login_state = TELNET_LOGIN_USERNAME;
                            }

                            tcp_output(tpcb);
                        }
                        else if (client->password_len < sizeof(client->password) - 1)
                        {
                            client->password[client->password_len++] = data[i];
                        }

                    }
                    else if (client->login_state == TELNET_LOGIN_AUTHENTICATED)
                    {
                        if (data[i] == '\r' || data[i] == '\n')
                        {
                            client->command[client->command_len] = '\0';
                            if (client->command_len == 0)
                            {
                                static const char response[] = "\r\n>>> ";

                                tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                            }
                            else if (strcmp(client->command, "ip a") == 0)
                            {
                                char response[256];
                                char ip_str[16];
                                char mask_str[16];
                                char gw_str[16];

                                uint32_t uid0;
                                uint32_t uid1;
                                uint32_t uid2;

                                ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_str, sizeof(ip_str));
                                ip4addr_ntoa_r(netif_ip4_netmask(&gnetif), mask_str, sizeof(mask_str));
                                ip4addr_ntoa_r(netif_ip4_gw(&gnetif), gw_str, sizeof(gw_str));

                                uid0 = HAL_GetUIDw0();
                                uid1 = HAL_GetUIDw1();
                                uid2 = HAL_GetUIDw2();

                                snprintf(response, sizeof(response),
                                         "\r\n"
                                         "IP Address : %s\r\n"
                                         "Netmask    : %s\r\n"
                                         "Gateway    : %s\r\n"
                                         "UUID       : %08lX-%08lX-%08lX\r\n"
                                         "\r\n>>> ", ip_str, mask_str, gw_str, (unsigned long)uid0, (unsigned long)uid1, (unsigned long)uid2);

                                tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                            }
                            else if (strcmp(client->command, "dhcp") == 0)
                            {
                                static const char response[] =
                                    "\r\nSwitching to DHCP...\r\n"
                                    "Connection will be lost.\r\n";

                                tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                                tcp_output(tpcb);
                                HAL_Delay(200);


                                    ip4_addr_t zero_ip;
                                    ip4_addr_t zero_mask;
                                    ip4_addr_t zero_gw;

                                    IP4_ADDR(&zero_ip, 0, 0, 0, 0);
                                    IP4_ADDR(&zero_mask, 0, 0, 0, 0);
                                    IP4_ADDR(&zero_gw, 0, 0, 0, 0);

                                    dhcp_stop(&gnetif);
                                    netif_set_addr(&gnetif, &zero_ip, &zero_mask, &zero_gw);
                                    dhcp_start(&gnetif);

                            }
                            else if (strncmp(client->command, "setstatic ", 10) == 0)
                            {
                                ip4_addr_t new_ip;
                                ip4_addr_t new_mask;
                                ip4_addr_t new_gw;

                                char ip_string[16];
                                char mask_string[16];
                                char gw_string[16];
                                char extra_string[16];

                                int parsed;

                                //1 value  = IP, 2 values = IP + MASK, 3 values = IP + MASK + GATEWAY, 4 values = invalid command
                                parsed = sscanf(&client->command[10], "%15s %15s %15s %15s", ip_string, mask_string, gw_string, extra_string);

                                //IP is mandatory
                                if (parsed >= 1 && parsed <= 3 && ip4addr_aton(ip_string, &new_ip))
                                {
                                   //generate default mask and gateway
                                    if (parsed == 1)
                                    {
                                        get_default_network_config(&new_ip, &new_mask, &new_gw);
                                    }

                                    // IP + MASK supplied: use supplied mask and generate gateway.
                                    else if (parsed == 2)
                                    {
                                        if (!ip4addr_aton(mask_string, &new_mask))
                                        {
                                            static const char error[] =
                                                "\r\nInvalid subnet mask.\r\n"
                                                "\r\n>>> ";

                                            tcp_write(tpcb, error, sizeof(error) - 1, TCP_WRITE_FLAG_COPY);
                                            tcp_output(tpcb);
                                            goto setstatic_done;
                                        }

                                        //Gateway is generated from the supplied subnet
                                        uint32_t network;
                                        uint32_t broadcast;
                                        uint32_t mask;
                                        uint32_t gateway;

                                        mask = new_mask.addr;
                                        network = new_ip.addr & mask;
                                        broadcast = network | ~mask;

                                        gateway = broadcast - 1;
                                        new_gw.addr = gateway;
                                    }

                                    //IP + MASK + GATEWAY supplied: use all supplied values.
                                    else
                                    {
                                        if (!ip4addr_aton(mask_string, &new_mask) || !ip4addr_aton(gw_string, &new_gw))
                                        {
                                            static const char error[] = "\r\nInvalid subnet mask or gateway.\r\n\r\n>>> ";
                                            tcp_write(tpcb, error, sizeof(error) - 1, TCP_WRITE_FLAG_COPY);
                                            tcp_output(tpcb);
                                            goto setstatic_done;
                                        }
                                    }

                                        char ip_str[16];
                                        char mask_str[16];
                                        char gw_str[16];
                                        char response[192];

                                        ip4addr_ntoa_r(&new_ip, ip_str, sizeof(ip_str));
                                        ip4addr_ntoa_r(&new_mask, mask_str, sizeof(mask_str));
                                        ip4addr_ntoa_r(&new_gw, gw_str, sizeof(gw_str));

                                        snprintf(response, sizeof(response),
                                                 "\r\n"
                                                 "Changing network configuration...\r\n"
                                                 "IP Address: %s\r\n"
                                                 "Netmask:    %s\r\n"
                                                 "Gateway:    %s\r\n"
                                                 "Connection will be lost.\r\n", ip_str, mask_str, gw_str);

                                        tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                        tcp_output(tpcb);
                                        HAL_Delay(200);
                                        dhcp_stop(&gnetif);
                                        netif_set_addr(&gnetif, &new_ip, &new_mask, &new_gw);

                                }
                            setstatic_done:
                                ;
                            }
                            else if (strcmp(client->command, "getmac") == 0)
                            {
                                char response[128];

                                snprintf(response, sizeof(response),
                                         "\r\n"
                                         "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\r\n"
                                         "\r\n>>> ",
                                         gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
                                         gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);

                                tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                            }
                            else if (strncmp(client->command, "setlcgateext ", 13) == 0)
                            {
                                uint32_t value;

                                static const char response[] = "\r\nLC Gate Ext set successfully.\r\n\r\n>>> ";

                                if (sscanf(&client->command[13], "%lu", &value) == 1)
                                {
                                    if (IP_Persist_Save_Ext(value, ippaext) == HAL_OK)
                                    {
                                        lcgateext = value;
                                        tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                                        tcp_output(tpcb);
                                    }
                                }
                            }
                            else if (strcmp(client->command, "getlcgateext") == 0)
                            {
                                char response[96];
                                snprintf(response, sizeof(response), "\r\nLC Gate Ext: %lu\r\n\r\n>>> ", (unsigned long)lcgateext);
                                tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                            }
                            else if (strncmp(client->command, "setippaext ", 11) == 0)
                            {
                                uint32_t value;

                                static const char response[] = "\r\nIPPA Ext set successfully.\r\n\r\n>>> ";

                                if (sscanf(&client->command[11], "%lu", &value) == 1)
                                {
                                    if (IP_Persist_Save_Ext(lcgateext, value) == HAL_OK)
                                    {
                                        ippaext = value;
                                        tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                                        tcp_output(tpcb);
                                    }
                                }
                            }
                            else if (strcmp(client->command, "getippaext") == 0)
                            {
                            	char response[96];

                            	snprintf(response, sizeof(response), "\r\nIPPA Ext: %lu\r\n\r\n>>> ", (unsigned long)ippaext);
                            	tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                            }
                            else if (strncmp(client->command, "settrapip ", 10) == 0)
                            {
                                ip4_addr_t new_manager_ip4;

                                if (ip4addr_aton(&client->command[10], &new_manager_ip4))
                                {
                                    HAL_StatusTypeDef status =
                                        IP_Persist_Save_Manager_IP(new_manager_ip4.addr);

                                    if (status == HAL_OK)
                                    {
                                        ip_addr_t new_manager_ip;
                                        new_manager_ip.addr = new_manager_ip4.addr;
                                        snmp_trap_dst_ip_set(0, &new_manager_ip);
                                        const char *response = "SNMP Manager IP saved successfully.\r\n\r\n>>> ";

                                        tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                        tcp_output(tpcb);
                                    }
                                    else
                                    {
                                        const char *response = "Error: Failed to save SNMP Manager IP.\r\n\r\n>>> ";
                                        tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                        tcp_output(tpcb);
                                    }
                                }
                                else
                                {
                                    const char *response = "Invalid IP address. Use: settrapip 192.168.80.7\r\n\r\n>>> ";
                                    tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                    tcp_output(tpcb);
                                }
                            }
                            else if (strcmp(client->command, "gettrapip") == 0)
                            {
                                uint32_t saved_manager_ip = IP_Persist_Load_Manager_IP();

                                ip4_addr_t manager_ip4;
                                char manager_ip_str[16];
                                char response[96];

                                if (saved_manager_ip != 0)
                                {
                                    manager_ip4.addr = saved_manager_ip;
                                }
                                else
                                {
                                    IP4_ADDR(&manager_ip4, 192, 168, 80, 7);
                                }

                                ip4addr_ntoa_r(&manager_ip4, manager_ip_str, sizeof(manager_ip_str));

                                snprintf(response, sizeof(response), "\r\nSNMP Manager IP: %s\r\n\r\n>>> ", manager_ip_str);
                                tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                tcp_output(tpcb);
                            }
                            else if (strcmp(client->command, "get detail") == 0)
                            {
                                char response[512];

                                char ip_str[16];
                                char mask_str[16];
                                char gw_str[16];
                                char manager_ip_str[16];

                                uint32_t uid0;
                                uint32_t uid1;
                                uint32_t uid2;

                                uint32_t saved_manager_ip;
                                ip4_addr_t manager_ip4;

                                /* Get current network configuration */
                                ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_str, sizeof(ip_str));
                                ip4addr_ntoa_r(netif_ip4_netmask(&gnetif), mask_str, sizeof(mask_str));
                                ip4addr_ntoa_r(netif_ip4_gw(&gnetif), gw_str, sizeof(gw_str));

                                uid0 = HAL_GetUIDw0();
                                uid1 = HAL_GetUIDw1();
                                uid2 = HAL_GetUIDw2();

                                saved_manager_ip = IP_Persist_Load_Manager_IP();

                                if (saved_manager_ip != 0)
                                {
                                    manager_ip4.addr = saved_manager_ip;
                                }
                                else
                                {
                                    IP4_ADDR(&manager_ip4, 192, 168, 80, 7);
                                }

                                ip4addr_ntoa_r(&manager_ip4, manager_ip_str, sizeof(manager_ip_str));

                                snprintf(response, sizeof(response),
                                    "\r\n"
                                    "IP Address      : %s\r\n"
                                    "Netmask         : %s\r\n"
                                    "Gateway         : %s\r\n"
                                    "MAC Address     : %02X:%02X:%02X:%02X:%02X:%02X\r\n"
                                    "UUID            : %08lX-%08lX-%08lX\r\n"
                                    "LC Gate Ext     : %lu\r\n"
                                    "IPPA Ext        : %lu\r\n"
                                    "SNMP Manager IP : %s\r\n"
                                    "\r\n>>> ",
                                    ip_str, mask_str, gw_str,
                                    gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2], gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5],
                                    (unsigned long)uid0, (unsigned long)uid1, (unsigned long)uid2,
                                    (unsigned long)lcgateext, (unsigned long)ippaext,
                                    manager_ip_str
                                );

                                tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
                                tcp_output(tpcb);
                            }
                            else if (strcmp(client->command, "help") == 0)
                            {
                            	static const char response[] =
                            			"\r\nAvailable Commands:"
                            			"\r\nip a         - To check current ip, netmask, gateway and uuid"
                            			"\r\ngetmac       - To check MAC address"
                            			"\r\nsetstatic    - To set static ip (FORMAT: setstatic <ip> <netmask> <gateway>)"
                            			"\r\ndhcp         - To set dhcp configuration"
                            			"\r\nsettrapip    - To set snmp manager ip"
                            			"\r\ngettrapip    - To check snmp manager ip"
                            			"\r\nsetlcgateext - To set lc gate extension"
                            			"\r\ngetlcgateext - To check current lc gate extension"
                            			"\r\nsetippaext   - To set ippa extension"
                            			"\r\ngetippaext   - To check current ippa extension"
                            			"\r\nget detail   - To check system details\r\n\r\n>>> ";
                            	tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                            	tcp_output(tpcb);
                            }
                            else
                            {
                                static const char response[] = "\r\nUnknown Command\r\n\r\n>>> ";
                                tcp_write(tpcb, response, sizeof(response) - 1, TCP_WRITE_FLAG_COPY);
                            }

                            client->command_len = 0;
                            client->command[0] = '\0';
                            client->cursor_pos = 0;

                            client->completion_prefix_len = 0;
                            client->completion_index = 0;
                            client->completion_active = 0;
                            client->completion_prefix[0] = '\0';

                            tcp_output(tpcb);
                        }
                        else if (data[i] == 0x08 || data[i] == 0x7F)
                        {
                            if (client->cursor_pos > 0)
                            {
                                uint8_t tail_len;
                                uint8_t j;

                                memmove(&client->command[client->cursor_pos - 1], &client->command[client->cursor_pos], client->command_len - client->cursor_pos);

                                client->command_len--;
                                client->cursor_pos--;
                                client->command[client->command_len] = '\0';
                                tail_len = client->command_len - client->cursor_pos;

                                static const char backspace[] = "\b";
                                static const char erase_char[] = " ";

                                tcp_write(tpcb, backspace, 1, TCP_WRITE_FLAG_COPY);

                                if (tail_len > 0)
                                {
                                    tcp_write(tpcb, &client->command[client->cursor_pos], tail_len, TCP_WRITE_FLAG_COPY);
                                }

                                tcp_write(tpcb, erase_char, 1, TCP_WRITE_FLAG_COPY);

                                for (j = 0; j < tail_len + 1; j++)
                                {
                                    tcp_write(tpcb, backspace, 1, TCP_WRITE_FLAG_COPY);
                                }

                                client->completion_active = 0;
                                client->completion_index = 0;
                                client->completion_prefix_len = 0;
                                client->completion_prefix[0] = '\0';

                                tcp_output(tpcb);
                            }
                            continue;
                        }
                        else if (data[i] == 0x09)
                        {
                            client->command[client->command_len] = '\0';
                            telnet_show_completion(tpcb, client);
                            continue;
                        }
                        else if (client->command_len < sizeof(client->command) - 1)
                        {
                            uint8_t tail_len;
                            uint8_t j;

                            static const char backspace[] = "\b";

                            client->completion_active = 0;
                            client->completion_index = 0;
                            client->completion_prefix_len = 0;
                            client->completion_prefix[0] = '\0';

                            tail_len = client->command_len - client->cursor_pos;

                            memmove(&client->command[client->cursor_pos + 1], &client->command[client->cursor_pos], tail_len);

                            client->command[client->cursor_pos] = data[i];
                            client->command_len++;
                            client->cursor_pos++;
                            client->command[client->command_len] = '\0';

                            tcp_write(tpcb, &data[i], 1, TCP_WRITE_FLAG_COPY);

                            if (tail_len > 0)
                            {
                                tcp_write(tpcb, &client->command[client->cursor_pos], tail_len, TCP_WRITE_FLAG_COPY);
                            }

                            for (j = 0; j < tail_len; j++)
                            {
                                tcp_write(tpcb, backspace, 1, TCP_WRITE_FLAG_COPY);
                            }
                            tcp_output(tpcb);
                        }
                    }

                }
                break;

            case TELNET_STATE_IAC:

                if (data[i] == TELNET_IAC)
                {
                    tcp_write(tpcb, &data[i], 1, TCP_WRITE_FLAG_COPY);
                    client->telnet_state = TELNET_STATE_DATA;
                }
                else if (data[i] == TELNET_WILL || data[i] == TELNET_WONT || data[i] == TELNET_DO || data[i] == TELNET_DONT)
                {
                    client->telnet_command = data[i];
                    client->telnet_state = TELNET_STATE_COMMAND;
                }
                else
                {
                    client->telnet_state = TELNET_STATE_DATA;
                }

                break;

            case TELNET_STATE_COMMAND:
            {
                uint8_t response[3];
                response[0] = TELNET_IAC;

                if (data[i] == TELNET_ECHO)
                {
                    if (client->telnet_command == TELNET_DO)
                    {
                        response[1] = TELNET_WILL;
                        response[2] = TELNET_ECHO;

                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_DONT)
                    {
                        response[1] = TELNET_WONT;
                        response[2] = TELNET_ECHO;

                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_WILL ||
                             client->telnet_command == TELNET_WONT)
                    {
                        response[1] = TELNET_DONT;
                        response[2] = TELNET_ECHO;
                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                }

                else if (data[i] == TELNET_SGA)
                {
                    if (client->telnet_command == TELNET_DO)
                    {
                        response[1] = TELNET_WILL;
                        response[2] = TELNET_SGA;
                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_DONT)
                    {
                        response[1] = TELNET_WONT;
                        response[2] = TELNET_SGA;
                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_WILL)
                    {
                        response[1] = TELNET_DO;
                        response[2] = TELNET_SGA;
                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_WONT)
                    {
                        response[1] = TELNET_DONT;
                        response[2] = TELNET_SGA;
                        tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                    }
                }

                else
                {
                    if (client->telnet_command == TELNET_WILL || client->telnet_command == TELNET_WONT)
                    {
                        response[1] = TELNET_DONT;
                    }
                    else
                    {
                        response[1] = TELNET_WONT;
                    }

                    response[2] = data[i];
                    tcp_write(tpcb, response, sizeof(response), TCP_WRITE_FLAG_COPY);
                }
                client->telnet_state = TELNET_STATE_DATA;
                client->telnet_command = 0;
                break;
            }
            default:

                client->telnet_state = TELNET_STATE_DATA;
                break;
        }
    }

    tcp_output(tpcb);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

static err_t telnet_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    struct telnet_client *client;

    if (err != ERR_OK || newpcb == NULL)
    {
        return ERR_VAL;
    }

    client = mem_malloc(sizeof(struct telnet_client));

    if (client == NULL)
    {
        tcp_close(newpcb);
        return ERR_MEM;
    }

    client->telnet_state = TELNET_STATE_DATA;
    client->telnet_command = 0;

    client->login_state = TELNET_LOGIN_USERNAME;
    client->username_len = 0;
    client->password_len = 0;
    client->last_was_cr = 0;
    client->command_len = 0;
    client->cursor_pos = 0;
    client->completion_prefix_len = 0;
    client->completion_index = 0;
    client->completion_active = 0;
    client->completion_prefix[0] = '\0';

    static const uint8_t telnet_options[] =
    {
        /* Server will echo characters */
        TELNET_IAC,
        TELNET_WILL,
        TELNET_ECHO,

        /* Server will suppress Go Ahead */
        TELNET_IAC,
        TELNET_WILL,
        TELNET_SGA,

        /* Request client to suppress Go Ahead */
        TELNET_IAC,
        TELNET_DO,
        TELNET_SGA
    };

    tcp_write(newpcb, telnet_options, sizeof(telnet_options), TCP_WRITE_FLAG_COPY);

    static const char username_prompt[] = "Username: ";

    tcp_write(newpcb, username_prompt, sizeof(username_prompt) - 1, TCP_WRITE_FLAG_COPY);
    tcp_output(newpcb);

    tcp_arg(newpcb, client);
    tcp_recv(newpcb, telnet_recv);

    return ERR_OK;
}

void telnet_server_init(void)
{
    struct tcp_pcb *pcb;

        if (IP_Persist_Load_Ext(&lcgateext, &ippaext) != HAL_OK)
        {
            lcgateext = 0;
            ippaext = 0;
        }

    pcb = tcp_new();

    if (pcb == NULL)
    {
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, TELNET_PORT) != ERR_OK)
    {
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen(pcb);

    if (pcb == NULL)
    {
        return;
    }

    tcp_accept(pcb, telnet_accept);
}

uint32_t Telnet_Get_LCGateExt(void)
{
    return lcgateext;
}

uint32_t Telnet_Get_IPPAExt(void)
{
    return ippaext;
}
