#include "snmp_persist.h"
#include "string.h"

#define SNMP_PERSIST_RECORD_SIZE  32U

typedef struct
{
    uint32_t magic;
    uint32_t engine_boots;
    uint32_t boots_inverse;

    uint32_t reserved[5];
} snmp_persist_record_t;

static uint32_t SNMP_Persist_Checksum(uint32_t boots)
{
    return ~boots;
}

static int SNMP_Persist_RecordValid(const snmp_persist_record_t *record)
{
    if (record->magic != SNMP_PERSIST_MAGIC)
    {
        return 0;
    }

    if (record->boots_inverse != SNMP_Persist_Checksum(record->engine_boots))
    {
        return 0;
    }

    return 1;
}

static const snmp_persist_record_t *
SNMP_Persist_FindLatest(void)
{
    const snmp_persist_record_t *record;

    for (uint32_t offset = 0;
         offset < 128U * 1024U;
         offset += SNMP_PERSIST_RECORD_SIZE)
    {
        record = (const snmp_persist_record_t *)
                 (SNMP_PERSIST_FLASH_ADDRESS + offset);

        /*
         * Blank Flash means there are no more records.
         */
        if (record->magic == 0xFFFFFFFFUL)
        {
            break;
        }

        if (!SNMP_Persist_RecordValid(record))
        {
            break;
        }
    }

    /*
     * Walk again to find the last valid record.
     */
    const snmp_persist_record_t *latest = NULL;

    for (uint32_t offset = 0;
         offset < 128U * 1024U;
         offset += SNMP_PERSIST_RECORD_SIZE)
    {
        record = (const snmp_persist_record_t *)
                 (SNMP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            break;
        }

        if (!SNMP_Persist_RecordValid(record))
        {
            break;
        }

        latest = record;
    }

    return latest;
}

static HAL_StatusTypeDef
SNMP_Persist_WriteRecord(uint32_t address, uint32_t boots)
{
    snmp_persist_record_t record = {0};

    record.magic = SNMP_PERSIST_MAGIC;
    record.engine_boots = boots;
    record.boots_inverse = SNMP_Persist_Checksum(boots);

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

HAL_StatusTypeDef
SNMP_Persist_Init(uint32_t *engine_boots)
{
    if (engine_boots == NULL)
    {
        return HAL_ERROR;
    }

    const snmp_persist_record_t *latest =
        SNMP_Persist_FindLatest();

    uint32_t boots;

    if (latest == NULL)
    {
        /*
         * First boot ever.
         */
        boots = 1;
    }
    else
    {
        /*
         * Every reboot increments engineBoots.
         */
        boots = latest->engine_boots + 1;
    }

    /*
     * Find first unused 32-byte slot.
     */
    uint32_t write_address = SNMP_PERSIST_FLASH_ADDRESS;

    for (uint32_t offset = 0;
         offset < 128U * 1024U;
         offset += SNMP_PERSIST_RECORD_SIZE)
    {
        const snmp_persist_record_t *record =
            (const snmp_persist_record_t *)
            (SNMP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            write_address = SNMP_PERSIST_FLASH_ADDRESS + offset;
            break;
        }
    }

    /*
     * If sector is full, erase it and start again.
     */
    if (write_address >= SNMP_PERSIST_FLASH_ADDRESS +
                         128U * 1024U)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_error = 0;

        HAL_FLASH_Unlock();

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = FLASH_BANK_2;
        erase.Sector = FLASH_SECTOR_7;
        erase.NbSectors = 1;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        HAL_StatusTypeDef status =
            HAL_FLASHEx_Erase(&erase, &sector_error);

        HAL_FLASH_Lock();

        if (status != HAL_OK)
        {
            return status;
        }

        write_address = SNMP_PERSIST_FLASH_ADDRESS;
    }

    /*
     * Store the new boot count.
     */
    HAL_StatusTypeDef status =
        SNMP_Persist_WriteRecord(write_address, boots);

    if (status == HAL_OK)
    {
        *engine_boots = boots;
    }

    return status;
}
