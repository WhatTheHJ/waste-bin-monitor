#ifndef BUZZER_H
#define BUZZER_H

#include "main.h"

void Buzzer_Init(void);
void Buzzer_Loop(void);

/* 사용자 수거 요청용 수동 부저 */
void Buzzer_ManualBeep(uint32_t ms);

#endif /* BUZZER_H */
