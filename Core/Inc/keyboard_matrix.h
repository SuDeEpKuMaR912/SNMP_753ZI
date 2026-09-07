#ifndef KEYBOARD_MATRIX_H
#define KEYBOARD_MATRIX_H

#include "main.h"

void Keyboard_Matrix_Init(void);
void Keyboard_Matrix_Process(void);
void Keyboard_Matrix_EXTI_Callback(uint16_t GPIO_Pin);

#endif
