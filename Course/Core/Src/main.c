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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
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
UART_HandleTypeDef huart1;

osThreadId defaultTaskHandle;
/* USER CODE BEGIN PV */
uint8_t in_state[4]  = {0};
uint8_t out_state[4] = {0};

/* Маска поведения (пока просто храним 4 символа '0'/'1') */
char logic_mask[5] = "0000";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
void StartDefaultTask(void const * argument);

/* USER CODE BEGIN PFP */
static void ReadInputs(void);
static void ApplyLogic(void);
static void WriteOutputs(void);

static void UartSend(const char *s);
static void ProcessCommand(char *cmd);
static void PollUartCommand(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void ReadInputs(void)
{
  /* При Pull-up: отпущена = 1, нажата = 0 (если кнопка на GND) */
  in_state[0] = (uint8_t)HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin);
  in_state[1] = (uint8_t)HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin);
  in_state[2] = (uint8_t)HAL_GPIO_ReadPin(IN3_GPIO_Port, IN3_Pin);
  in_state[3] = (uint8_t)HAL_GPIO_ReadPin(IN4_GPIO_Port, IN4_Pin);
}

static void ApplyLogic(void)
{
  /* Базовая логика из задания */
  out_state[0] = (uint8_t)(in_state[0] && in_state[3]); /* OUT1 = IN1 && IN4 */
  out_state[1] = in_state[1];                           /* OUT2 = IN2 */

  /* OUT3/OUT4 пока оставим как есть (0 по умолчанию) */
  /* Позже добавим управление через команды/маску */
}

static void WriteOutputs(void)
{
  HAL_GPIO_WritePin(OUT1_GPIO_Port, OUT1_Pin, out_state[0] ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OUT2_GPIO_Port, OUT2_Pin, out_state[1] ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OUT3_GPIO_Port, OUT3_Pin, out_state[2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OUT4_GPIO_Port, OUT4_Pin, out_state[3] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static char rx_line[64];
static uint8_t rx_pos = 0;

static void UartSend(const char *s)
{
  HAL_UART_Transmit(&huart1, (uint8_t*)s, (uint16_t)strlen(s), 100);
}

static void ProcessCommand(char *cmd)
{
  char resp[96];

  /* Убрать \r \n в конце */
  size_t n = strlen(cmd);
  while (n > 0 && (cmd[n - 1] == '\r' || cmd[n - 1] == '\n'))
  {
    cmd[n - 1] = '\0';
    n--;
  }

  /* 1) In? */
  if (strcmp(cmd, "In?") == 0)
  {
    ReadInputs();
    snprintf(resp, sizeof(resp), "IN: %d%d%d%d\r\n",
             in_state[0], in_state[1], in_state[2], in_state[3]);
    UartSend(resp);
    return;
  }

  /* 2) Out N on/off */
  {
    int ch = 0;
    char state[8] = {0};
    if (sscanf(cmd, "Out %d %7s", &ch, state) == 2)
    {
      if (ch >= 1 && ch <= 4)
      {
        if (strcmp(state, "on") == 0)
        {
          out_state[ch - 1] = 1;
          WriteOutputs();
          snprintf(resp, sizeof(resp), "OK: OUT%d=ON\r\n", ch);
          UartSend(resp);
          return;
        }
        else if (strcmp(state, "off") == 0)
        {
          out_state[ch - 1] = 0;
          WriteOutputs();
          snprintf(resp, sizeof(resp), "OK: OUT%d=OFF\r\n", ch);
          UartSend(resp);
          return;
        }
      }
      UartSend("ERR: use Out 1..4 on/off\r\n");
      return;
    }
  }

  /* 3) Mask XXXX (X только 0/1, длина 4) */
  {
    char m[8] = {0};
    if (sscanf(cmd, "Mask %7s", m) == 1)
    {
      if (strlen(m) == 4 &&
          (m[0] == '0' || m[0] == '1') &&
          (m[1] == '0' || m[1] == '1') &&
          (m[2] == '0' || m[2] == '1') &&
          (m[3] == '0' || m[3] == '1'))
      {
        memcpy(logic_mask, m, 4);
        logic_mask[4] = '\0';
        snprintf(resp, sizeof(resp), "OK: MASK=%s\r\n", logic_mask);
        UartSend(resp);
        return;
      }
      UartSend("ERR: use Mask XXXX (X=0/1)\r\n");
      return;
    }
  }

  UartSend("ERR: unknown command\r\n");
}

static void PollUartCommand(void)
{
  uint8_t ch;

  /* Читаем по 1 байту без блокировки (timeout=0) */
  if (HAL_UART_Receive(&huart1, &ch, 1, 0) == HAL_OK)
  {
    if (ch == '\r' || ch == '\n')
    {
      if (rx_pos > 0)
      {
        rx_line[rx_pos] = '\0';
        ProcessCommand(rx_line);
        rx_pos = 0;
      }
    }
    else
    {
      if (rx_pos < sizeof(rx_line) - 1)
      {
        rx_line[rx_pos++] = (char)ch;
      }
      else
      {
        rx_pos = 0;
        UartSend("ERR: line too long\r\n");
      }
    }
  }
}

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
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  huart1.Init.BaudRate = 115200;
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
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, OUT1_Pin|OUT2_Pin|OUT3_Pin|OUT4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : IN1_Pin IN2_Pin IN3_Pin IN4_Pin */
  GPIO_InitStruct.Pin = IN1_Pin|IN2_Pin|IN3_Pin|IN4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : USART_TX_Pin USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OUT1_Pin OUT2_Pin OUT3_Pin OUT4_Pin */
  GPIO_InitStruct.Pin = OUT1_Pin|OUT2_Pin|OUT3_Pin|OUT4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
	  ReadInputs();
	  ApplyLogic();
	  WriteOutputs();

	  PollUartCommand();

	  osDelay(10);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM5 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM5)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
