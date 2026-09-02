#include "bms_monitor.h"

#include <stdio.h>
#include <string.h>
#include "lwip/apps/snmp.h"

/* USART2 is used for BMS communication */
extern UART_HandleTypeDef huart2;

/* BMS command */
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

/* Read incoming BMS frame */
void Read_BMS_Data(void)
{
    uint8_t ch;
    uint16_t len = 0;
    uint8_t frame[128];
    uint8_t *p = frame;

    memset(frame, 0, sizeof(frame));

    /* Flush old UART bytes */
    while(HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK);

    /* Send command */
    HAL_UART_Transmit(&huart2,
                      bms_cmd,
                      sizeof(bms_cmd),
                      HAL_MAX_DELAY);

    /* Wait for frame start */
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

    /* Receive command, status and length */
    if(HAL_UART_Receive(&huart2, &frame[1], 3, 1000) != HAL_OK)
    {
        printf("Header receive failed\r\n");
        return;
    }

    /* Now frame[3] contains the data length */
    uint8_t dataLength = frame[3];

    /* Total frame length = 4 + dataLength + 3 */
    uint16_t totalLength = 4 + dataLength + 3;

    if(totalLength > sizeof(frame))
    {
        printf("Invalid frame length\r\n");
        return;
    }

    /* Receive remaining bytes */
    if(HAL_UART_Receive(&huart2,
                        &frame[4],
                        totalLength - 4,
                        1000) != HAL_OK)
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
    uint16_t prodDate;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t swVersion;
    uint8_t soc;
    uint8_t fetStatus;
    uint8_t cellCount;
    uint8_t ntcCount;

    float temp1 = 0;
    float temp2 = 0;
    float temp3 = 0;

    /* Decode frame */

    packVoltage = ((uint16_t)p[4] << 8) | p[5];
    current     = ((int16_t)p[6] << 8) | p[7];
    remainCap   = ((uint16_t)p[8] << 8) | p[9];
    ratedCap    = ((uint16_t)p[10] << 8) | p[11];
    cycles      = ((uint16_t)p[12] << 8) | p[13];
    prodDate    = ((uint16_t)p[14] << 8) | p[15];

    day   = prodDate & 0x1F;
    month = (prodDate >> 5) & 0x0F;
    year  = 2000 + (prodDate >> 9);
    swVersion   = p[22];
    soc         = p[23];
    fetStatus   = p[24];
    cellCount   = p[25];
    ntcCount    = p[26];

    /* Temperatures */

    if(ntcCount >= 1)
    {
        uint16_t raw =
            ((uint16_t)p[27] << 8) | p[28];

        temp1 = (raw - 2731) / 10.0f;
    }

    if(ntcCount >= 2)
    {
        uint16_t raw =
            ((uint16_t)p[29] << 8) | p[30];

        temp2 = (raw - 2731) / 10.0f;
    }

    if(ntcCount >= 3)
    {
        uint16_t raw =
            ((uint16_t)p[31] << 8) | p[32];

        temp3 = (raw - 2731) / 10.0f;
    }

    /* Check for battery percentage change */
    if(!soc_initialized)
    {
        previous_soc = soc;
        soc_initialized = 1;
    }
    else if(soc != previous_soc)
    {
        struct snmp_obj_id eoid;

        /* --------------------------------------------------
         * Trap data strings
         * -------------------------------------------------- */

        char soc_str[16];
        char state_str[20];
        char voltage_str[24];
        char current_str[24];

        /* SOC */
        snprintf(soc_str,
                 sizeof(soc_str),
                 "SOC: %u",
                 soc);

        /* Battery state */
        if(current > 0)
        {
            snprintf(state_str,
                     sizeof(state_str),
                     "State: CHARGING");
        }
        else if(current < 0)
        {
            snprintf(state_str,
                     sizeof(state_str),
                     "State: DISCHARGING");
        }
        else
        {
            snprintf(state_str,
                     sizeof(state_str),
                     "State: IDLE");
        }

        /* Voltage */
        snprintf(voltage_str,
                 sizeof(voltage_str),
                 "Voltage: %.2f V",
                 packVoltage / 100.0f);

        /* Current */
        snprintf(current_str,
                 sizeof(current_str),
                 "Current: %.2f A",
                 current / 100.0f);


        /* --------------------------------------------------
         * Enterprise OID
         * -------------------------------------------------- */

        snmp_oid_assign(&eoid,
                        bms_enterprise_oid,
                        sizeof(bms_enterprise_oid) /
                        sizeof(bms_enterprise_oid[0]));


        /* --------------------------------------------------
         * SOC OID
         *
         * 1.3.6.1.4.1.12345.1.1
         * -------------------------------------------------- */

        static const u32_t soc_oid_array[] =
        {
            1, 3, 6, 1, 4, 1, 12345, 1, 1
        };

        struct snmp_obj_id soc_oid;

        snmp_oid_assign(&soc_oid,
                        soc_oid_array,
                        sizeof(soc_oid_array) /
                        sizeof(soc_oid_array[0]));


        /* --------------------------------------------------
         * State OID
         *
         * 1.3.6.1.4.1.12345.1.2
         * -------------------------------------------------- */

        static const u32_t state_oid_array[] =
        {
            1, 3, 6, 1, 4, 1, 12345, 1, 2
        };

        struct snmp_obj_id state_oid;

        snmp_oid_assign(&state_oid,
                        state_oid_array,
                        sizeof(state_oid_array) /
                        sizeof(state_oid_array[0]));


        /* --------------------------------------------------
         * Voltage OID
         *
         * 1.3.6.1.4.1.12345.1.3
         * -------------------------------------------------- */

        static const u32_t voltage_oid_array[] =
        {
            1, 3, 6, 1, 4, 1, 12345, 1, 3
        };

        struct snmp_obj_id voltage_oid;

        snmp_oid_assign(&voltage_oid,
                        voltage_oid_array,
                        sizeof(voltage_oid_array) /
                        sizeof(voltage_oid_array[0]));


        /* --------------------------------------------------
         * Current OID
         *
         * 1.3.6.1.4.1.12345.1.4
         * -------------------------------------------------- */

        static const u32_t current_oid_array[] =
        {
            1, 3, 6, 1, 4, 1, 12345, 1, 4
        };

        struct snmp_obj_id current_oid;

        snmp_oid_assign(&current_oid,
                        current_oid_array,
                        sizeof(current_oid_array) /
                        sizeof(current_oid_array[0]));


        /* --------------------------------------------------
         * Create varbinds
         * -------------------------------------------------- */

        struct snmp_varbind soc_varbind;
        struct snmp_varbind state_varbind;
        struct snmp_varbind voltage_varbind;
        struct snmp_varbind current_varbind;


        /* SOC */
        soc_varbind.oid = soc_oid;
        soc_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        soc_varbind.value = soc_str;
        soc_varbind.value_len = strlen(soc_str);
        soc_varbind.next = &state_varbind;
        soc_varbind.prev = NULL;


        /* State */
        state_varbind.oid = state_oid;
        state_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        state_varbind.value = state_str;
        state_varbind.value_len = strlen(state_str);
        state_varbind.next = &voltage_varbind;
        state_varbind.prev = &soc_varbind;


        /* Voltage */
        voltage_varbind.oid = voltage_oid;
        voltage_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        voltage_varbind.value = voltage_str;
        voltage_varbind.value_len = strlen(voltage_str);
        voltage_varbind.next = &current_varbind;
        voltage_varbind.prev = &state_varbind;


        /* Current */
        current_varbind.oid = current_oid;
        current_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        current_varbind.value = current_str;
        current_varbind.value_len = strlen(current_str);
        current_varbind.next = NULL;
        current_varbind.prev = &voltage_varbind;


        /* --------------------------------------------------
         * Send SNMP trap
         * -------------------------------------------------- */

        err_t trap_err;

        trap_err = snmp_send_trap(&eoid,
                                  SNMP_GENTRAP_ENTERPRISE_SPECIFIC,
                                  2,
                                  &soc_varbind);


        if(trap_err == ERR_OK)
        {
            printf("SNMP TRAP SENT\r\n");
            printf("%s\r\n", soc_str);
            printf("%s\r\n", state_str);
            printf("%s\r\n", voltage_str);
            printf("%s\r\n", current_str);
        }
        else
        {
            printf("SNMP TRAP FAILED: %d\r\n", trap_err);
        }


        /* Update previous SOC */
        previous_soc = soc;
    }

    printf("\r\n");
    printf("========================================\r\n");

    printf("Pack Voltage     : %.2f V\r\n",
           packVoltage / 100.0f);

    printf("Current          : %.2f A\r\n",
           current / 100.0f);

    if(current > 0)
    {
        printf("Battery Status   : CHARGING\r\n");
    }

    printf("Remaining Cap    : %.2f Ah\r\n",
           remainCap / 100.0f);

    printf("Nominal Cap      : %.2f Ah\r\n",
           ratedCap / 100.0f);

    printf("Cycle Count      : %u\r\n",
           cycles);

    printf("SOC              : %u %%\r\n",
           soc);

    printf("Checksum         : %02X %02X\r\n",
           p[len - 3],
           p[len - 2]);

    printf("End Byte         : %02X\r\n",
           p[len - 1]);

    printf("========================================\r\n");
}
