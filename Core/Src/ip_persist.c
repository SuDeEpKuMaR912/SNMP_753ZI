#include "ip_persist.h"

#define IP_PERSIST_RECORD_SIZE    32U
#define IP_PERSIST_SECTOR_SIZE    (128U * 1024U)

typedef struct
{
    uint32_t magic;
    uint32_t ip_address;
    uint32_t ip_inverse;
    uint32_t sequence;
    uint32_t mode;
    uint32_t reserved[3];
} ip_persist_record_t;

static uint32_t IP_Persist_Checksum(uint32_t ip)
{
    return ~ip;
}

static uint8_t IP_Persist_ModeValid(uint32_t mode)
{
    return (mode == IP_MODE_STATIC || mode == IP_MODE_DHCP);
}

static uint8_t IP_Persist_RecordValid(const ip_persist_record_t *record)
{
    if (record->magic != IP_PERSIST_MAGIC)
    {
        return 0;
    }

    if (record->ip_inverse != IP_Persist_Checksum(record->ip_address))
    {
        return 0;
    }

    if (!IP_Persist_ModeValid(record->mode))
    {
        return 0;
    }

    return 1;
}

static const ip_persist_record_t * IP_Persist_FindLatest(void)
{
    const ip_persist_record_t *latest = NULL;

    for (uint32_t offset = 0; offset < IP_PERSIST_SECTOR_SIZE; offset += IP_PERSIST_RECORD_SIZE)
    {
        const ip_persist_record_t *record = (const ip_persist_record_t *)(IP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            break;
        }

        if (!IP_Persist_RecordValid(record))
        {
            break;
        }

        latest = record;
    }

    return latest;
}

static uint32_t IP_Persist_FindFreeAddress(void)
{
    for (uint32_t offset = 0; offset < IP_PERSIST_SECTOR_SIZE; offset += IP_PERSIST_RECORD_SIZE)
    {
        const ip_persist_record_t *record = (const ip_persist_record_t *)(IP_PERSIST_FLASH_ADDRESS + offset);

        if (record->magic == 0xFFFFFFFFUL)
        {
            return IP_PERSIST_FLASH_ADDRESS + offset;
        }
    }

    return 0;
}

static HAL_StatusTypeDef IP_Persist_WriteRecord(uint32_t address, uint32_t ip_address, IP_Mode_t mode, uint32_t sequence)
{
    ip_persist_record_t record = {0};

    record.magic = IP_PERSIST_MAGIC;
    record.ip_address = ip_address;
    record.ip_inverse = IP_Persist_Checksum(ip_address);
    record.sequence = sequence;
    record.mode = mode;

    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address, (uint32_t)&record);

    HAL_FLASH_Lock();

    return status;
}

HAL_StatusTypeDef IP_Persist_Init(ip4_addr_t *ip, const ip4_addr_t *default_ip)
{
    if (ip == NULL || default_ip == NULL)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest = IP_Persist_FindLatest();

    if (latest == NULL)
    {
        *ip = *default_ip;

        return HAL_OK;
    }

    ip->addr = latest->ip_address;

    return HAL_OK;
}

HAL_StatusTypeDef IP_Persist_Save(const ip4_addr_t *ip)
{
    if (ip == NULL)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest = IP_Persist_FindLatest();

    uint32_t write_address = IP_Persist_FindFreeAddress();

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

        HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &sector_error);

        HAL_FLASH_Lock();

        if (status != HAL_OK)
        {
            return status;
        }

        write_address = IP_PERSIST_FLASH_ADDRESS;
    }

    uint32_t sequence = 1;

    if (latest != NULL)
    {
        sequence = latest->sequence + 1;
    }

    return IP_Persist_WriteRecord(write_address, ip->addr, IP_MODE_STATIC, sequence);
}

HAL_StatusTypeDef IP_Persist_Save_Mode(const ip4_addr_t *ip, IP_Mode_t mode)
{
    if (ip == NULL)
    {
        return HAL_ERROR;
    }

    if (mode != IP_MODE_STATIC && mode != IP_MODE_DHCP)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest = IP_Persist_FindLatest();

    uint32_t write_address = IP_Persist_FindFreeAddress();

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

        HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &sector_error);

        HAL_FLASH_Lock();

        if (status != HAL_OK)
        {
            return status;
        }

        write_address = IP_PERSIST_FLASH_ADDRESS;
    }

    uint32_t sequence = 1;

    if (latest != NULL)
    {
        sequence = latest->sequence + 1;
    }

    return IP_Persist_WriteRecord(write_address, ip->addr, mode, sequence);
}

HAL_StatusTypeDef IP_Persist_Load_Mode(IP_Mode_t *mode)
{
    if (mode == NULL)
    {
        return HAL_ERROR;
    }

    const ip_persist_record_t *latest = IP_Persist_FindLatest();

    if (latest == NULL)
    {
        *mode = IP_MODE_STATIC;
        return HAL_OK;
    }

    *mode = (IP_Mode_t)latest->mode;

    return HAL_OK;
}
