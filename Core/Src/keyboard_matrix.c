#include "keyboard_matrix.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

#define NUM_ROWS 5
#define NUM_COLS 6

static const uint8_t keyMap[NUM_ROWS][NUM_COLS] =
{
    {0x3A, 0x3B, 0x44, 0x45, 0x70, 0xE9},     // KR1: F1  F2  F11 F12 F21 V+
    {0x3C, 0x3D, 0x68, 0x69, 0xE2, 0xEA},     // KR2: F3  F4  F13 F14 SPK V-
    {0x3E, 0x3F, 0x6A, 0x6B, 0x72, 0x52},     // KR3: F5  F6  F15 F16 MUTE F_UP
    {0x40, 0x41, 0x6C, 0x6D, 0x2C, 0x51},     // KR4: F7  F8  F17 F18 G_CALL F_DN
    {0x42, 0x43, 0x6E, 0x6F, 0x20, 0x26}      // KR5: F9  F10 F19 F20 CALL_ANS CALL_CUT
};

static GPIO_TypeDef *row_ports[NUM_ROWS] =
{
    GPIOE,
    GPIOE,
    GPIOE,
    GPIOE,
    GPIOF
};

static uint16_t row_pins[NUM_ROWS] =
{
    GPIO_PIN_3,
	GPIO_PIN_4,
	GPIO_PIN_5,
	GPIO_PIN_6,
	GPIO_PIN_10
};

static GPIO_TypeDef *col_ports[NUM_COLS] =
{
    GPIOF,
    GPIOF,
    GPIOF,
    GPIOF,
    GPIOF,
    GPIOF
};

static uint16_t col_pins[NUM_COLS] =
{
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_2,
    GPIO_PIN_3,
    GPIO_PIN_4,
    GPIO_PIN_5
};

static uint8_t keyboardReport[8] =
{
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
};

static uint8_t consumerReport[3] =
{
    0x02,
    0x00,
    0x00
};

static uint8_t telephonyReport[2] =
{
    0x06,
    0x00
};

static uint8_t currentKey = 0x00;
static uint8_t previousKey = 0x00;
static volatile uint8_t scanKey = 0x00;
static volatile uint8_t scanReady = 0;
static volatile uint8_t active_row = 0;

void Keyboard_Matrix_Init(void)
{
    uint8_t i;

    for (i = 0; i < NUM_ROWS; i++)
    {
        HAL_GPIO_WritePin(row_ports[i],
                          row_pins[i],
                          GPIO_PIN_SET);
    }

    active_row = 0;
    scanKey = 0x00;
    scanReady = 0;
    previousKey = 0x00;
}

static uint8_t Keyboard_Matrix_Scan(void)
{
    for (uint8_t col = 0; col < NUM_COLS; col++)
    {
        if (HAL_GPIO_ReadPin(col_ports[col], col_pins[col]) == GPIO_PIN_RESET)
        {
            return keyMap[active_row][col];
        }
    }

    return 0x00;
}

static void Keyboard_SendConsumer(uint16_t usage)
{
    consumerReport[0] = 0x02;
    consumerReport[1] = usage & 0xFF;
    consumerReport[2] = (usage >> 8) & 0xFF;

    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);

    USBD_HID_SendReport(&hUsbDeviceFS, consumerReport, sizeof(consumerReport));
}

static void Keyboard_SendTelephony(uint8_t usage)
{
    telephonyReport[0] = 0x06;

    if (usage == 0x20)
    {
        // Hook Switch - Call Answer
        telephonyReport[1] = 0x01;
    }
    else if (usage == 0x26)
    {
        // Drop - Call Cut
        telephonyReport[1] = 0x02;
    }
    else if (usage == 0x2C)
    {
        // Conference - Group Call
        telephonyReport[1] = 0x08;
    }
    else
    {
        return;
    }

    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);
    USBD_HID_SendReport(&hUsbDeviceFS, telephonyReport, sizeof(telephonyReport));
}

static void Keyboard_SendKey(uint8_t key)
{
    keyboardReport[3] = key;

    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);
    USBD_HID_SendReport(&hUsbDeviceFS, keyboardReport, sizeof(keyboardReport));
}

static void Keyboard_SendRelease(void)
{
	if (previousKey == 0xE2 || previousKey == 0xEA || previousKey == 0xE9)
	{
		consumerReport[0] = 0x02;
		consumerReport[1] = 0x00;
		consumerReport[2] = 0x00;

		while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);
		USBD_HID_SendReport(&hUsbDeviceFS, consumerReport, sizeof(consumerReport));
	}
	else if (previousKey == 0x20 || previousKey == 0x26 || previousKey == 0x2C)
	{
		telephonyReport[0] = 0x06;
		telephonyReport[1] = 0x00;

		while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);
		USBD_HID_SendReport(&hUsbDeviceFS, telephonyReport, sizeof(telephonyReport));
	}
	else
	{
		keyboardReport[3] = 0x00;

	    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY)
	    {
	    }
		USBD_HID_SendReport(&hUsbDeviceFS, keyboardReport, sizeof(keyboardReport));
	}
}

void Keyboard_Matrix_Process(void)
{
	if (!scanReady)
	{
	    return;
	}

	scanReady = 0;

	currentKey = scanKey;

    if (currentKey != 0x00 && previousKey == 0x00)
    {
        if (currentKey == 0xEA || currentKey == 0xE2 || currentKey == 0xE9)
        {
            Keyboard_SendConsumer(currentKey);
        }
        else if (currentKey == 0x20 || currentKey == 0x26 || currentKey == 0x2C)
        {
        	Keyboard_SendTelephony(currentKey);
        }
        else
        {
            Keyboard_SendKey(currentKey);
        }

        previousKey = currentKey;
    }

    if (currentKey == 0x00 && previousKey != 0x00)
    {
    	Keyboard_SendRelease();
        previousKey = 0x00;
    }
}

void Keyboard_Matrix_TimerCallback(void)
{
    /* Set all rows HIGH */
    for (uint8_t row = 0; row < NUM_ROWS; row++)
    {
        HAL_GPIO_WritePin(row_ports[row], row_pins[row], GPIO_PIN_SET);
    }

    /* Start a new complete scan */
    if (active_row == 0)
    {
        scanKey = 0x00;
    }

    /* Activate current row */
    HAL_GPIO_WritePin(row_ports[active_row], row_pins[active_row], GPIO_PIN_RESET);

    /* Scan current row */
    uint8_t key = Keyboard_Matrix_Scan();

    /* Keep the detected key until the complete matrix is scanned */
    if (scanKey == 0x00 && key != 0x00)
    {
        scanKey = key;
    }

    /* Move to next row */
    active_row++;

    /* Complete 5-row scan */
    if (active_row >= NUM_ROWS)
    {
        active_row = 0;
        scanReady = 1;
    }
}
