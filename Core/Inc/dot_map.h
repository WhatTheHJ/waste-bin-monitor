#ifndef DOT_MAP_H
#define DOT_MAP_H

#include <stdint.h>

void DM_Map_Init(void);
void DM_Map_Loop(void);
void DM_Map_SetBinPercent(uint8_t bin, int percent);
int DM_Map_GetBinPercent(uint8_t bin);

#endif /* DOT_MAP_H */
