#include "gpio_control.h"
#include "main.h"
#include "lwip/apps/snmp.h"
#include "telnet_server.h"
#include "string.h"

static uint8_t previous_gpio_state = 0;
static uint8_t gpio_initialized = 0;

/* GPIO trap OID */
static const u32_t gpio_enterprise_oid[] =
{
    1, 3, 6, 1, 4, 1, 12345, 2
};


void GPIO_Control_Init(void)
{
    /* PB6 is configured as input in CubeMX */

    /* Initial LED state */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    /*
     * Store the initial PB6 state.
     * This prevents a trap from being sent immediately at startup.
     */
    previous_gpio_state =
        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;

    gpio_initialized = 1;
}


void GPIO_Control_Process(void)
{
    uint8_t current_gpio_state;

    current_gpio_state =
        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;

    if (current_gpio_state == 1)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    }

    if (gpio_initialized && current_gpio_state != previous_gpio_state)
    {
        struct snmp_obj_id eoid;
        struct snmp_varbind gpio_varbind;

        char gpio_state[10];
        char gpio_info[192];

        uint32_t uid0;
        uint32_t uid1;
        uint32_t uid2;

        uint32_t lcgateext;
        uint32_t ippaext;

        err_t trap_err;


        /* Get STM32 UID */
        uid0 = HAL_GetUIDw0();
        uid1 = HAL_GetUIDw1();
        uid2 = HAL_GetUIDw2();


        /* Get current extension values */
        lcgateext = Telnet_Get_LCGateExt();
        ippaext = Telnet_Get_IPPAExt();


        /* Determine GPIO state */
        if (current_gpio_state == 1)
        {
            snprintf(gpio_state,
                     sizeof(gpio_state),
                     "CLOSE");
        }
        else
        {
            snprintf(gpio_state,
                     sizeof(gpio_state),
                     "OPEN");
        }


        /* Create trap information string */
        snprintf(gpio_info,
                 sizeof(gpio_info),
                 "%s, LC Gate Ext: %lu, IPPA Ext: %lu, UUID: %08lX-%08lX-%08lX",
                 gpio_state,
                 (unsigned long)lcgateext,
                 (unsigned long)ippaext,
                 (unsigned long)uid0,
                 (unsigned long)uid1,
                 (unsigned long)uid2);


        /* Assign enterprise OID */
        snmp_oid_assign(&eoid,
                        gpio_enterprise_oid,
                        sizeof(gpio_enterprise_oid) /
                        sizeof(gpio_enterprise_oid[0]));


        /* Create single varbind */
        gpio_varbind.oid = eoid;
        gpio_varbind.type = SNMP_ASN1_TYPE_OCTET_STRING;
        gpio_varbind.value = gpio_info;
        gpio_varbind.value_len = strlen(gpio_info);
        gpio_varbind.next = NULL;
        gpio_varbind.prev = NULL;


        /* Send trap */
        trap_err = snmp_send_trap(&eoid,
                                  SNMP_GENTRAP_ENTERPRISE_SPECIFIC,
                                  3,
                                  &gpio_varbind);


        if (trap_err == ERR_OK)
        {
            printf("GPIO SNMP TRAP SENT\r\n");
            printf("%s\r\n", gpio_info);
        }
        else
        {
            printf("GPIO SNMP TRAP FAILED: %d\r\n", trap_err);
        }


        /* Store new state */
        previous_gpio_state = current_gpio_state;
    }
}
