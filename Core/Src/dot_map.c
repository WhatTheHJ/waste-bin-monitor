/*
 * dot_map.c
 * 8x8 도트매트릭스 + 74HC595 2개로 쓰레기통 관제 맵 표시 (NUCLEO-F411RE, HAL)
 * "ㄴ" 모양 길 위에, 포화도 75%를 넘은 쓰레기통 위치를 깜빡여 표시한다.
 *
 * 사용법
 *   main.c의 USER CODE 구역에서 아래 함수를 부른다.
 *     USER CODE BEGIN Includes : #include "dot_map.h"
 *     USER CODE BEGIN 2        : DM_Map_Init();
 *     USER CODE BEGIN 3        : DM_Map_Loop();
 *   포화도가 들어오면(블루투스 수신 등) DM_Map_SetBinPercent(통 번호, %)로 알려 준다.
 *   통 번호: 0 = A, 1 = B, 2 = C
 *
 * 배선 기준
 *   STM32 D11(PA7) → 595 #1 DS(14번)
 *   STM32 D12(PA6) → 595 #1, #2 SH_CP(11번)
 *   STM32 D10(PB6) → 595 #1, #2 ST_CP(12번)
 *   595 #1 Q7'(9번) → 595 #2 DS(14번)
 *   595 #1 Q0~Q7 → 저항 → 매트릭스 열 C1~C8 (Low일 때 켜짐)
 *   595 #2 Q0~Q7 → 매트릭스 행 R1~R8 (High일 때 켜짐)
 */

#include "main.h"
#include "dot_map.h"
#include <stdio.h>
#include <string.h>

/* ---- 핀 ----
 * CubeMX에서 User Label을 DM_DATA / DM_CLK / DM_LATCH로 붙이면 main.h 값을 쓰고,
 * 아직 안 붙였으면 아래 기본값을 쓴다. */
#ifndef DM_DATA_Pin
#define DM_DATA_Pin         GPIO_PIN_7
#define DM_DATA_GPIO_Port   GPIOA
#endif
#ifndef DM_CLK_Pin
#define DM_CLK_Pin          GPIO_PIN_6
#define DM_CLK_GPIO_Port    GPIOA
#endif
#ifndef DM_LATCH_Pin
#define DM_LATCH_Pin        GPIO_PIN_6
#define DM_LATCH_GPIO_Port  GPIOB
#endif

/* 모드 4(자가진단)에서만 쓰는 입력 핀 */
#define DM_PROBE_Pin        GPIO_PIN_5    /* D4 (PB5): 아무 595 핀에 대 보는 "테스터" 선 */
#define DM_PROBE_GPIO_Port  GPIOB
#define DM_LOOP_Pin         GPIO_PIN_8    /* D7 (PA8): 595 #2의 9번(Q7')에서 돌아오는 선 */
#define DM_LOOP_GPIO_Port   GPIOA
#define DM_LOOP_PATTERN     0xA5C3u       /* 되돌려 읽을 16비트 시험 패턴 */

extern UART_HandleTypeDef huart2; /* ST-LINK 가상 COM 포트 (115200bps) */

/* ---- 동작 모드 ----
 * 0 : 관제 맵. "ㄴ" 길 + 75% 초과 쓰레기통 위치 깜빡임 (실제 사용)
 * 아래 1~6은 배선 점검용 테스트 모드
 * 1 : LED를 왼쪽 위부터 한 칸씩 차례로 켜기 (배선 한 줄 한 줄 확인용)
 * 2 : 전체 켜기 (밝기, 빠진 LED 확인용)
 * 3 : 신호선 점검. DATA·CLK·LATCH 세 핀을 1초마다 함께 High/Low로 바꾼다.
 *     멀티미터(또는 LED+저항)로 595의 14번(DATA), 11번(CLK), 12번(LATCH)에서
 *     3.3V ↔ 0V가 1초마다 바뀌는지 확인한다. 안 바뀌는 핀은 배선 문제다.
 */
#define DM_TEST_MODE      0

/* ---- 화면이 이상하게 나올 때 바꾸는 설정 ---- */
#define DM_FLIP_LEFT_RIGHT  0   /* 좌우가 뒤집혀 보이면 1 */
#define DM_FLIP_UP_DOWN     0   /* 위아래가 뒤집혀 보이면 1 */
#define DM_ROW_ACTIVE_HIGH  1   /* 행은 High일 때 켜짐. 아무것도 안 켜지면 두 값을 모두 반대로 */
#define DM_COL_ACTIVE_LOW   1   /* 열은 Low일 때 켜짐 */

/* ---- 행·열 매핑 ----
 * 화면의 위에서 r번째 줄(0부터)을 켤 때 595 #2의 몇 번째 출력(Q0~Q7)을 쓸지,
 * 왼쪽에서 c번째 칸을 켤 때 595 #1의 몇 번째 출력을 쓸지 적는다.
 * 핀맵대로 배선된 1088AS라면 0~7 그대로다. 모드 6으로 찾은 값으로 바꾼다. */
static const uint8_t DM_ROW_MAP[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const uint8_t DM_COL_MAP[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/* "ㄴ" 모양. 한 줄에 8비트, 비트 0이 왼쪽 첫 칸(열 C1) */
/* ---- 쓰레기통 위치 ----
 * {x, y}: x는 왼쪽에서 몇 번째 칸, y는 위에서 몇 번째 줄 (둘 다 1부터, 왼쪽 위가 {1, 1}). */
#define DM_BIN_COUNT          3
#define DM_BIN_ALERT_PERCENT  75    /* 이 값을 "초과"하면 위치를 깜빡여 표시 */
#define DM_BIN_BLINK_MS       300   /* 깜빡임 간격 */
#define DM_MAP_DEMO           0     /* 1이면 실제 데이터가 오기 전 시연용 값을 넣는다 */

static const uint8_t DM_BIN_POS[DM_BIN_COUNT][2] = {
	{ 4, 2 },   /* A */
	{ 2, 6 },   /* B */
	{ 6, 5 },   /* C */
};

/* 통별 포화도(%). DM_Map_SetBinPercent()로 갱신한다 */
static volatile int dm_binPercent[DM_BIN_COUNT] = { 0, 0, 0 };

/* 길 모양 "ㄴ". 한 줄에 8비트, 비트 0이 왼쪽 첫 칸(열 C1).
 * 2진수로 쓰면 오른쪽 끝 자리가 화면 왼쪽 첫 칸이다 (그림과 좌우가 반대로 적힌다).
 * 오른쪽으로 한 칸 옮기려면 값을 왼쪽으로 한 칸 민다 (0b00000010 → 0b00000100). */
__attribute__((unused)) static const uint8_t DM_PATTERN_NIEUN[8] = {
	0b00000100, /* . . ■ . . . . . */
	0b00000100, /* . . ■ . . . . . */
	0b00000100, /* . . ■ . . . . . */
	0b00000100, /* . . ■ . . . . . */
	0b00000100, /* . . ■ . . . . . */
	0b00000100, /* . . ■ . . . . . */
	0b11111100, /* . . ■ ■ ■ ■ ■ ■ */
	0b00000000, /* . . . . . . . . */
};

static void DM_PulseDelay(void);
__attribute__((unused)) static void DM_SelfTest(void);
__attribute__((unused)) static void DM_PolarityTest(void);
__attribute__((unused)) static void DM_MapTestStep(uint8_t step);
static void DM_ShiftByte(uint8_t value);
static void DM_Output(uint8_t rowBits, uint8_t colBits);
static uint8_t DM_ReverseBits(uint8_t v);
static void DM_ShowPixels(uint8_t row, uint8_t colPattern);
__attribute__((unused)) static void DM_DrawFrame(const uint8_t frame[8]);
__attribute__((unused)) static void DM_DrawMap(void);

/* 595 제어 핀 3개를 출력으로 설정하고 화면을 끈다.
 * MX_GPIO_Init() 다음에 불러야 한다. (PB6가 CubeMX에서 다른 용도로 잡혀 있어도 여기서 출력으로 덮어쓴다) */
void DM_Map_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	HAL_GPIO_WritePin(DM_DATA_GPIO_Port, DM_DATA_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(DM_LATCH_GPIO_Port, DM_LATCH_Pin, GPIO_PIN_RESET);

	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;

	GPIO_InitStruct.Pin = DM_DATA_Pin;
	HAL_GPIO_Init(DM_DATA_GPIO_Port, &GPIO_InitStruct);
	GPIO_InitStruct.Pin = DM_CLK_Pin;
	HAL_GPIO_Init(DM_CLK_GPIO_Port, &GPIO_InitStruct);
	GPIO_InitStruct.Pin = DM_LATCH_Pin;
	HAL_GPIO_Init(DM_LATCH_GPIO_Port, &GPIO_InitStruct);

	/* 시작할 때 화면 전체 끄기 (꺼진 상태도 활성 레벨 설정에 맞춰 보낸다) */
	DM_Output(DM_ROW_ACTIVE_HIGH ? 0x00 : 0xFF,
			DM_COL_ACTIVE_LOW ? 0xFF : 0x00);

#if DM_MAP_DEMO
	/* 시연용: A 80%, B 90%, C 95% → A와 C 위치가 깜빡인다 */
	DM_Map_SetBinPercent(0, 80);
	DM_Map_SetBinPercent(1, 90);
	DM_Map_SetBinPercent(2, 95);
#endif
}

/* 통 하나의 포화도를 알려 준다. bin: 0 = A, 1 = B, 2 = C */
void DM_Map_SetBinPercent(uint8_t bin, int percent) {
	if (bin >= DM_BIN_COUNT)
		return;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	dm_binPercent[bin] = percent;
}

/* 통 하나의 포화도(%)를 돌려준다. 잘못된 번호면 0 */
int DM_Map_GetBinPercent(uint8_t bin) {
	if (bin >= DM_BIN_COUNT)
		return 0;
	return dm_binPercent[bin];
}

/* while(1) 안에서 계속 부른다 */
void DM_Map_Loop(void) {
#if DM_TEST_MODE == 0
	/* 관제 맵: 8줄을 빠르게 번갈아 켜서 한 화면처럼 보이게 한다 */
	DM_DrawMap();

#elif DM_TEST_MODE == 1
  /* 한 칸씩 켜기: 0.2초마다 다음 칸으로 이동 */
  static uint8_t index = 0;
  static uint32_t lastTick = 0;

  if (HAL_GetTick() - lastTick >= 200) {
    lastTick = HAL_GetTick();
    index = (uint8_t)((index + 1) % 64);
  }
  DM_ShowPixels((uint8_t)(index / 8), (uint8_t)(1u << (index % 8)));

#elif DM_TEST_MODE == 6
  /* 행·열 매핑 찾기: 파란 버튼을 누를 때마다 다음 단계 */
  static uint8_t step = 0;
  static uint8_t started = 0;
  static GPIO_PinState lastButton = GPIO_PIN_SET;

  if (!started) {
    started = 1;
    DM_MapTestStep(step);
  }

  GPIO_PinState button = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);   /* 누르면 LOW */
  if (lastButton == GPIO_PIN_SET && button == GPIO_PIN_RESET) {
    HAL_Delay(30);                                                 /* 채터링 방지 */
    if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) {
      step = (uint8_t)((step + 1) % 16);
      DM_MapTestStep(step);
    }
  }
  lastButton = button;

#elif DM_TEST_MODE == 5
  /* 출력·극성 진단: 2초마다 다음 조합 */
  static uint32_t lastTick = 0;

  if (lastTick == 0 || HAL_GetTick() - lastTick >= 2000) {
    lastTick = HAL_GetTick();
    DM_PolarityTest();
  }

#elif DM_TEST_MODE == 4
  /* 자가진단: 1초마다 결과를 시리얼로 출력 */
  static uint32_t lastTick = 0;

  if (HAL_GetTick() - lastTick >= 1000) {
    lastTick = HAL_GetTick();
    DM_SelfTest();
  }

#elif DM_TEST_MODE == 3
  /* 신호선 점검: 세 핀을 1초마다 함께 토글 */
  static uint32_t lastTick = 0;
  static GPIO_PinState level = GPIO_PIN_RESET;

  if (HAL_GetTick() - lastTick >= 1000) {
    lastTick = HAL_GetTick();
    level = (level == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(DM_DATA_GPIO_Port, DM_DATA_Pin, level);
    HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, level);
    HAL_GPIO_WritePin(DM_LATCH_GPIO_Port, DM_LATCH_Pin, level);
  }

#else
  /* 전체 켜기 */
  static const uint8_t allOn[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  DM_DrawFrame(allOn);
#endif
}

/* 595가 신호 변화를 확실히 읽도록 약 1us 기다린다 (84MHz 기준, 3.3V에서 최소 펄스 폭보다 넉넉함) */
static void DM_PulseDelay(void) {
	for (volatile int i = 0; i < 20; i++) {
	}
}

/* 8비트를 한 비트씩 시프트레지스터로 밀어 넣는다. 큰 비트부터 보낸다. */
static void DM_ShiftByte(uint8_t value) {
	for (int bit = 7; bit >= 0; bit--) {
		HAL_GPIO_WritePin(DM_DATA_GPIO_Port, DM_DATA_Pin,
				((value >> bit) & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
		DM_PulseDelay();
		HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, GPIO_PIN_SET);
		DM_PulseDelay();
		HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, GPIO_PIN_RESET);
	}
}

/* 16비트를 보내고 래치로 한꺼번에 출력한다.
 * 먼저 보낸 8비트는 595 #2(행)까지 밀려가고, 나중에 보낸 8비트는 595 #1(열)에 남는다. */
static void DM_Output(uint8_t rowBits, uint8_t colBits) {
	DM_ShiftByte(rowBits);
	DM_ShiftByte(colBits);
	DM_PulseDelay();
	HAL_GPIO_WritePin(DM_LATCH_GPIO_Port, DM_LATCH_Pin, GPIO_PIN_SET);
	DM_PulseDelay();
	HAL_GPIO_WritePin(DM_LATCH_GPIO_Port, DM_LATCH_Pin, GPIO_PIN_RESET);
}

/* 비트 순서를 거꾸로 뒤집는다 (좌우 반전용) */
static uint8_t DM_ReverseBits(uint8_t v) {
	uint8_t r = 0;
	for (int i = 0; i < 8; i++) {
		r = (uint8_t) ((r << 1) | (v & 0x01));
		v >>= 1;
	}
	return r;
}

/* 한 줄(row)만 켜고, 그 줄에서 colPattern의 1인 칸을 켠다 */
static void DM_ShowPixels(uint8_t row, uint8_t colPattern) {
	if (DM_FLIP_UP_DOWN)
		row = (uint8_t) (7 - row);
	if (DM_FLIP_LEFT_RIGHT)
		colPattern = DM_ReverseBits(colPattern);

	uint8_t rowBits = (uint8_t) (1u << DM_ROW_MAP[row]);
	uint8_t colBits = 0;
	for (int c = 0; c < 8; c++) {
		if (colPattern & (1u << c))
			colBits |= (uint8_t) (1u << DM_COL_MAP[c]);
	}

	if (!DM_ROW_ACTIVE_HIGH)
		rowBits = (uint8_t) ~rowBits;
	if (DM_COL_ACTIVE_LOW)
		colBits = (uint8_t) ~colBits;

	DM_Output(rowBits, colBits);
}

/* 8줄을 1ms씩 차례로 켜서 화면 전체를 그린다 */
static void DM_DrawFrame(const uint8_t frame[8]) {
	for (uint8_t row = 0; row < 8; row++) {
		DM_ShowPixels(row, frame[row]);
		HAL_Delay(1);
	}
}

/* ---------------------------------------------------------------------- */
/* 모드 4: 자가진단                                                        */
/* ---------------------------------------------------------------------- */

static void DM_Print(const char *text) {
	HAL_UART_Transmit(&huart2, (const uint8_t*) text, (uint16_t) strlen(text),
			100);
}

/* 입력 핀을 풀업/풀다운으로 한 번씩 읽어 상태를 판단한다.
 * 둘 다 1이면 HIGH, 둘 다 0이면 LOW, 풀 저항 방향을 따라가면 아무 데도 안 이어진 OPEN.
 * 반환: 1 = HIGH, 0 = LOW, -1 = OPEN */
static int DM_ReadPinState(GPIO_TypeDef *port, uint16_t pin) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;

	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
	HAL_Delay(2);
	GPIO_PinState withPullUp = HAL_GPIO_ReadPin(port, pin);

	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
	HAL_Delay(2);
	GPIO_PinState withPullDown = HAL_GPIO_ReadPin(port, pin);

	if (withPullUp == GPIO_PIN_SET && withPullDown == GPIO_PIN_SET)
		return 1;
	if (withPullUp == GPIO_PIN_RESET && withPullDown == GPIO_PIN_RESET)
		return 0;
	return -1;
}

static const char* DM_StateName(int state) {
	if (state == 1)
		return "HIGH (3.3V)";
	if (state == 0)
		return "LOW (0V)";
	return "OPEN (not connected)";
}

/* 16비트 패턴을 밀어 넣은 뒤, 595 #2의 Q7'(D7)에서 한 비트씩 되돌려 읽는다.
 * 16단을 다 지나 먼저 넣은 비트부터 나오므로 그대로 읽히면 체인 전체가 정상이다. */
static uint16_t DM_LoopbackRead(uint16_t pattern) {
	DM_ShiftByte((uint8_t) (pattern >> 8));
	DM_ShiftByte((uint8_t) (pattern & 0xFF));

	uint16_t readBack = 0;
	for (int i = 0; i < 16; i++) {
		DM_PulseDelay();
		readBack = (uint16_t) ((readBack << 1)
				| (HAL_GPIO_ReadPin(DM_LOOP_GPIO_Port, DM_LOOP_Pin)
						== GPIO_PIN_SET ? 1 : 0));
		HAL_GPIO_WritePin(DM_DATA_GPIO_Port, DM_DATA_Pin, GPIO_PIN_RESET);
		DM_PulseDelay();
		HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, GPIO_PIN_SET);
		DM_PulseDelay();
		HAL_GPIO_WritePin(DM_CLK_GPIO_Port, DM_CLK_Pin, GPIO_PIN_RESET);
	}
	return readBack;
}

static void DM_SelfTest(void) {
	char line[128];
	static uint32_t count = 0;

	snprintf(line, sizeof(line), "\r\n===== DOT SELF-TEST #%lu =====\r\n",
			(unsigned long) ++count);
	DM_Print(line);

	/* 1) D4 테스터 선 */
	int probe = DM_ReadPinState(DM_PROBE_GPIO_Port, DM_PROBE_Pin);
	snprintf(line, sizeof(line), "[D4 probe] %s\r\n", DM_StateName(probe));
	DM_Print(line);

	/* 2) D7 되돌림 선 연결 확인 */
	int loopState = DM_ReadPinState(DM_LOOP_GPIO_Port, DM_LOOP_Pin);
	if (loopState == -1) {
		DM_Print("[D7 loop ] OPEN -> connect 595 #2 pin 9 to D7\r\n");
		return;
	}

	/* 3) 패턴 되돌려 읽기 (풀다운 상태로 읽어서 선이 빠지면 0으로 보인다) */
	uint16_t readBack = DM_LoopbackRead(DM_LOOP_PATTERN);
	snprintf(line, sizeof(line), "[D7 loop ] sent 0x%04X, read 0x%04X -> ",
			(unsigned) DM_LOOP_PATTERN, (unsigned) readBack);
	DM_Print(line);

	if (readBack == DM_LOOP_PATTERN) {
		DM_Print("OK (DATA, CLK, MR, 595#1->#2 all good)\r\n");
	} else if (readBack == 0x0000) {
		DM_Print(
				"FAIL, always 0 -> check MR(pin10)=3.3V, DATA D11->#1 pin14, CLK D12->pin11\r\n");
	} else if (readBack == 0xFFFF) {
		DM_Print(
				"FAIL, always 1 -> check DATA D11->#1 pin14, CLK D12->pin11\r\n");
	} else if ((uint8_t) (readBack >> 8)
			== (uint8_t) (DM_LOOP_PATTERN & 0xFF)) {
		DM_Print(
				"FAIL, only 8 bits deep -> #1 pin9 is not reaching #2 pin14\r\n");
	} else {
		DM_Print("FAIL, bits garbled -> loose CLK/DATA wire or noise\r\n");
	}
}

/* ---------------------------------------------------------------------- */
/* 모드 5: 출력·극성 진단                                                  */
/* ---------------------------------------------------------------------- */

static void DM_PolarityTest(void) {
	static uint8_t combo = 0;
	char line[128];

	uint8_t rowActiveHigh = (combo & 0x02) ? 1 : 0;
	uint8_t colActiveLow = (combo & 0x01) ? 1 : 0;

	/* 첫 번째 행(R1)만 켜고, 열은 8칸 모두 켠다 → 가로줄 하나 */
	uint8_t rowBits = rowActiveHigh ? 0x01 : (uint8_t) ~0x01;
	uint8_t colBits = colActiveLow ? 0x00 : 0xFF;
	DM_Output(rowBits, colBits);
	HAL_Delay(5);

	int probe = DM_ReadPinState(DM_PROBE_GPIO_Port, DM_PROBE_Pin);
	snprintf(line, sizeof(line),
			"[combo %u] ROW_ACTIVE_HIGH=%u COL_ACTIVE_LOW=%u | 595#2 Q0 should be %s | D4 probe: %s\r\n",
			(unsigned) combo, (unsigned) rowActiveHigh, (unsigned) colActiveLow,
			rowActiveHigh ? "HIGH" : "LOW", DM_StateName(probe));
	DM_Print(line);

	combo = (uint8_t) ((combo + 1) % 4);
	if (combo == 0) {
		DM_Print("----\r\n");
	}
}

/* ---------------------------------------------------------------------- */
/* 모드 6: 행·열 매핑 찾기                                                 */
/* ---------------------------------------------------------------------- */

/* 매핑 표를 거치지 않고 595 출력 하나만 켠 화면을 고정으로 띄운다 */
static void DM_MapTestStep(uint8_t step) {
	char line[128];
	uint8_t rowBits;
	uint8_t colBits;

	if (step < 8) {
		rowBits = (uint8_t) (1u << step); /* 595 #2 Q(step) 하나만 */
		colBits = 0xFF; /* 595 #1 전부 */
		snprintf(line, sizeof(line),
				"[step %2u] 595#2 Q%u only + all of 595#1 -> what lights?\r\n",
				(unsigned) step, (unsigned) step);
	} else {
		rowBits = 0xFF; /* 595 #2 전부 */
		colBits = (uint8_t) (1u << (step - 8)); /* 595 #1 Q(step-8) 하나만 */
		snprintf(line, sizeof(line),
				"[step %2u] 595#1 Q%u only + all of 595#2 -> what lights?\r\n",
				(unsigned) step, (unsigned) (step - 8));
	}

	if (!DM_ROW_ACTIVE_HIGH)
		rowBits = (uint8_t) ~rowBits;
	if (DM_COL_ACTIVE_LOW)
		colBits = (uint8_t) ~colBits;
	DM_Output(rowBits, colBits);
	DM_Print(line);
}

/* ---------------------------------------------------------------------- */
/* 모드 0: 관제 맵                                                         */
/* ---------------------------------------------------------------------- */

/* "ㄴ" 길 위에 75% 초과 쓰레기통 위치를 깜빡여 한 화면을 그린다.
 * 길 위에 있는 점도 보이도록 켜짐/꺼짐을 번갈아 바꾼다. */
static void DM_DrawMap(void) {
	uint8_t frame[8];
	uint8_t blinkOn = ((HAL_GetTick() / DM_BIN_BLINK_MS) % 2) == 0;

	memcpy(frame, DM_PATTERN_NIEUN, sizeof(frame));

	for (int i = 0; i < DM_BIN_COUNT; i++) {
		if (dm_binPercent[i] <= DM_BIN_ALERT_PERCENT)
			continue;

		uint8_t x = (uint8_t) (DM_BIN_POS[i][0] - 1);   /* 1부터 센 좌표 → 0부터 */
		uint8_t y = (uint8_t) (DM_BIN_POS[i][1] - 1);

		if (blinkOn)
			frame[y] |= (uint8_t) (1u << x);
		else
			frame[y] &= (uint8_t) ~(1u << x);
	}

	DM_DrawFrame(frame);
}
