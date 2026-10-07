/*
 * lcd1602.h
 *
 *  Created on: 2026. 10. 6.
 *      Author: kccistc
 */

#ifndef LCD1602_H
#define LCD1602_H

#include "main.h"

void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(char *str);

/* 쓰레기통 A/B/C 상태 표시용 */
void LCD_UpdateBins(int a, int b, int c);

#endif
