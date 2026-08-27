#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"
#include <string.h>

#define TELNET_PORT 23

#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254
#define TELNET_ECHO  1

#define TELNET_STATE_DATA       0
#define TELNET_STATE_IAC        1
#define TELNET_STATE_COMMAND    2

#define TELNET_LOGIN_USERNAME      0
#define TELNET_LOGIN_PASSWORD      1
#define TELNET_LOGIN_AUTHENTICATED 2

struct telnet_client
{
    uint8_t telnet_state;
    uint8_t telnet_command;

    uint8_t login_state;

    char username[32];
    uint8_t username_len;

    char password[32];
    uint8_t password_len;

    uint8_t last_was_cr;
};

static err_t telnet_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct telnet_client *client = (struct telnet_client *)arg;
    uint8_t *data;
    u16_t i;

    if (p == NULL)
    {
        /* Client closed the connection */
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
            case TELNET_STATE_DATA:

                if (data[i] == TELNET_IAC)
                {
                    client->telnet_state = TELNET_STATE_IAC;
                }
                else
                {
                    /*
                     * Ignore LF if it immediately follows CR.
                     * Terminals commonly send Enter as CR + LF.
                     */
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
                            static const char password_prompt[] = "\rPassword: ";

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
                        }
                    }
                    else if (client->login_state == TELNET_LOGIN_PASSWORD)
                    {
                        if (data[i] == '\r' || data[i] == '\n')
                        {
                            client->password[client->password_len] = '\0';

                            if (strcmp(client->username, "sudeepk") == 0 && strcmp(client->password, "coral@auth@123") == 0)
                            {
                                static const char login_ok[] = "\r\nLogin successful.\r\n";

                                uint8_t echo_restore[] =
                                {
                                    TELNET_IAC,
                                    TELNET_WONT,
                                    TELNET_ECHO
                                };

                                tcp_write(tpcb, echo_restore, sizeof(echo_restore), TCP_WRITE_FLAG_COPY);

                                tcp_write(tpcb, login_ok, sizeof(login_ok) - 1, TCP_WRITE_FLAG_COPY);

                                client->login_state = TELNET_LOGIN_AUTHENTICATED;
                            }
                            else
                            {
                                static const char login_failed[] = "\r\nLogin incorrect.\r\nUsername: ";

                                client->username_len = 0;
                                client->password_len = 0;

                                tcp_write(tpcb, login_failed, sizeof(login_failed) - 1, TCP_WRITE_FLAG_COPY);

                                client->login_state = TELNET_LOGIN_USERNAME;
                            }

                            tcp_output(tpcb);
                        }
                        else if (client->password_len < sizeof(client->password) - 1)
                        {
                            client->password[client->password_len++] = data[i];

                            /*
                             * Do NOT echo password characters.
                             */
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

                /*
                 * ECHO negotiation is special because the server
                 * initiates it when entering password mode.
                 */
                if (data[i] == TELNET_ECHO)
                {
                    if (client->telnet_command == TELNET_DO)
                    {
                        /*
                         * Client accepted our WILL ECHO.
                         *
                         * Nothing else needs to be sent.
                         */
                    }
                    else if (client->telnet_command == TELNET_DONT)
                    {
                        /*
                         * Client rejected ECHO.
                         * Tell the client we will not echo.
                         */
                        response[0] = TELNET_IAC;
                        response[1] = TELNET_WONT;
                        response[2] = TELNET_ECHO;

                        tcp_write(tpcb,
                                  response,
                                  sizeof(response),
                                  TCP_WRITE_FLAG_COPY);
                    }
                    else if (client->telnet_command == TELNET_WILL ||
                             client->telnet_command == TELNET_WONT)
                    {
                        /*
                         * We don't want the client controlling echo.
                         */
                        response[0] = TELNET_IAC;
                        response[1] = TELNET_DONT;
                        response[2] = TELNET_ECHO;

                        tcp_write(tpcb,
                                  response,
                                  sizeof(response),
                                  TCP_WRITE_FLAG_COPY);
                    }
                }
                else
                {
                    /*
                     * We don't support other Telnet options.
                     */
                    response[0] = TELNET_IAC;

                    if (client->telnet_command == TELNET_WILL ||
                        client->telnet_command == TELNET_WONT)
                    {
                        response[1] = TELNET_DONT;
                    }
                    else
                    {
                        response[1] = TELNET_WONT;
                    }

                    response[2] = data[i];

                    tcp_write(tpcb,
                              response,
                              sizeof(response),
                              TCP_WRITE_FLAG_COPY);
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

    static const char username_prompt[] = "Username: ";

    tcp_write(newpcb,
              username_prompt,
              sizeof(username_prompt) - 1,
              TCP_WRITE_FLAG_COPY);

    tcp_output(newpcb);

    tcp_arg(newpcb, client);
    tcp_recv(newpcb, telnet_recv);

    return ERR_OK;
}

void telnet_server_init(void)
{
    struct tcp_pcb *pcb;

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
