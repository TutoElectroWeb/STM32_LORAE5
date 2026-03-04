/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : exemple_lorae5_api_coverage.c
  * @brief          : Demo de couverture API STM32_LORAE5
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
#define LOG_NAME "exemple_lorae5_api_coverage"  ///< Nom du journal pour affichage bannière
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

static volatile uint32_t line_cb_count = 0U;
static uint8_t coverage_step = 0U;
static uint32_t next_action_ms = 0U;
static uint8_t uplink_hex_sent = 0U;
static uint8_t reset_done = 0U;
static uint8_t sendmsg_done = 0U;      ///< Garde SendMsg (ASCII) en une seule fois
static uint8_t overflow_clear_done = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void LoraCoverage_AppInit(void);
static void LoraCoverage_AppTask(void);
static void LoraCoverage_OnLine(const char *line, void *user_ctx);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}

static void LoraCoverage_OnLine(const char *line, void *user_ctx)
{
  (void)line;
  (void)user_ctx;
  line_cb_count++;
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
  printf("  LoRa-E5 - API coverage demo\r\n");
  printf("========================================\r\n\r\n");

  LoraCoverage_AppInit();
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    LoraCoverage_AppTask();
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
static void LoraCoverage_AppInit(void)
{
  if (LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma)) != LORAE5_OK)
  {
    Error_Handler();
  }

  if (LORAE5_StartRx(&hlorae5) != LORAE5_OK)
  {
    Error_Handler();
  }

  /* SetRegion + Join : couverts ici, erreur attendue si pas de clés */
  LORAE5_Status st = LORAE5_SetRegion(&hlorae5, LORAE5_APP_REGION);
  printf("[COV] LORAE5_SetRegion  -> %s\r\n", LORAE5_StatusToString(st));

  st = LORAE5_Join(&hlorae5);
  printf("[COV] LORAE5_Join       -> %s\r\n", LORAE5_StatusToString(st));

  coverage_step = 0U;
  next_action_ms = HAL_GetTick() + 300U;
  uplink_hex_sent = 0U;
  reset_done = 0U;
  sendmsg_done = 0U;
  overflow_clear_done = 0U;

  printf("[COV] cible: SetRegion, Join, StatusToString, SetLineCallback, SendRaw, Reset, StartManagedOtaa, IsJoined, SendMsg, SendMsgHex, GetOverflowCount, ClearOverflowCount\r\n");
}

static void LoraCoverage_AppTask(void)
{
  char line[LORAE5_RX_LINE_MAX];
  uint32_t now = HAL_GetTick();
  static uint32_t last_cb_print_ms = 0U;

  LORAE5_Task(&hlorae5, now);

  while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
  {
    printf("[LORA] %s\r\n", line);
  }

  if ((now - last_cb_print_ms) >= 2000U)
  {
    printf("[APP] line_cb_count=%lu  rx_overflow=%u\r\n",
           (unsigned long)line_cb_count,
           (unsigned)LORAE5_GetOverflowCount(&hlorae5));

    if (overflow_clear_done == 0U)
    {
      LORAE5_ClearOverflowCount(&hlorae5);
      printf("[COV] LORAE5_ClearOverflowCount OK\r\n");
      overflow_clear_done = 1U;
    }
    last_cb_print_ms = now;
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

  {
    uint8_t dl_port = 0U;
    char    dl_data[LORAE5_RX_LINE_MAX];
    bool    dl_hex  = false;
    if (LORAE5_ConsumeDownlinkEvent(&hlorae5, &dl_port, dl_data, sizeof(dl_data), &dl_hex))
    {
      printf("[LORA] DOWNLINK port=%u %s: %s\r\n",
             dl_port, dl_hex ? "(hex)" : "(ascii)", dl_data);
    }
  }

  if ((int32_t)(now - next_action_ms) < 0)
  {
    if (LORAE5_IsJoined(&hlorae5) && (uplink_hex_sent == 0U))
    {
      if (LORAE5_SendMsgHex(&hlorae5, "48656C6C6F2D434F564552414745", false) == LORAE5_OK)
      {
        uplink_hex_sent = 1U;
        printf("[APP] SendMsgHex OK\r\n");
      }
    }
    return;
  }

  switch (coverage_step)
  {
    case 0:
      if (LORAE5_StopRx(&hlorae5) == LORAE5_OK)
      {
        printf("[COV] LORAE5_StopRx OK\r\n");
        coverage_step = 1U;
        next_action_ms = now + 120U;
      }
      break;

    case 1:
      if (LORAE5_StartRx(&hlorae5) == LORAE5_OK)
      {
        printf("[COV] LORAE5_StartRx OK\r\n");
        coverage_step = 2U;
        next_action_ms = now + 120U;
      }
      break;

    case 2:
      LORAE5_SetLineCallback(&hlorae5, LoraCoverage_OnLine, NULL);
      printf("[COV] LORAE5_SetLineCallback OK\r\n");
      coverage_step = 3U;
      next_action_ms = now + 200U;
      break;

    case 3:
      if (LORAE5_SendRaw(&hlorae5, "AT\r\n") == LORAE5_OK)
      {
        printf("[COV] LORAE5_SendRaw OK\r\n");
        coverage_step = 4U;
        next_action_ms = now + 600U;
      }
      break;

    case 4:
      if (LORAE5_Reset(&hlorae5) == LORAE5_OK)
      {
        printf("[COV] LORAE5_Reset OK\r\n");
        reset_done = 1U;
        coverage_step = 5U;
        next_action_ms = now + 2500U;
      }
      break;

    case 5:
      if (reset_done != 0U)
      {
        if (LORAE5_StartManagedOtaa(&hlorae5,
                                    LORAE5_APP_REGION,
                                    LORAE5_APP_UPLINK_PAYLOAD,
                                    LORAE5_APP_UPLINK_PERIOD_MS,
                                    LORAE5_APP_STARTUP_DELAY_MS) == LORAE5_OK)
        {
          printf("[COV] LORAE5_StartManagedOtaa OK\r\n");
          coverage_step = 6U;
          next_action_ms = now + 500U;
        }
      }
      break;

    default:
      if (LORAE5_IsJoined(&hlorae5))
      {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
        if (sendmsg_done == 0U)
        {
          /* SendMsg ASCII (complément de SendMsgHex déjà couvert ci-dessus) */
          LORAE5_Status st = LORAE5_SendMsg(&hlorae5, LORAE5_APP_UPLINK_PAYLOAD, false);
          printf("[COV] LORAE5_SendMsg    -> %s\r\n", LORAE5_StatusToString(st));
          sendmsg_done = 1U;
        }
      }
      else
      {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
      }
      next_action_ms = now + 500U;
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
  __disable_irq();                                                /* Désactive les IRQ — HAL_Delay et HAL_GetTick ne fonctionnent plus */
    LORAE5_DeInit(&hlorae5);
    while (1)
  {
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);                   /* Clignotement LED LD2 — diagnostic visuel sans debugger */
    for (volatile uint32_t wait = 0U; wait < 250000U; ++wait)     /* ~250 ms sur Nucleo 80 MHz (1 NOP ≈ 1 cycle) */
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
