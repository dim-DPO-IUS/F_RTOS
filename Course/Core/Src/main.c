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
#include "event_groups.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EVT_IN1            (1U << 3)
#define EVT_IN2            (1U << 2)
#define EVT_IN3            (1U << 1)
#define EVT_IN4            (1U << 0)
#define EVT_INPUT_CHANGED  (1U << 4)
#define EVT_MASK_CHANGED   (1U << 5)

#define OUT_COUNT 4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

osThreadId out1TaskHandle;
osThreadId uartTaskHandle;
osThreadId inputTaskHandle;
osThreadId out2TaskHandle;
osThreadId out3TaskHandle;
osThreadId out4TaskHandle;

/* USER CODE BEGIN PV */
EventGroupHandle_t ctrlEventGroup;
static uint8_t out_masks[OUT_COUNT] = {0x08, 0x04, 0x02, 0x01};
/*
------+-------+------+---------------------
Выход |	Маска |	Бит  | Реагирует на
------+-------+------+---------------------
OUT1  |	0x08  = 1000 | EVT_IN1	Кнопка IN1
OUT2  |	0x04  = 0100 | EVT_IN2	Кнопка IN2
OUT3  |	0x02  = 0010 | EVT_IN3	Кнопка IN3
OUT4  |	0x01  = 0001 | EVT_IN4	Кнопка IN4
------+--------------+---------------------
*/

static char rx_line[64];
static uint8_t rx_pos = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
void StartOut1Task(void const * argument);
void StartUartTask(void const * argument);
void StartInputTask(void const * argument);
void StartOut2Task(void const * argument);
void StartOut3Task(void const * argument);
void StartOut4Task(void const * argument);

/* USER CODE BEGIN PFP */
static void UartSend(const char *s);
static void ProcessCommand(char *cmd);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void UartSend(const char *s)
{
  HAL_UART_Transmit(&huart1, (uint8_t*)s, (uint16_t)strlen(s), 100);
}

static void ProcessCommand(char *cmd)
{
  char resp[64];

  size_t n = strlen(cmd);
  while (n > 0 && (cmd[n - 1] == '\r' || cmd[n - 1] == '\n'))
  {
    cmd[n - 1] = '\0';
    n--;
  }

  /* In? — читаем пины напрямую для актуального состояния */
  if (strcmp(cmd, "In?") == 0)
  {
    int in1 = (HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    int in2 = (HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    int in3 = (HAL_GPIO_ReadPin(IN3_GPIO_Port, IN3_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    int in4 = (HAL_GPIO_ReadPin(IN4_GPIO_Port, IN4_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    snprintf(resp, sizeof(resp), "IN: %d%d%d%d\r\n", in1, in2, in3, in4);
    UartSend(resp);
    return;
  }

  /* Mask? — опрос текущих масок */
  if (strcmp(cmd, "Mask?") == 0)
  {
    char m[4][5];
    for (int i = 0; i < 4; i++)
    {
      m[i][0] = (out_masks[i] & 0x08U) ? '1' : '0';
      m[i][1] = (out_masks[i] & 0x04U) ? '1' : '0';
      m[i][2] = (out_masks[i] & 0x02U) ? '1' : '0';
      m[i][3] = (out_masks[i] & 0x01U) ? '1' : '0';
      m[i][4] = '\0';
    }
    snprintf(resp, sizeof(resp), "MASK: %s %s %s %s\r\n", m[0], m[1], m[2], m[3]);
    UartSend(resp);
    return;
  }

  /* Out? — опрос состояния выходов */
  if (strcmp(cmd, "Out?") == 0)
  {
    int o1 = (HAL_GPIO_ReadPin(OUT1_GPIO_Port, OUT1_Pin) == GPIO_PIN_SET) ? 1 : 0;
    int o2 = (HAL_GPIO_ReadPin(OUT2_GPIO_Port, OUT2_Pin) == GPIO_PIN_SET) ? 1 : 0;
    int o3 = (HAL_GPIO_ReadPin(OUT3_GPIO_Port, OUT3_Pin) == GPIO_PIN_SET) ? 1 : 0;
    int o4 = (HAL_GPIO_ReadPin(OUT4_GPIO_Port, OUT4_Pin) == GPIO_PIN_SET) ? 1 : 0;
    snprintf(resp, sizeof(resp), "OUT: %d %d %d %d\r\n", o1, o2, o3, o4);
    UartSend(resp);
    return;
  }

  /* Out N on/off — прямое временное управление выходом */
  {
    int num;
    char action[8] = {0};
    if (sscanf(cmd, "Out %d %7s", &num, action) == 2)
    {
      if (num < 1 || num > 4)
      {
        UartSend("ERR: Out number must be 1-4\r\n");
        return;
      }

      GPIO_TypeDef* port;
      uint16_t pin;
      switch (num)
      {
        case 1: port = OUT1_GPIO_Port; pin = OUT1_Pin; break;
        case 2: port = OUT2_GPIO_Port; pin = OUT2_Pin; break;
        case 3: port = OUT3_GPIO_Port; pin = OUT3_Pin; break;
        case 4: port = OUT4_GPIO_Port; pin = OUT4_Pin; break;
        default: return;
      }

      if (strcmp(action, "on") == 0)
      {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
        snprintf(resp, sizeof(resp), "OK: Out %d on\r\n", num);
        UartSend(resp);
        return;
      }
      else if (strcmp(action, "off") == 0)
      {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
        snprintf(resp, sizeof(resp), "OK: Out %d off\r\n", num);
        UartSend(resp);
        return;
      }
      else
      {
        UartSend("ERR: use Out N on or Out N off\r\n");
        return;
      }
    }
  }

  /* Mask m1m1m1m1 m2m2m2m2 m3m3m3m3 m4m4m4m4 */
  {
    char m[4][8] = {{0}};
    if (sscanf(cmd, "Mask %4s %4s %4s %4s", m[0], m[1], m[2], m[3]) == 4)
    {
      int valid = 1;
      for (int i = 0; i < 4; i++)
      {
        if (strlen(m[i]) != 4) { valid = 0; break; }
        for (int j = 0; j < 4; j++)
        {
          if (m[i][j] != '0' && m[i][j] != '1') { valid = 0; break; }
        }
        if (!valid) break;
      }

      if (!valid)
      {
        UartSend("ERR: use Mask XXXX XXXX XXXX XXXX\r\n");
        return;
      }

      for (int i = 0; i < 4; i++)
      {
        out_masks[i] = (uint8_t)((m[i][0] == '1' ? 0x08U : 0U) |
                                 (m[i][1] == '1' ? 0x04U : 0U) |
                                 (m[i][2] == '1' ? 0x02U : 0U) |
                                 (m[i][3] == '1' ? 0x01U : 0U));
      }

      xEventGroupSetBits(ctrlEventGroup, EVT_MASK_CHANGED);

      snprintf(resp, sizeof(resp), "OK: MASK=%s %s %s %s\r\n", m[0], m[1], m[2], m[3]);
      UartSend(resp);
      return;
    }
  }

  UartSend("ERR: unknown command\r\n");
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
  ctrlEventGroup = xEventGroupCreate();
  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of out1Task */
  osThreadDef(out1Task, StartOut1Task, osPriorityNormal, 0, 128);
  out1TaskHandle = osThreadCreate(osThread(out1Task), NULL);

  /* definition and creation of uartTask */
  osThreadDef(uartTask, StartUartTask, osPriorityIdle, 0, 512);
  uartTaskHandle = osThreadCreate(osThread(uartTask), NULL);

  /* definition and creation of inputTask */
  osThreadDef(inputTask, StartInputTask, osPriorityAboveNormal, 0, 512);
  inputTaskHandle = osThreadCreate(osThread(inputTask), NULL);

  /* definition and creation of out2Task */
  osThreadDef(out2Task, StartOut2Task, osPriorityNormal, 0, 128);
  out2TaskHandle = osThreadCreate(osThread(out2Task), NULL);

  /* definition and creation of out3Task */
  osThreadDef(out3Task, StartOut3Task, osPriorityNormal, 0, 128);
  out3TaskHandle = osThreadCreate(osThread(out3Task), NULL);

  /* definition and creation of out4Task */
  osThreadDef(out4Task, StartOut4Task, osPriorityNormal, 0, 128);
  out4TaskHandle = osThreadCreate(osThread(out4Task), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
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

/* USER CODE BEGIN Header_StartOut1Task */
/**
  * @brief  Function implementing the out1Task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartOut1Task */
void StartOut1Task(void const * argument)
{
  /* USER CODE BEGIN 5 */
  for(;;)
  {
	EventBits_t bits = xEventGroupWaitBits(ctrlEventGroup, EVT_INPUT_CHANGED | EVT_MASK_CHANGED, pdTRUE, pdFALSE, portMAX_DELAY);
    if (out_masks[0] != 0x00 && (bits & out_masks[0]) == out_masks[0])
      HAL_GPIO_WritePin(OUT1_GPIO_Port, OUT1_Pin, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(OUT1_GPIO_Port, OUT1_Pin, GPIO_PIN_RESET);
  }
    /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartOut2Task */
/**
* @brief Function implementing the out2Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOut2Task */
void StartOut2Task(void const * argument)
{
  for(;;)
  {
	EventBits_t bits = xEventGroupWaitBits(ctrlEventGroup, EVT_INPUT_CHANGED | EVT_MASK_CHANGED, pdTRUE, pdFALSE, portMAX_DELAY);
    if (out_masks[1] != 0x00 && (bits & out_masks[1]) == out_masks[1])
      HAL_GPIO_WritePin(OUT2_GPIO_Port, OUT2_Pin, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(OUT2_GPIO_Port, OUT2_Pin, GPIO_PIN_RESET);
  }
}

/* USER CODE BEGIN Header_StartOut3Task */
/**
* @brief Function implementing the out3Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOut3Task */
void StartOut3Task(void const * argument)
{
  for(;;)
  {
	EventBits_t bits = xEventGroupWaitBits(ctrlEventGroup, EVT_INPUT_CHANGED | EVT_MASK_CHANGED, pdTRUE, pdFALSE, portMAX_DELAY);
    if (out_masks[2] != 0x00 && (bits & out_masks[2]) == out_masks[2])
      HAL_GPIO_WritePin(OUT3_GPIO_Port, OUT3_Pin, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(OUT3_GPIO_Port, OUT3_Pin, GPIO_PIN_RESET);
  }
}

/* USER CODE BEGIN Header_StartOut4Task */
/**
* @brief Function implementing the out4Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOut4Task */
void StartOut4Task(void const * argument)
{
  for(;;)
  {
	EventBits_t bits = xEventGroupWaitBits(ctrlEventGroup, EVT_INPUT_CHANGED | EVT_MASK_CHANGED, pdTRUE, pdFALSE, portMAX_DELAY);
    if (out_masks[3] != 0x00 && (bits & out_masks[3]) == out_masks[3])
      HAL_GPIO_WritePin(OUT4_GPIO_Port, OUT4_Pin, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(OUT4_GPIO_Port, OUT4_Pin, GPIO_PIN_RESET);
  }
}

/* USER CODE BEGIN Header_StartUartTask */
/**
* @brief Function implementing the uartTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartTask */
void StartUartTask(void const * argument)
{
  /* USER CODE BEGIN StartUartTask */
  uint8_t ch;

  for(;;)
  {
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
    osDelay(2);
  }
  /* USER CODE END StartUartTask */
}

/* USER CODE BEGIN Header_StartInputTask */
/**
* @brief Function implementing the inputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartInputTask */
void StartInputTask(void const * argument)
{
  /* USER CODE BEGIN StartInputTask */
	uint8_t prev = 0xFFU;
	uint8_t curr;
	uint8_t stable = 0xFFU;
	uint8_t debounce_ctr = 0;

	for(;;)
	{
	    curr = (uint8_t)((HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin) ? 0x08U : 0U) |
	                     (HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin) ? 0x04U : 0U) |
	                     (HAL_GPIO_ReadPin(IN3_GPIO_Port, IN3_Pin) ? 0x02U : 0U) |
	                     (HAL_GPIO_ReadPin(IN4_GPIO_Port, IN4_Pin) ? 0x01U : 0U));

	    if (curr == stable)
	    {
	        debounce_ctr = 0;
	    }
	    else
	    {
	        debounce_ctr++;
	        if (debounce_ctr >= 2)
	        {
	            stable = curr;
	            debounce_ctr = 0;

	            /* Подтверждённое изменение — обновляем биты событий */
	            if (stable & 0x08U) xEventGroupClearBits(ctrlEventGroup, EVT_IN1);
	            else               xEventGroupSetBits(ctrlEventGroup, EVT_IN1);

	            if (stable & 0x04U) xEventGroupClearBits(ctrlEventGroup, EVT_IN2);
	            else               xEventGroupSetBits(ctrlEventGroup, EVT_IN2);

	            if (stable & 0x02U) xEventGroupClearBits(ctrlEventGroup, EVT_IN3);
	            else               xEventGroupSetBits(ctrlEventGroup, EVT_IN3);

	            if (stable & 0x01U) xEventGroupClearBits(ctrlEventGroup, EVT_IN4);
	            else               xEventGroupSetBits(ctrlEventGroup, EVT_IN4);

	            prev = stable;
	            xEventGroupSetBits(ctrlEventGroup, EVT_INPUT_CHANGED);
	        }
	    }

	    osDelay(10);
//	    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
	}
  /* USER CODE END StartInputTask */
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
