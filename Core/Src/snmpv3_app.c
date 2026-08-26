#include "snmpv3_app.h"
#include <string.h>
#include "lwip/err.h"
#include "snmp_persist.h"

static const char engine_id[] =
{
    0x80, 0x00, 0x1F, 0x88,
    0x03,
    0x00, 0x80, 0xE1, 0x00,
    0x00, 0x00
};

void snmpv3_get_engine_id(const char **id, u8_t *len)
{
    *id = engine_id;
    *len = sizeof(engine_id);
}

err_t snmpv3_set_engine_id(const char *id, u8_t len)
{
    LWIP_UNUSED_ARG(id);
    LWIP_UNUSED_ARG(len);

    return ERR_OK;
}

static u32_t engine_boots = 0;

err_t snmpv3_app_init(void)
{
    uint32_t boots;

    if (SNMP_Persist_Init(&boots) != HAL_OK)
    {
        return ERR_IF;
    }

    engine_boots = boots;

    return ERR_OK;
}

u32_t snmpv3_get_engine_boots(void)
{
    return engine_boots;
}

void snmpv3_set_engine_boots(u32_t boots)
{
    engine_boots = boots;
}

u32_t snmpv3_get_engine_time(void)
{
    return HAL_GetTick() / 1000;
}

void snmpv3_reset_engine_time(void)
{
    /* HAL_GetTick() naturally continues from the MCU uptime */
}

err_t snmpv3_get_user(const char *username, snmpv3_auth_algo_t *auth_algo, u8_t *auth_key, snmpv3_priv_algo_t *priv_algo, u8_t *priv_key)
{
    if (username == NULL)
    {
        return ERR_ARG;
    }

    /*
     * User 1: lwip
     * noAuthNoPriv
     */
    if (strcmp(username, "lwip") == 0)
    {
        if (auth_algo != NULL)
        {
            *auth_algo = SNMP_V3_AUTH_ALGO_INVAL;
        }

        if (priv_algo != NULL)
        {
            *priv_algo = SNMP_V3_PRIV_ALGO_INVAL;
        }

        return ERR_OK;
    }

    /*
     * User 2: lwipsha
     * SHA authentication, no privacy
     */
    if (strcmp(username, "lwipsha") == 0)
    {
        if (auth_algo != NULL)
        {
            *auth_algo = SNMP_V3_AUTH_ALGO_SHA;
        }

        if (priv_algo != NULL)
        {
            *priv_algo = SNMP_V3_PRIV_ALGO_INVAL;
        }

        if (auth_key != NULL)
        {
            const char *id;
            u8_t id_len;

            snmpv3_get_engine_id(&id, &id_len);

            snmpv3_password_to_key_sha(
                (const u8_t *)"maplesyrup",
                strlen("maplesyrup"),
                (const u8_t *)id,
                id_len,
                auth_key
            );
        }

        return ERR_OK;
    }

    /*
     * User 3: lwippriv
     * SHA authentication, and privacy
     */
    if (strcmp(username, "sudeepk") == 0)
    {
        if (auth_algo != NULL)
        {
            *auth_algo = SNMP_V3_AUTH_ALGO_SHA;
        }

        if (priv_algo != NULL)
        {
            *priv_algo = SNMP_V3_PRIV_ALGO_AES;
        }

        if (auth_key != NULL || priv_key != NULL)
        {
            const char *id;
            u8_t id_len;

            snmpv3_get_engine_id(&id, &id_len);

            /*
             * Generate the localized SHA key.
             * lwIP uses the resulting key for both
             * authentication and AES privacy.
             */

            if (auth_key != NULL)
            {
                snmpv3_password_to_key_sha(
                    (const u8_t *)"coral@auth@123",
                    strlen("coral@auth@123"),
                    (const u8_t *)id,
                    id_len,
                    auth_key
                );
            }

            if (priv_key != NULL)
            {
                snmpv3_password_to_key_sha(
                    (const u8_t *)"coral@priv@123",
                    strlen("coral@priv@123"),
                    (const u8_t *)id,
                    id_len,
                    priv_key
                );
            }
        }

        return ERR_OK;
    }

    return ERR_VAL;
}

u8_t snmpv3_get_amount_of_users(void)
{
    return 3;
}

err_t snmpv3_get_user_storagetype(
    const char *username,
    snmpv3_user_storagetype_t *storagetype)
{
    if (username == NULL || storagetype == NULL)
    {
        return ERR_ARG;
    }

    if (strcmp(username, "lwip") != 0 && strcmp(username, "lwipsha") != 0 && strcmp(username, "sudeepk") != 0)
    {
        return ERR_VAL;
    }

    *storagetype = SNMP_V3_USER_STORAGETYPE_VOLATILE;

    return ERR_OK;
}

err_t snmpv3_get_username(char *username, u8_t index)
{
    if (username == NULL)
    {
        return ERR_ARG;
    }

    if (index == 0)
    {
        strcpy(username, "lwip");
        return ERR_OK;
    }

    if (index == 1)
    {
        strcpy(username, "lwipsha");
        return ERR_OK;
    }

    if (index == 2)
    {
        strcpy(username, "sudeepk");
        return ERR_OK;
    }

    return ERR_VAL;
}


