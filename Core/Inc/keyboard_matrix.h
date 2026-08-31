#ifndef KEYBOARD_MATRIX_H
#define KEYBOARD_MATRIX_H

#include "main.h"

#define NUM_ROWS 5
#define NUM_COLS 6

void Keyboard_Matrix_Init(void);
uint8_t Keyboard_Matrix_Scan(void);

#endif
