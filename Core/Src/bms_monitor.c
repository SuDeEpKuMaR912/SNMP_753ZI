#include "bms_monitor.h"

#include <stdio.h>
#include <string.h>
#include "lwip/apps/snmp.h"
#include "telnet_server.h"

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

typedef enum
{
    BMS_STATE_IDLE = 0,
    BMS_STATE_WAIT_START,
    BMS_STATE_WAIT_HEADER,
    BMS_STATE_WAIT_BODY,
} BMS_State_t;

static BMS_State_t bms_state = BMS_STATE_IDLE;
static uint8_t  bms_frame[128];
static uint16_t bms_idx = 0;
static uint16_t bms_total_len = 0;
static uint32_t bms_req_timer = 0;
static uint32_t bms_state_timer = 0;

#define BMS_REQUEST_INTERVAL_MS   5000U
#define BMS_BYTE_POLL_TIMEOUT_MS  2U
#define BMS_FRAME_TIMEOUT_MS      1000U

static void BMS_Decode_And_Report(uint8_t *p, uint16_t len);

void BMS_Process(void)
{
    uint8_t ch;

    switch (bms_state)
    {
    case BMS_STATE_IDLE:
        if (HAL_GetTick() - bms_req_timer < BMS_REQUEST_INTERVAL_MS)
        {
            return;
        }
        bms_req_timer = HAL_GetTick();

        while (HAL_UART_Receive(&huart2, &ch, 1, 0) == HAL_OK) { }

        if (HAL_UART_Transmit(&huart2, bms_cmd, sizeof(bms_cmd), 50) != HAL_OK)
        {
            return;
        }

        bms_idx = 0;
        memset(bms_frame, 0, sizeof(bms_frame));
        bms_state_timer = HAL_GetTick();
        bms_state = BMS_STATE_WAIT_START;
        break;

    case BMS_STATE_WAIT_START:
        if (HAL_UART_Receive(&huart2, &ch, 1, BMS_BYTE_POLL_TIMEOUT_MS) == HAL_OK)
        {
            if (ch == 0xDD)
            {
                bms_frame[0] = ch;
                bms_idx = 1;
                bms_state_timer = HAL_GetTick();
                bms_state = BMS_STATE_WAIT_HEADER;
            }
        }
        else if (HAL_GetTick() - bms_state_timer >= BMS_FRAME_TIMEOUT_MS)
        {
            printf("BMS Read Failed\r\n");
            bms_state = BMS_STATE_IDLE;
        }
        break;

    case BMS_STATE_WAIT_HEADER:
        while (bms_idx < 4 &&
               HAL_UART_Receive(&huart2, &bms_frame[bms_idx], 1, BMS_BYTE_POLL_TIMEOUT_MS) == HAL_OK)
        {
            bms_idx++;
        }

        if (bms_idx >= 4)
        {
            uint8_t dataLength = bms_frame[3];
            bms_total_len = 4 + dataLength + 3;

            if (bms_total_len > sizeof(bms_frame))
            {
                printf("Invalid frame length\r\n");
                bms_state = BMS_STATE_IDLE;
            }
            else
            {
                bms_state_timer = HAL_GetTick();
                bms_state = BMS_STATE_WAIT_BODY;
            }
        }
        else if (HAL_GetTick() - bms_state_timer >= BMS_FRAME_TIMEOUT_MS)
        {
            printf("Header receive failed\r\n");
            bms_state = BMS_STATE_IDLE;
        }
        break;

    case BMS_STATE_WAIT_BODY:
        while (bms_idx < bms_total_len &&
               HAL_UART_Receive(&huart2, &bms_frame[bms_idx], 1, BMS_BYTE_POLL_TIMEOUT_MS) == HAL_OK)
        {
            bms_idx++;
        }

        if (bms_idx >= bms_total_len)
        {
            BMS_Decode_And_Report(bms_frame, bms_total_len);
            bms_state = BMS_STATE_IDLE;
        }
        else if (HAL_GetTick() - bms_state_timer >= BMS_FRAME_TIMEOUT_MS)
        {
            printf("Remaining frame receive failed\r\n");
            bms_state = BMS_STATE_IDLE;
        }
        break;
    }
}

static void BMS_Decode_And_Report(uint8_t *p, uint16_t len)
{
    printf("\r\nReceived %u bytes\r\n", len);

    for (uint16_t i = 0; i < len; i++)
        printf("%02X ", p[i]);

    printf("\r\n");

    if (len < 50)
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
        char bms_info[192];

        uint32_t uid0;
        uint32_t uid1;
        uint32_t uid2;

        uint32_t lcgateext;
        uint32_t ippaext;

        uid0 = HAL_GetUIDw0();
        uid1 = HAL_GetUIDw1();
        uid2 = HAL_GetUIDw2();

        lcgateext = Telnet_Get_LCGateExt();
        ippaext = Telnet_Get_IPPAExt();

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

        snprintf(bms_info,
                 sizeof(bms_info),
                 "SOC: %u, State: %s, Voltage: %.2f V, Current: %.2f A, LC Gate Ext: %lu, IPPA Ext: %lu, UUID: %08lX-%08lX-%08lX",
                 soc,
                 state_str,
                 packVoltage / 100.0f,
                 current / 100.0f,
                 (unsigned long)lcgateext,
                 (unsigned long)ippaext,
                 (unsigned long)uid0,
                 (unsigned long)uid1,
                 (unsigned long)uid2);

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
