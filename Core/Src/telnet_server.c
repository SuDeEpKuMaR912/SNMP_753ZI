#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"

#define TELNET_PORT 23

#define TELNET_IAC   255
#define TELNET_WILL  251
#define TELNET_WONT  252
#define TELNET_DO    253
#define TELNET_DONT  254

#define TELNET_STATE_DATA       0
#define TELNET_STATE_IAC        1
#define TELNET_STATE_COMMAND    2

struct telnet_client
{
    uint8_t telnet_state;
    uint8_t telnet_command;
};

static err_t telnet_recv(void *arg, struct tcp_pcb *tpcb,
                         struct pbuf *p, err_t err)
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
                    /* Normal character: echo it */
                    tcp_write(tpcb,
                              &data[i],
                              1,
                              TCP_WRITE_FLAG_COPY);
                }

                break;

            case TELNET_STATE_IAC:

                if (data[i] == TELNET_IAC)
                {
                    tcp_write(tpcb,
                              &data[i],
                              1,
                              TCP_WRITE_FLAG_COPY);

                    client->telnet_state = TELNET_STATE_DATA;
                }
                else if (data[i] == TELNET_WILL ||
                         data[i] == TELNET_WONT ||
                         data[i] == TELNET_DO ||
                         data[i] == TELNET_DONT)
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
