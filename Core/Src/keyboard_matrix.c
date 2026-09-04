#include "keyboard_matrix.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

#define NUM_ROWS 5
#define NUM_COLS 6

static const uint8_t keyMap[NUM_ROWS][NUM_COLS] =
{
    {0x3A, 0x3B, 0x42, 0x00, 0x6A, 0x6D},     //f1   f2   f9   CallAns   f15   f18
    {0x3C, 0x3D, 0x43, 0x00, 0x6B, 0x70},     //f3   f4   f10  CallCut   f16   f21
    {0x3E, 0x3F, 0x00, 0x00, 0x6C, 0x71},     //f5   f6   V-   Mute      f17   f22
    {0x40, 0x41, 0x00, 0x00, 0x52, 0x00},     //f7   f8   V+   Spk       Up    ---
    {0x44, 0x45, 0x68, 0x69, 0x51, 0x00}      //f11  f12  f13  f14       Down  ---
};

static GPIO_TypeDef *row_ports[NUM_ROWS] =
{
    GPIOC,
    GPIOC,
    GPIOC,
    GPIOC,
    GPIOC
};

static uint16_t row_pins[NUM_ROWS] =
{
    GPIO_PIN_6,
	GPIO_PIN_7,
	GPIO_PIN_8,
	GPIO_PIN_9,
	GPIO_PIN_10
};

static GPIO_TypeDef *col_ports[NUM_COLS] =
{
    GPIOC,
    GPIOC,
    GPIOD,
    GPIOD,
    GPIOG,
    GPIOG
};

static uint16_t col_pins[NUM_COLS] =
{
    GPIO_PIN_11,
    GPIO_PIN_12,
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_0,
    GPIO_PIN_1
};

static uint8_t keyboardReport[8] =
{
    0x01,   /* Report ID */
    0x00,   /* Modifier */
    0x00,   /* Reserved */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
};

static uint8_t currentKey = 0x00;
static uint8_t previousKey = 0x00;

void Keyboard_Matrix_Init(void)
{
    uint8_t i;

    for (i = 0; i < NUM_ROWS; i++)
    {
        HAL_GPIO_WritePin(row_ports[i], row_pins[i], GPIO_PIN_SET);
    }
}

static uint8_t Keyboard_Matrix_Scan(void)
{
    uint8_t keyRow = 0xFF;
    uint8_t keyCol = 0xFF;

    for (uint8_t row = 0; row < NUM_ROWS; row++)
    {
        /* Set all rows HIGH */
        for (uint8_t r = 0; r < NUM_ROWS; r++)
        {
            HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
        }

        /* Activate current row */
        HAL_GPIO_WritePin(row_ports[row], row_pins[row], GPIO_PIN_RESET);

        /* Small settling delay */
        for (volatile uint32_t i = 0; i < 200; i++);

        /* Read columns */
        for (uint8_t col = 0; col < NUM_COLS; col++)
        {
            if (HAL_GPIO_ReadPin(col_ports[col], col_pins[col]) == GPIO_PIN_RESET)
            {
                keyRow = row;
                keyCol = col;
                break;
            }
        }

        if (keyRow != 0xFF)
        {
            break;
        }
    }

    if (keyRow == 0xFF)
    {
        return 0x00;
    }

    return keyMap[keyRow][keyCol];
}

static void Keyboard_SendKey(uint8_t key)
{
    keyboardReport[3] = key;

    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);

    USBD_HID_SendReport(&hUsbDeviceFS, keyboardReport, sizeof(keyboardReport));
}

static void Keyboard_SendRelease(void)
{
    keyboardReport[3] = 0x00;

    while (((USBD_HID_HandleTypeDef *) hUsbDeviceFS.pClassData)->state == HID_BUSY);

    USBD_HID_SendReport(&hUsbDeviceFS, keyboardReport, sizeof(keyboardReport));
}

void Keyboard_Matrix_Process(void)
{
    currentKey = Keyboard_Matrix_Scan();

    /*
     * New key pressed.
     */
    if (currentKey != 0x00 && previousKey == 0x00)
    {
        Keyboard_SendKey(currentKey);

        previousKey = currentKey;
    }

    /*
     * Key released.
     */
    if (currentKey == 0x00 && previousKey != 0x00)
    {
        Keyboard_SendRelease();

        previousKey = 0x00;
    }
}
