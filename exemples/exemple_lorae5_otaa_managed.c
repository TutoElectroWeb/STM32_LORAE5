/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : exemple_lorae5_otaa_managed.c
  * @brief          : Exemple OTAA managé (lib gère la séquence AT)
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
#include "STM32_LORAE5_conf.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LOG_NAME "exemple_lorae5_otaa_managed"  ///< Nom du journal pour affichage bannière
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
static uint8_t deinit_done = 0U;      ///< Démo LORAE5_DeInit (appui B1 ou timeout)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void LoraManaged_AppInit(void);
static void LoraManaged_AppTask(void);
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
  printf("  LoRa-E5 - OTAA managé\r\n");
  printf("========================================\r\n\r\n");

  LoraManaged_AppInit();
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    LoraManaged_AppTask();
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
static void LoraManaged_AppInit(void)
{
  if (LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma)) != LORAE5_OK)
  {
    Error_Handler();
  }

  if (LORAE5_StartRx(&hlorae5) != LORAE5_OK)
  {
    Error_Handler();
  }

  if (LORAE5_StartManagedOtaaWithKeys(&hlorae5,
                                      LORAE5_APP_REGION,
                                      LORAE5_APP_UPLINK_PAYLOAD,
                                      LORAE5_APP_UPLINK_PERIOD_MS,
                                      LORAE5_APP_STARTUP_DELAY_MS,
                                      LORAE5_APP_DEV_EUI,
                                      LORAE5_APP_APP_EUI,
                                      LORAE5_APP_APP_KEY) != LORAE5_OK)
  {
    Error_Handler();
  }

  printf("INFO  OTAA managé lancé\r\n");
}

static void LoraManaged_AppTask(void)
{
  char line[LORAE5_RX_LINE_MAX];
  uint32_t now = HAL_GetTick();

  LORAE5_Task(&hlorae5, now);

  while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
  {
    printf("[LORA] %s\r\n", line);
  }

  if (LORAE5_ConsumeJoinOkEvent(&hlorae5))
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
    printf("[LORA] JOIN OK\r\n");
  }

  if (LORAE5_ConsumeJoinFailEvent(&hlorae5))
  {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    printf("[LORA] JOIN FAIL, retry auto\r\n");
  }

  /* Downlink reçu depuis le serveur LoRaWAN */
  {
    uint8_t dl_port = 0U;
    char    dl_data[64];
    bool    dl_hex   = false;
    if (LORAE5_ConsumeDownlinkEvent(&hlorae5, &dl_port, dl_data, sizeof(dl_data), &dl_hex))
    {
      printf("[LORA] Downlink port=%u hex=%d : %s\r\n",
             (unsigned)dl_port, (int)dl_hex, dl_data);
    }
  }

  /* DeInit : demonstré après 60 s pour libérer proprement la ressource */
  if ((deinit_done == 0U) && (now >= 60000U))
  {
    LORAE5_StopRx(&hlorae5);
    LORAE5_DeInit(&hlorae5);
    deinit_done = 1U;
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    printf("[COV] LORAE5_DeInit OK — handle libéré à t=%lums\r\n", (unsigned long)now);
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
