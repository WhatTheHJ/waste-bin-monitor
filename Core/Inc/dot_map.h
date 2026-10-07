#ifndef DOT_MAP_H
#define DOT_MAP_H

#include <stdint.h>

void DM_Map_Init(void);
void DM_Map_Loop(void);

void DM_Map_SetBinPercent(uint8_t bin, int percent);
int DM_Map_GetBinPercent(uint8_t bin);

/* 사용자 수거 요청 위치를 duration_ms 동안 깜빡임
 * bin: 0=A, 1=B, 2=C */
void DM_Map_RequestAlert(uint8_t bin, uint32_t duration_ms);

#endif /* DOT_MAP_H */
