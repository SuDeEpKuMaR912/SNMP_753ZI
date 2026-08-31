#include "ip_persist.h"

#define IP_PERSIST_RECORD_SIZE    32U
#define IP_PERSIST_SECTOR_SIZE    (128U * 1024U)

typedef struct
{
    uint32_t magic;
    uint32_t ip_address;
    uint32_t ip_inverse;
    uint32_t sequence;
    uint32_t reserved[4];

} ip_persist_record_t;


/*
 * Simple corruption check.
 */
static uint32_t IP_Persist_Checksum(uint32_t ip)
{
    return ~ip;
}


/*
 * Check whether a Flash record is valid.
 */
static uint8_t IP_Persist_RecordValid(
    const ip_persist_record_t *record)
{
    if (record->magic != IP_PERSIST_MAGIC)
    {
        return 0;
    }

    if (record->ip_inverse !=
        IP_Persist_Checksum(record->ip_address))
    {
        return 0;
    }

    return 1;
}


/*
 * Find the newest valid IP record.
 */
static const ip_persist_record_t *
IP_Persist_FindLatest(void)
{
    const ip_persist_record_t *latest = NULL;

    for (uint32_t offset = 0;
         offset < IP_PERSIST_SECTOR_SIZE;
         offset += IP_PERSIST_RECORD_SIZE)
    {
        const ip_persist_record_t *record =
            (const ip_persist_record_t *)
            (IP_PERSIST_FLASH_ADDRESS + offset);

        /*
         * Empty Flash.
         */
        if (record->magic == 0xFFFFFFFFUL)
        {
            break;
        }

        /*
         * Stop if a corrupted/incomplete record
         * is encountered.
         */
        if (!IP_Persist_RecordValid(record))
        {
            break;
        }

        latest = record;
    }

    return latest;
}


/*
 * Find the first unused Flash record slot.
 */
static uint32_t IP_Persist_FindFreeAddress(void)
{
    for (uint32_t offset = 0;
         offset < IP_PERSIST_SECTOR_SIZE;
         offset += IP_PERSIST_RECORD_SIZE)
    {
        const ip_persist_record_t *record =
            (const ip_persist_record_t *)
            (IP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            return IP_PERSIST_FLASH_ADDRESS + offset;
        }
    }

    return 0;
}


/*
 * Write one 32-byte Flash record.
 */
static HAL_StatusTypeDef
IP_Persist_WriteRecord(uint32_t address,
                       uint32_t ip_address,
                       uint32_t sequence)
{
    ip_persist_record_t record = {0};

    record.magic = IP_PERSIST_MAGIC;
    record.ip_address = ip_address;
    record.ip_inverse = IP_Persist_Checksum(ip_address);
    record.sequence = sequence;

    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    status = HAL_FLASH_Program(
        FLASH_TYPEPROGRAM_FLASHWORD,
        address,
        (uint32_t)&record
    );

    HAL_FLASH_Lock();

    return status;
}


/*
 * Load persistent IP address.
 *
 * If no valid IP exists in Flash, use default_ip.
 */
HAL_StatusTypeDef
IP_Persist_Init(ip4_addr_t *ip,
                const ip4_addr_t *default_ip)
{
    if (ip == NULL || default_ip == NULL)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest =
        IP_Persist_FindLatest();

    if (latest == NULL)
    {
        /*
         * No saved IP exists.
         * Use firmware default.
         */
        *ip = *default_ip;

        return HAL_OK;
    }

    /*
     * Load saved IP.
     */
    ip->addr = latest->ip_address;

    return HAL_OK;
}


/*
 * Save a new IP address to Flash.
 */
HAL_StatusTypeDef
IP_Persist_Save(const ip4_addr_t *ip)
{
    if (ip == NULL)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest =
        IP_Persist_FindLatest();

    /*
     * Don't write Flash if the IP hasn't changed.
     */
    if (latest != NULL &&
        latest->ip_address == ip->addr)
    {
        return HAL_OK;
    }

    uint32_t write_address =
        IP_Persist_FindFreeAddress();

    /*
     * Sector full.
     * Erase Sector 6 and start again.
     */
    if (write_address == 0)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_error = 0;

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = FLASH_BANK_2;
        erase.Sector = FLASH_SECTOR_6;
        erase.NbSectors = 1;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        HAL_FLASH_Unlock();

        HAL_StatusTypeDef status =
            HAL_FLASHEx_Erase(
                &erase,
                &sector_error
            );

        HAL_FLASH_Lock();

        if (status != HAL_OK)
        {
            return status;
        }

        write_address =
            IP_PERSIST_FLASH_ADDRESS;
    }

    uint32_t sequence = 1;

    if (latest != NULL)
    {
        sequence = latest->sequence + 1;
    }

    return IP_Persist_WriteRecord(
        write_address,
        ip->addr,
        sequence
    );
}
