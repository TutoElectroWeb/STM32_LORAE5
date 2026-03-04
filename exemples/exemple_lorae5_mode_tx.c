/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : test_lorae5_mode_tx.c
  * @brief          : Exemple mode TEST TX périodique LoRa-E5
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "STM32_LORAE5.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LOG_NAME          "exemple_lorae5_mode_tx"  ///< Nom du journal pour affichage bannière
#define TEST_TX_PERIOD_MS  5000U                     ///< Période entre 2 trames TX radio (ms)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */
static LORAE5_Handle_t hlorae5;
static uint8_t lora_rx_dma[256];
static uint8_t test_step = 0U;
static uint32_t next_action_ms = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void LoraTestTx_AppInit(void);
static void LoraTestTx_AppTask(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  printf("\r\n========================================\r\n");
  printf("  Fichier: " LOG_NAME "\r\n");
  printf("  LoRa-E5 - Mode TEST TX\r\n");
  printf("========================================\r\n\r\n");

  LoraTestTx_AppInit();
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    LoraTestTx_AppTask();
    /* USER CODE END 3 */
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

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

static void MX_USART1_UART_Init(void)
{
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
}

static void MX_USART2_UART_Init(void)
{
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
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
static void LoraTestTx_AppInit(void)
{
  if (LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma)) != LORAE5_OK)
  {
    Error_Handler();
  }

  if (LORAE5_StartRx(&hlorae5) != LORAE5_OK)
  {
    Error_Handler();
  }

  test_step = 0U;
  next_action_ms = HAL_GetTick() + 300U;
}

static void LoraTestTx_AppTask(void)
{
  char line[LORAE5_RX_LINE_MAX];
  uint32_t now = HAL_GetTick();

  while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
  {
    printf("[LORA] %s\r\n", line);
  }

  if ((int32_t)(now - next_action_ms) < 0)
  {
    return;
  }

  switch (test_step)
  {
    case 0:
      if (LORAE5_Ping(&hlorae5) == LORAE5_OK)
      {
        printf("[APP] AT\r\n");
        test_step = 1U;
        next_action_ms = now + 400U;
      }
      break;

    case 1:
      if (LORAE5_SetMode(&hlorae5, LORAE5_MODE_TEST) == LORAE5_OK)
      {
        printf("[APP] AT+MODE=TEST\r\n");
        test_step = 2U;
        next_action_ms = now + 500U;
      }
      break;

    case 2:
      if (LORAE5_SendAT(&hlorae5, "AT+TEST=RFCFG,868,SF12,125,12,15,14,ON,OFF,OFF") == LORAE5_OK)
      {
        printf("[APP] AT+TEST=RFCFG,...\r\n");
        test_step = 3U;
        next_action_ms = now + 1000U;
      }
      break;

    default:
      if (LORAE5_SendAT(&hlorae5, "AT+TEST=TXLRPKT,\"TutoElectroWeb\"") == LORAE5_OK)
      {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        printf("[APP] TXLRPKT\r\n");
        next_action_ms = now + TEST_TX_PERIOD_MS;
      }
      break;
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if ((huart != NULL) && (huart->Instance == USART1))
  {
    LORAE5_OnRxEvent(&hlorae5, Size);

    if ((huart->hdmarx != NULL) && (huart->hdmarx->Init.Mode != DMA_CIRCULAR))
    {
      (void)LORAE5_StartRx(&hlorae5);
    }
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == USART1))
  {
    LORAE5_OnTxCplt(&hlorae5);
  }
}
/* USER CODE END 4 */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
    LORAE5_DeInit(&hlorae5);
    while (1)
  {
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    for (volatile uint32_t wait = 0U; wait < 250000U; ++wait)
    {
      __NOP();
    }
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
