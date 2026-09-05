#ifndef IP_PERSIST_H
#define IP_PERSIST_H

#include "main.h"
#include "lwip/ip4_addr.h"

#define IP_PERSIST_FLASH_ADDRESS    0x081C0000UL
#define IP_PERSIST_MAGIC            0x49504346UL

typedef enum
{
    IP_MODE_STATIC = 0,
    IP_MODE_DHCP = 1
} IP_Mode_t;

HAL_StatusTypeDef IP_Persist_Init(ip4_addr_t *ip, const ip4_addr_t *default_ip);
HAL_StatusTypeDef IP_Persist_Save(const ip4_addr_t *ip);

HAL_StatusTypeDef IP_Persist_Save_Mode(const ip4_addr_t *ip, IP_Mode_t mode);
HAL_StatusTypeDef IP_Persist_Load_Mode(IP_Mode_t *mode);

#endif
