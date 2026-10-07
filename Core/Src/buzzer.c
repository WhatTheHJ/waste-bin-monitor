/*
 * buzzer.c
 *
 * 사용자 수거 요청이 들어왔을 때만 부저를 울린다.
 *   웹(web/request.php) 버튼 → 라즈베리파이 서버 → 블루투스 "REQUEST@A"
 *   → main.c HAL_UART_RxCpltCallback() → Buzzer_ManualBeep()
 *   → BUZZER_ON_MS 울림 / BUZZER_OFF_MS 쉼을 반복
 *
 * 포화도(90% 이상 등)로는 울리지 않는다.
 *
 * 배선 (능동 부저): + (긴 다리) → A0 (PA0), - (짧은 다리) → GND
 * 소리는 PA0을 TIM2 채널 1 PWM으로 구동해 크기·음색을 조절한다.
 */

#include "main.h"
#include "buzzer.h"

/* ---- 핀 (TIM2 채널 1) ---- */
#define BUZZER_Pin            GPIO_PIN_0
#define BUZZER_GPIO_Port      GPIOA

/* ---- 소리 조절 ---- */
#define BUZZER_VOLUME         10    /* 크기 1~100 (%) */
#define BUZZER_TONE_HZ        400   /* 음색. 낮을수록 낮고 거친 소리 */
#define BUZZER_ON_MS          500   /* 한 번 울리는 시간 */
#define BUZZER_OFF_MS         500   /* 쉬는 시간 */
#define BUZZER_ACTIVE_HIGH    0     /* High일 때 울리면 1, Low일 때 울리면 0 */

/* 1이면 요청과 상관없이 항상 울린다 (배선·소리 확인용) */
#define BUZZER_TEST_MODE      0

#define BUZZER_TIMER_CLOCK_HZ 1000000u
#define BUZZER_PERIOD_TICKS   (BUZZER_TIMER_CLOCK_HZ / BUZZER_TONE_HZ)

/* 수거 요청 부저 상태 */
static uint8_t manualBeepActive = 0;
static uint32_t manualBeepUntil = 0;


static void Buzzer_Write(uint8_t on)
{
    TIM2->CCR1 = on
        ? (BUZZER_PERIOD_TICKS * BUZZER_VOLUME / 100u)
        : 0u;
}


/* 지금 울려야 하는 상태인지 (수거 요청 시간 안이면 1) */
static uint8_t Buzzer_IsRequested(uint32_t now)
{
#if BUZZER_TEST_MODE
    (void)now;
    return 1;
#else
    if (!manualBeepActive)
    {
        return 0;
    }

    if ((int32_t)(manualBeepUntil - now) > 0)
    {
        return 1;
    }

    manualBeepActive = 0;
    return 0;
#endif
}


void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0;
    TIM2->PSC = (SystemCoreClock / BUZZER_TIMER_CLOCK_HZ) - 1u;
    TIM2->ARR = BUZZER_PERIOD_TICKS - 1u;
    TIM2->CCR1 = 0;
    TIM2->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM2->CCER = TIM_CCER_CC1E |
                 (BUZZER_ACTIVE_HIGH ? 0u : TIM_CCER_CC1P);
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    GPIO_InitStruct.Pin = BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;

    HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

    manualBeepActive = 0;
    Buzzer_Write(0);
}


/*
 * 수거 요청 부저. ms 동안 울림/쉼을 반복한다.
 * 울리는 중에 다시 부르면 그 시점부터 ms를 다시 센다.
 * 예: Buzzer_ManualBeep(3000);  -> 3초간 삐- 삐- 삐-
 */
void Buzzer_ManualBeep(uint32_t ms)
{
    if (ms == 0)
    {
        return;
    }

    manualBeepActive = 1;
    manualBeepUntil = HAL_GetTick() + ms;
}


/* while(1) 안에서 계속 부른다. 기다리지 않고 바로 돌아온다. */
void Buzzer_Loop(void)
{
    static uint8_t sounding = 0;
    static uint32_t lastToggle = 0;

    uint32_t now = HAL_GetTick();

    if (!Buzzer_IsRequested(now))
    {
        if (sounding)
        {
            sounding = 0;
            Buzzer_Write(0);
        }

        /* 다음 요청이 오면 바로 울리도록 */
        lastToggle = now - BUZZER_OFF_MS;
        return;
    }

    if (sounding &&
        now - lastToggle >= BUZZER_ON_MS)
    {
        sounding = 0;
        lastToggle = now;
        Buzzer_Write(0);
    }
    else if (!sounding &&
             now - lastToggle >= BUZZER_OFF_MS)
    {
        sounding = 1;
        lastToggle = now;
        Buzzer_Write(1);
    }
}
