/*
 * Buzzer.c
 *
 *  Created on: 2026. 10. 6.
 *      Author: kccistc
 *
 * 포화도 90% 이상인 쓰레기통이 하나라도 있으면 부저를 울린다 (부저 1개).
 * 포화도 값은 dot_map.c가 들고 있는 값을 DM_Map_GetBinPercent()로 읽는다.
 *
 * 사용법 (main.c USER CODE 구역)
 *   USER CODE BEGIN Includes : #include "buzzer.h"
 *   USER CODE BEGIN 2        : Buzzer_Init();    (DM_Map_Init() 다음)
 *   USER CODE BEGIN 3        : Buzzer_Loop();    (while(1) 안에서 계속)
 *
 * 배선 (능동 부저 기준)
 *   부저 + (긴 다리, 또는 모듈 S/I/O) → STM32 A0 (PA0)
 *   부저 - (짧은 다리, 또는 모듈 GND) → GND
 *   모듈에 VCC 핀이 있으면 3.3V
 *
 * 소리 조절
 *   PA0을 TIM2 채널 1 PWM으로 빠르게 켰다 껐다 해서 크기와 음색을 바꾼다.
 *   능동 부저는 음 높이가 부저 안에서 정해져 있어 완전히 바꿀 수는 없다.
 *   더 작게 하려면 부저 + 다리에 100Ω~1kΩ 저항을 직렬로 넣어도 된다.
 */

#include "main.h"
#include "buzzer.h"
#include "dot_map.h"

/* ---- 핀 ----
 * PWM을 쓰기 때문에 TIM2 채널 1이 나오는 PA0(A0)에 고정한다. */
#define BUZZER_Pin            GPIO_PIN_0
#define BUZZER_GPIO_Port      GPIOA

/* ---- 소리 조절 (여기 값만 바꾸면 된다) ---- */
#define BUZZER_VOLUME         10    /* 크기 1~100 (%). 켜져 있는 시간 비율. 100이면 원래 소리 */
#define BUZZER_TONE_HZ        400   /* 음색 100~5000 Hz. 낮을수록 "부-" 하는 낮고 거친 소리 */
#define BUZZER_ON_MS          500   /* 한 번 울리는 시간 */
#define BUZZER_OFF_MS         500   /* 쉬는 시간 */

/* ---- 경보 조건 ---- */
#define BUZZER_BIN_COUNT      3
#define BUZZER_ALERT_PERCENT  90    /* 이 값 "이상"인 통이 하나라도 있으면 울린다 */
#define BUZZER_ACTIVE_HIGH    0     /* High일 때 울리면 1. 반대로 동작하는 모듈이면 0 */

/* ---- 테스트 ----
 * 1이면 포화도와 상관없이 항상 울린다 (배선·소리 확인용). 확인이 끝나면 0으로 되돌린다. */
#define BUZZER_TEST_MODE      1

/* TIM2 카운터를 1MHz로 돌린다 (TIM2 클럭 84MHz ÷ 84) */
#define BUZZER_TIMER_CLOCK_HZ 1000000u
#define BUZZER_PERIOD_TICKS   (BUZZER_TIMER_CLOCK_HZ / BUZZER_TONE_HZ)

/* on이면 설정한 크기로 PWM 출력, 아니면 듀티 0 = 계속 꺼짐 */
static void Buzzer_Write(uint8_t on) {
	TIM2->CCR1 = on ? (BUZZER_PERIOD_TICKS * BUZZER_VOLUME / 100u) : 0u;
}

/* 90% 이상인 통이 하나라도 있으면 1 */
static uint8_t Buzzer_IsAlert(void) {
#if BUZZER_TEST_MODE
	return 1;
#endif
	for (uint8_t i = 0; i < BUZZER_BIN_COUNT; i++) {
		if (DM_Map_GetBinPercent(i) >= BUZZER_ALERT_PERCENT)
			return 1;
	}
	return 0;
}

/* PA0을 TIM2 PWM 출력으로 설정하고 끈 상태로 시작한다. MX_GPIO_Init() 다음에 불러야 한다. */
void Buzzer_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_TIM2_CLK_ENABLE();

	/* TIM2 채널 1 PWM 모드 1 */
	TIM2->CR1 = 0;
	TIM2->PSC = (SystemCoreClock / BUZZER_TIMER_CLOCK_HZ) - 1u;   /* APB1 ÷2 → 타이머 클럭 = HCLK */
	TIM2->ARR = BUZZER_PERIOD_TICKS - 1u;
	TIM2->CCR1 = 0;
	TIM2->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
	TIM2->CCER = TIM_CCER_CC1E | (BUZZER_ACTIVE_HIGH ? 0u : TIM_CCER_CC1P);
	TIM2->EGR = TIM_EGR_UG;
	TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

	GPIO_InitStruct.Pin = BUZZER_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
	HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);
}

/* while(1) 안에서 계속 부른다. 기다리지 않고 바로 돌아온다.
 * 경보 중에는 BUZZER_ON_MS 울림 / BUZZER_OFF_MS 쉼을 반복하고, 경보가 풀리면 바로 끈다. */
void Buzzer_Loop(void) {
	static uint8_t sounding = 0;
	static uint32_t lastToggle = 0;
	uint32_t now = HAL_GetTick();

	if (!Buzzer_IsAlert()) {
		if (sounding) {
			sounding = 0;
			Buzzer_Write(0);
		}
		lastToggle = now - BUZZER_OFF_MS;   /* 다음 경보가 오면 바로 울리도록 */
		return;
	}

	if (sounding && now - lastToggle >= BUZZER_ON_MS) {
		sounding = 0;
		lastToggle = now;
		Buzzer_Write(0);
	} else if (!sounding && now - lastToggle >= BUZZER_OFF_MS) {
		sounding = 1;
		lastToggle = now;
		Buzzer_Write(1);
	}
}
