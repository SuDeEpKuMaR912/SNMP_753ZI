#include "bms_monitor.h"

#include <stdio.h>
#include <string.h>
#include "lwip/apps/snmp.h"

extern UART_HandleTypeDef huart2;

static uint8_t bms_cmd[] =
{
    0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77
};

static uint8_t previous_soc = 0;
static uint8_t soc_initialized = 0;

static const u32_t bms_enterprise_oid[] =
{
    1, 3, 6, 1, 4, 1, 12345, 1
};

void Read_BMS_Data(void)
{
    uint8_t ch;
    uint16_t len = 0;
    uint8_t frame[128];
    uint8_t *p = frame;

    memset(frame, 0, sizeof(frame));

    while(HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK);

    HAL_UART_Transmit(&huart2, bms_cmd, sizeof(bms_cmd), HAL_MAX_DELAY);

    while(1)
    {
        if(HAL_UART_Receive(&huart2, &ch, 1, 1000) != HAL_OK)
        {
            printf("BMS Read Failed\r\n");
            return;
        }

        if(ch == 0xDD)
        {
            frame[0] = ch;
            break;
        }
    }

    if(HAL_UART_Receive(&huart2, &frame[1], 3, 1000) != HAL_OK)
    {
        printf("Header receive failed\r\n");
        return;
    }

    uint8_t dataLength = frame[3];
    uint16_t totalLength = 4 + dataLength + 3;

    if(totalLength > sizeof(frame))
    {
        printf("Invalid frame length\r\n");
        return;
    }

    /* Receive remaining bytes */
    if(HAL_UART_Receive(&huart2, &frame[4], totalLength - 4, 1000) != HAL_OK)
    {
        printf("Remaining frame receive failed\r\n");
        return;
    }

    len = totalLength;

    printf("\r\nReceived %u bytes\r\n", len);

    for(uint16_t i = 0; i < len; i++)
        printf("%02X ", frame[i]);

    printf("\r\n");

    if(len < 50)
    {
        printf("Frame too short\r\n");
        return;
    }

    uint16_t packVoltage;
    int16_t current;
    uint16_t remainCap;
    uint16_t ratedCap;
    uint16_t cycles;
    uint8_t soc;

    /* Decode frame */
    packVoltage = ((uint16_t)p[4] << 8) | p[5];
    current     = ((int16_t)p[6] << 8) | p[7];
    remainCap   = ((uint16_t)p[8] << 8) | p[9];
    ratedCap    = ((uint16_t)p[10] << 8) | p[11];
    cycles      = ((uint16_t)p[12] << 8) | p[13];
    soc         = p[23];

    if(!soc_initialized)
    {
        previous_soc = soc;
        soc_initialized = 1;
    }
    else if(soc != previous_soc)
    {
        struct snmp_obj_id eoid;
        struct snmp_varbind bms_varbind;

        char state_str[20];
        char bms_info[128];

        err_t trap_err;

        if(current > 0)
        {
            snprintf(state_str, sizeof(state_str), "CHARGING");
        }
        else if(current < 0)
        {
            snprintf(state_str, sizeof(state_str), "DISCHARGING");
        }
        else
        {
            snprintf(state_str, sizeof(state_str), "IDLE");
        }

        snprintf(bms_info, sizeof(bms_info), "SOC: %u, State: %s, Voltage: %.2f V, Current: %.2f A", soc, state_str,
                 packVoltage / 100.0f, current / 100.0f);

        snmp_oid_assign(&eoid, bms_enterprise_oid, sizeof(bms_enterprise_oid) / sizeof(bms_enterprise_oid[0]));

        bms_varbind.oid = eoid;
        bms_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        bms_varbind.value = bms_info;
        bms_varbind.value_len = strlen(bms_info);
        bms_varbind.next = NULL;
        bms_varbind.prev = NULL;

        trap_err = snmp_send_trap(&eoid, SNMP_GENTRAP_ENTERPRISE_SPECIFIC, 2, &bms_varbind);

        if(trap_err == ERR_OK)
        {
            printf("SNMP TRAP SENT\r\n");
            printf("%s\r\n", bms_info);
        }
        else
        {
            printf("SNMP TRAP FAILED: %d\r\n", trap_err);
        }

        previous_soc = soc;
    }

    printf("\r\n");
    printf("========================================\r\n");

    printf("Pack Voltage     : %.2f V\r\n", packVoltage / 100.0f);

    printf("Current          : %.2f A\r\n", current / 100.0f);

    if(current > 0)
    {
        printf("Battery Status   : CHARGING\r\n");
    }

    printf("Remaining Cap    : %.2f Ah\r\n", remainCap / 100.0f);

    printf("Nominal Cap      : %.2f Ah\r\n", ratedCap / 100.0f);

    printf("Cycle Count      : %u\r\n", cycles);

    printf("SOC              : %u %%\r\n", soc);

    printf("Checksum         : %02X %02X\r\n", p[len - 3], p[len - 2]);

    printf("End Byte         : %02X\r\n", p[len - 1]);

    printf("========================================\r\n");
}
