#ifndef SNMP_PERSIST_H
#define SNMP_PERSIST_H

#include "main.h"

#define SNMP_PERSIST_FLASH_ADDRESS   0x081E0000UL
#define SNMP_PERSIST_MAGIC           0x534E5033UL

HAL_StatusTypeDef SNMP_Persist_Init(uint32_t *engine_boots);

#endif
