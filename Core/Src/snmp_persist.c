#include "snmp_persist.h"

#define SNMP_PERSIST_RECORD_SIZE     32U
#define SNMP_PERSIST_SECTOR_SIZE     (128U * 1024U)

#define SNMP_ENGINE_BOOTS_MAX        2147483647UL

typedef struct
{
    uint32_t magic;
    uint32_t engine_boots;
    uint32_t boots_inverse;
    uint32_t sequence;

    uint32_t reserved[4];
} snmp_persist_record_t;


/*
 * The inverse provides a very simple corruption check.
 *
 * Valid record:
 *
 *     boots        = 10
 *     boots_inverse = ~10
 *
 * If Flash programming is interrupted and the record becomes
 * corrupted, this check will normally fail.
 */
static uint32_t SNMP_Persist_Checksum(uint32_t boots)
{
    return ~boots;
}


/*
 * Check whether a Flash record is valid.
 */
static uint8_t SNMP_Persist_RecordValid(
    const snmp_persist_record_t *record)
{
    if (record->magic != SNMP_PERSIST_MAGIC)
    {
        return 0;
    }

    if (record->engine_boots == 0 ||
        record->engine_boots > SNMP_ENGINE_BOOTS_MAX)
    {
        return 0;
    }

    if (record->boots_inverse !=
        SNMP_Persist_Checksum(record->engine_boots))
    {
        return 0;
    }

    return 1;
}


/*
 * Find the newest valid record.
 */
static const snmp_persist_record_t *
SNMP_Persist_FindLatest(void)
{
    const snmp_persist_record_t *latest = NULL;

    for (uint32_t offset = 0;
         offset < SNMP_PERSIST_SECTOR_SIZE;
         offset += SNMP_PERSIST_RECORD_SIZE)
    {
        const snmp_persist_record_t *record =
            (const snmp_persist_record_t *)
            (SNMP_PERSIST_FLASH_ADDRESS + offset);

        /*
         * Completely erased record.
         * Everything after this point is unused.
         */
        if (record->magic == 0xFFFFFFFFUL)
        {
            break;
        }

        /*
         * Invalid record.
         *
         * We stop here because this can indicate that power
         * was lost while the previous record was being written.
         */
        if (!SNMP_Persist_RecordValid(record))
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
static uint32_t SNMP_Persist_FindFreeAddress(void)
{
    for (uint32_t offset = 0;
         offset < SNMP_PERSIST_SECTOR_SIZE;
         offset += SNMP_PERSIST_RECORD_SIZE)
    {
        const snmp_persist_record_t *record =
            (const snmp_persist_record_t *)
            (SNMP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            return SNMP_PERSIST_FLASH_ADDRESS + offset;
        }
    }

    return 0;
}


/*
 * Write one 32-byte Flash word.
 */
static HAL_StatusTypeDef
SNMP_Persist_WriteRecord(uint32_t address, uint32_t boots)
{
    snmp_persist_record_t record = {0};

    record.magic = SNMP_PERSIST_MAGIC;
    record.engine_boots = boots;
    record.boots_inverse = SNMP_Persist_Checksum(boots);

    /*
     * Sequence isn't currently used for SNMP behavior.
     * Keep it zero for now.
     */
    record.sequence = 0;

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
 * Initialize persistent SNMP engineBoots.
 */
HAL_StatusTypeDef
SNMP_Persist_Init(uint32_t *engine_boots)
{
    if (engine_boots == NULL)
    {
        return HAL_ERROR;
    }


    /*
     * Find the latest valid record.
     */
    const snmp_persist_record_t *latest =
        SNMP_Persist_FindLatest();


    uint32_t boots;


    /*
     * First-ever initialization.
     */
    if (latest == NULL)
    {
        boots = 1;
    }
    else
    {
        /*
         * Prevent overflow.
         */
        if (latest->engine_boots >= SNMP_ENGINE_BOOTS_MAX)
        {
            return HAL_ERROR;
        }

        boots = latest->engine_boots + 1;
    }


    /*
     * Find an unused Flash slot.
     */
    uint32_t write_address =
        SNMP_Persist_FindFreeAddress();


    /*
     * Sector is full.
     */
    if (write_address == 0)
    {
        FLASH_EraseInitTypeDef erase = {0};

        uint32_t sector_error = 0;


        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = FLASH_BANK_2;
        erase.Sector = FLASH_SECTOR_7;
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


        /*
         * Sector is now empty.
         */
        write_address =
            SNMP_PERSIST_FLASH_ADDRESS;
    }


    /*
     * Store the new boot count.
     */
    HAL_StatusTypeDef status =
        SNMP_Persist_WriteRecord(
            write_address,
            boots
        );


    if (status != HAL_OK)
    {
        return status;
    }


    /*
     * Return the value that was successfully
     * written to Flash.
     */
    *engine_boots = boots;

    return HAL_OK;
}
