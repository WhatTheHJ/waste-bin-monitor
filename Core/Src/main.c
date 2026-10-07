/* USER CODE BEGIN Header */

/**

  ******************************************************************************

  * @file           : main.c

  * @brief          : Main program body

  ******************************************************************************

  * @attention

  *

  * Copyright (c) 2026 STMicroelectronics.

  * All rights reserved.

  *

  * This software is licensed under terms that can be found in the LICENSE file

  * in the root directory of this software component.

  * If no LICENSE file comes with this software, it is provided AS-IS.

  *

  ******************************************************************************

  */

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "main.h"

/* Private includes ----------------------------------------------------------*/

/* USER CODE BEGIN Includes */

#include <string.h>

#include <stdlib.h>

#include <stdio.h>

#include "dot_map.h"

#include "buzzer.h"

#include "lcd1602.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

uint8_t rxData;

char rxBuffer[64];
char messageBuffer[64];

volatile int rxIndex = 0;
volatile int messageReady = 0;

int bin_a = 0;
int bin_b = 0;
int bin_c = 0;

/* 사용자 수거 요청 LCD 표시용 */
uint8_t requestDisplayActive = 0;
uint32_t requestDisplayUntil = 0;
char requestBin = '-';

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/

void SystemClock_Config(void);

static void MX_GPIO_Init(void);

static void MX_USART1_UART_Init(void);

static void MX_USART2_UART_Init(void);

static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**

  * @brief  The application entry point.

  * @retval int

  */

int main(void)

{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */

  MX_GPIO_Init();

  MX_USART1_UART_Init();

  MX_USART2_UART_Init();

  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */

  DM_Map_Init();

  Buzzer_Init();

  LCD_Init();

  LCD_SetCursor(0, 0);

  LCD_Print("FOOD WASTE");

  LCD_SetCursor(1, 0);

  LCD_Print("SYSTEM START");

  /* HC-06 첫 1 byte 수신 시작 */

  HAL_UART_Receive_IT(&huart1, &rxData, 1);

  char msg[] = "STM32 START\r\n";

  HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);

  /* USER CODE END 2 */

  /* Infinite loop */

  /* USER CODE BEGIN WHILE */

  while (1)

  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // 8x8 화면 계속 Refresh
    DM_Map_Loop();

    // 부저 상태 계속 확인
    Buzzer_Loop();

    // 사용자 수거 요청 표시가 5초 지나면 기존 상태 화면으로 복귀
    if (requestDisplayActive && HAL_GetTick() >= requestDisplayUntil)
    {
        requestDisplayActive = 0;
        LCD_UpdateBins(bin_a, bin_b, bin_c);
    }

    if (messageReady)
    {
        char temp[64];

        strcpy(temp, messageBuffer);
        messageReady = 0;

        char *ptr = strtok(temp, "@");

        // ==================================================
        // Raspberry Pi 센서 상태
        // STATUS@A@B@C
        // ==================================================
        if (ptr != NULL && strcmp(ptr, "STATUS") == 0)
        {
            char *a_str = strtok(NULL, "@");
            char *b_str = strtok(NULL, "@");
            char *c_str = strtok(NULL, "@");

            if (a_str != NULL &&
                b_str != NULL &&
                c_str != NULL)
            {
                bin_a = atoi(a_str);
                bin_b = atoi(b_str);
                bin_c = atoi(c_str);

                // Dot Matrix + 자동 Buzzer에 실제 센서값 전달
                DM_Map_SetBinPercent(0, bin_a);
                DM_Map_SetBinPercent(1, bin_b);
                DM_Map_SetBinPercent(2, bin_c);

                // 사용자 수거요청 문구 표시 중에는 LCD를 바로 덮어쓰지 않음
                if (!requestDisplayActive)
                {
                    LCD_UpdateBins(bin_a, bin_b, bin_c);
                }

                // COM9 확인용
                char msg[100];

                snprintf(
                    msg,
                    sizeof(msg),
                    "A = %d%%\r\n"
                    "B = %d%%\r\n"
                    "C = %d%%\r\n",
                    bin_a,
                    bin_b,
                    bin_c
                );

                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)msg,
                    strlen(msg),
                    100
                );
            }
        }

        // ==================================================
        // 일반 사용자 수거 요청
        // REQUEST@A / REQUEST@B / REQUEST@C
        // ==================================================
        else if (ptr != NULL && strcmp(ptr, "REQUEST") == 0)
        {
            char *bin_str = strtok(NULL, "@");

            if (bin_str != NULL &&
                (bin_str[0] == 'A' ||
                 bin_str[0] == 'B' ||
                 bin_str[0] == 'C'))
            {
                requestBin = bin_str[0];

                // 사용자 요청 위치를 8x8 Matrix에서 5초간 깜빡임
                DM_Map_RequestAlert(
                    (uint8_t)(requestBin - 'A'),
                    5000
                );

                // 사용자 요청이 들어오면 2초간 수동 부저
                Buzzer_ManualBeep(2000);

                // LCD에는 5초간 수거 요청 표시
                requestDisplayActive = 1;
                requestDisplayUntil = HAL_GetTick() + 5000;

                char line1[17];

                snprintf(
                    line1,
                    sizeof(line1),
                    "REQUEST BIN %c",
                    requestBin
                );

                // 기존 글자 지우기
                LCD_SetCursor(0, 0);
                LCD_Print("                ");
                LCD_SetCursor(1, 0);
                LCD_Print("                ");

                LCD_SetCursor(0, 0);
                LCD_Print(line1);
                LCD_SetCursor(1, 0);
                LCD_Print("COLLECT REQUEST ");

                // COM9 확인용
                char msg[64];

                snprintf(
                    msg,
                    sizeof(msg),
                    "USER REQUEST : BIN %c\r\n",
                    requestBin
                );

                HAL_UART_Transmit(
                    &huart2,
                    (uint8_t *)msg,
                    strlen(msg),
                    100
                );
            }
        }
    }
}
/* USER CODE END 3 */

}

/**

  * @brief System Clock Configuration

  * @retval None

  */

void SystemClock_Config(void)

{

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage

  */

  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters

  * in the RCC_OscInitTypeDef structure.

  */

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;

  RCC_OscInitStruct.HSIState = RCC_HSI_ON;

  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;

  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;

  RCC_OscInitStruct.PLL.PLLM = 16;

  RCC_OscInitStruct.PLL.PLLN = 336;

  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;

  RCC_OscInitStruct.PLL.PLLQ = 4;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)

  {

    Error_Handler();

  }

  /** Initializes the CPU, AHB and APB buses clocks

  */

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK

                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;

  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;

  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;

  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)

  {

    Error_Handler();

  }

}

/**

  * @brief I2C1 Initialization Function

  * @param None

  * @retval None

  */

static void MX_I2C1_Init(void)

{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */

  hi2c1.Instance = I2C1;

  hi2c1.Init.ClockSpeed = 100000;

  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;

  hi2c1.Init.OwnAddress1 = 0;

  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;

  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;

  hi2c1.Init.OwnAddress2 = 0;

  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;

  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK)

  {

    Error_Handler();

  }

  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**

  * @brief USART1 Initialization Function

  * @param None

  * @retval None

  */

static void MX_USART1_UART_Init(void)

{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */

  huart1.Instance = USART1;

  huart1.Init.BaudRate = 9600;

  huart1.Init.WordLength = UART_WORDLENGTH_8B;

  huart1.Init.StopBits = UART_STOPBITS_1;

  huart1.Init.Parity = UART_PARITY_NONE;

  huart1.Init.Mode = UART_MODE_TX_RX;

  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;

  huart1.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart1) != HAL_OK)

  {

    Error_Handler();

  }

  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**

  * @brief USART2 Initialization Function

  * @param None

  * @retval None

  */

static void MX_USART2_UART_Init(void)

{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */

  huart2.Instance = USART2;

  huart2.Init.BaudRate = 115200;

  huart2.Init.WordLength = UART_WORDLENGTH_8B;

  huart2.Init.StopBits = UART_STOPBITS_1;

  huart2.Init.Parity = UART_PARITY_NONE;

  huart2.Init.Mode = UART_MODE_TX_RX;

  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;

  huart2.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart2) != HAL_OK)

  {

    Error_Handler();

  }

  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**

  * @brief GPIO Initialization Function

  * @param None

  * @retval None

  */

static void MX_GPIO_Init(void)

{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */

  __HAL_RCC_GPIOC_CLK_ENABLE();

  __HAL_RCC_GPIOH_CLK_ENABLE();

  __HAL_RCC_GPIOA_CLK_ENABLE();

  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */

  HAL_GPIO_WritePin(GPIOA, LD2_Pin|DM_CLK_Pin|DM_DATA_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */

  HAL_GPIO_WritePin(DM_LATCH_GPIO_Port, DM_LATCH_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */

  GPIO_InitStruct.Pin = B1_Pin;

  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;

  GPIO_InitStruct.Pull = GPIO_NOPULL;

  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD2_Pin DM_CLK_Pin DM_DATA_Pin */

  GPIO_InitStruct.Pin = LD2_Pin|DM_CLK_Pin|DM_DATA_Pin;

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;

  GPIO_InitStruct.Pull = GPIO_NOPULL;

  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : DM_LATCH_Pin */

  GPIO_InitStruct.Pin = DM_LATCH_Pin;

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;

  GPIO_InitStruct.Pull = GPIO_NOPULL;

  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(DM_LATCH_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */

}

/* USER CODE BEGIN 4 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)

{

    if (huart->Instance == USART1)

    {

        if (!messageReady)

        {

            if (rxData == '\n' || rxData == '\r')

            {

                if (rxIndex > 0)

                {

                    rxBuffer[rxIndex] = '\0';

                    strcpy(messageBuffer, rxBuffer);

                    messageReady = 1;

                    rxIndex = 0;

                }

            }

            else

            {

                if (rxIndex < sizeof(rxBuffer) - 1)

                {

                    rxBuffer[rxIndex++] = rxData;

                }

                else

                {

                    rxIndex = 0;

                }

            }

        }

        /* 다음 1 byte 계속 수신 */

        HAL_UART_Receive_IT(&huart1, &rxData, 1);

    }

}

/* USER CODE END 4 */

/**

  * @brief  This function is executed in case of error occurrence.

  * @retval None

  */

void Error_Handler(void)

{

  /* USER CODE BEGIN Error_Handler_Debug */

  /* User can add his own implementation to report the HAL error return state */

  __disable_irq();

  while (1)

  {

  }

  /* USER CODE END Error_Handler_Debug */

}

#ifdef USE_FULL_ASSERT

/**

  * @brief  Reports the name of the source file and the source line number

  *         where the assert_param error has occurred.

  * @param  file: pointer to the source file name

  * @param  line: assert_param error line source number

  * @retval None

  */

void assert_failed(uint8_t *file, uint32_t line)

{

  /* USER CODE BEGIN 6 */

  /* User can add his own implementation to report the file name and line number,

     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* USER CODE END 6 */

}

#endif /* USE_FULL_ASSERT */
