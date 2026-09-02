#ifndef IP_PERSIST_H
#define IP_PERSIST_H

#include "main.h"
#include "lwip/ip4_addr.h"

#define IP_PERSIST_FLASH_ADDRESS    0x081C0000UL
#define IP_PERSIST_MAGIC            0x49504346UL

HAL_StatusTypeDef IP_Persist_Init(ip4_addr_t *ip,
                                  const ip4_addr_t *default_ip);

HAL_StatusTypeDef IP_Persist_Save(const ip4_addr_t *ip);

#endif
