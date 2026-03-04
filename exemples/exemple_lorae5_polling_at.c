/**
 *******************************************************************************
 * @file    exemple_lorae5_polling_at.c
 * @author  manu
 * @brief   Exemple polling (bloquant) : commandes AT brutes vers LoRa-E5.
 *
 *          Démontre :
 *            - Envoi direct de commandes AT (HAL_UART_Transmit bloquant)
 *            - Lecture de la réponse via HAL_UART_Receive polling (pas de DMA)
 *            - Pattern minimal pour valider la connexion physique LoRa-E5
 *
 *          Mode polling = sans DMA, utile pour debug initial ou prototypage.
 *          Pour un usage complet, utiliser LORAE5_Init() + DMA.
 *
 * @par     Câblage minimum (Nucleo-L476RG)
 *           USART1 PA9  = TX → RX LoRa-E5
 *           USART1 PA10 = RX ← TX LoRa-E5
 *           Baudrate LoRa-E5 : 9600 baud (défaut firmware)
 *
 * @version 0.9.2
 * @date    2026-02-26
 * @copyright Copyright (c) 2026 manu — Licence MIT
 *******************************************************************************
 */

/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
/* USER CODE BEGIN Includes */
#define LOG_NAME "exemple_lorae5_polling_at"  ///< Identifiant log série banner
#include "STM32_LORAE5.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AT_TX_TIMEOUT_MS  1000U  ///< Timeout HAL_UART_Transmit polling (ms)
#define AT_RX_TIMEOUT_MS  3000U  ///< Timeout HAL_UART_Receive polling (ms)
#define AT_RX_BUF_SIZE      192U ///< Taille buffer réponse AT brute
#define AT_DELAY_BETWEEN_MS 500U ///< Délai inter-commandes (ms)
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
UART_HandleTypeDef huart1;  ///< USART1 — LoRa-E5 (polling, sans DMA)
UART_HandleTypeDef huart2;  ///< USART2 — debug printf
static LORAE5_Handle_t g_lora_demo;       ///< Handle demo pour couvrir Init/DeInit API
static uint8_t g_lora_demo_dma[64];       ///< Buffer RX minimal pour Init API
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static uint16_t at_send_recv(const char *cmd, char *resp, uint16_t resp_size);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/**
 * @brief  Redirige printf → USART2.
 */
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1U, 10U);
  return ch;
}

/**
 * @brief  Envoie une commande AT et lit la réponse (polling bloquant).
 * @param  cmd       Commande AT sans CRLF (ex: "AT", "AT+ID")
 * @param  resp      Buffer de réponse
 * @param  resp_size Taille du buffer
 * @retval Nombre d'octets reçus (0 = timeout ou TX error)
 */
static uint16_t at_send_recv(const char *cmd, char *resp, uint16_t resp_size)
{
  char tx_buf[AT_RX_BUF_SIZE];
  int  len = snprintf(tx_buf, sizeof(tx_buf), "%s\r\n", cmd);
  if (len <= 0) { return 0U; }

  /* TX bloquant */
  if (HAL_UART_Transmit(&huart1, (uint8_t *)tx_buf, (uint16_t)len,
                        AT_TX_TIMEOUT_MS) != HAL_OK)
  {
    printf("[AT] TX error cmd=%s\r\n", cmd);
    return 0U;
  }

  /* RX bloquant — attend la réponse module */
  memset(resp, 0, resp_size);
  HAL_StatusTypeDef st = HAL_UART_Receive(&huart1, (uint8_t *)resp,
                                          resp_size - 1U, AT_RX_TIMEOUT_MS);
  if (st == HAL_OK || st == HAL_TIMEOUT) {
    /* HAL_TIMEOUT = réponse partielle : on garde ce qui a été reçu */
    return (uint16_t)strlen(resp);
  }
  printf("[AT] RX error\r\n");
  return 0U;
}
/* USER CODE END 0 */

/**
 * @brief  Application principale — polling AT LoRa-E5.
 */
int main(void)
{
  /* MCU Configuration ------------------------------------------------------- */
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  LORAE5_Status st = LORAE5_Init(&g_lora_demo, &huart1,
                                 g_lora_demo_dma, sizeof(g_lora_demo_dma));
  printf("[INFO] LORAE5_Init demo: %s\r\n", LORAE5_StatusToString(st));
  if (st == LORAE5_OK) {
    st = LORAE5_DeInit(&g_lora_demo);
    printf("[INFO] LORAE5_DeInit demo: %s\r\n", LORAE5_StatusToString(st));
  }

  char resp[AT_RX_BUF_SIZE];

  printf("\r\n=== " LOG_NAME " (%s %s) ===\r\n", __DATE__, __TIME__);
  printf("Polling AT direct LoRa-E5 (9600 bps, sans DMA)\r\n\r\n");

  /* 1. Ping */
  printf("[AT] << AT\r\n");
  at_send_recv("AT", resp, sizeof(resp));
  printf("[AT] >> %s\r\n", resp);
  HAL_Delay(AT_DELAY_BETWEEN_MS);

  /* 2. Version firmware */
  printf("[AT] << AT+VER\r\n");
  at_send_recv("AT+VER", resp, sizeof(resp));
  printf("[AT] >> %s\r\n", resp);
  HAL_Delay(AT_DELAY_BETWEEN_MS);

  /* 3. DevEUI */
  printf("[AT] << AT+ID=DevEui\r\n");
  at_send_recv("AT+ID=DevEui", resp, sizeof(resp));
  printf("[AT] >> %s\r\n", resp);
  HAL_Delay(AT_DELAY_BETWEEN_MS);

  /* 4. Reset module */
  printf("[AT] << AT+RESET\r\n");
  at_send_recv("AT+RESET", resp, sizeof(resp));
  printf("[AT] >> %s\r\n", resp);
  HAL_Delay(AT_DELAY_BETWEEN_MS);
  /* USER CODE END 2 */

  /* Infinite loop ----------------------------------------------------------- */
  /* USER CODE BEGIN WHILE */
  printf("\r\n[POLL] Boucle : AT toutes les 5 s\r\n");
  uint32_t last_ms = HAL_GetTick();

  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    if ((now - last_ms) >= 5000U)
    {
      last_ms = now;
      at_send_recv("AT", resp, sizeof(resp));
      printf("[POLL] AT resp: %s\r\n", *resp ? resp : "(timeout)");
    }
    HAL_Delay(10U);
    /* USER CODE END 3 */
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
 * @brief  Gestionnaire d'erreur.
 */
void Error_Handler(void)
{
  __disable_irq();
  printf("[WARN] Error_Handler reached\r\n");
  while (1)
  {
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    for (volatile uint32_t wait = 0U; wait < 250000U; ++wait) {
      __NOP();
    }
  }
}

/**
 * @brief  Initialisation GPIO — LED LD2 (PA5).
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOA, LD2_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin   = LD2_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
 * @brief  Initialisation USART1 — 9600 baud, 8N1 (LoRa-E5).
 */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 9600U;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

/**
 * @brief  Initialisation USART2 — 115200 baud, 8N1 (debug printf).
 */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance          = USART2;
  huart2.Init.BaudRate     = 115200U;
  huart2.Init.WordLength   = UART_WORDLENGTH_8B;
  huart2.Init.StopBits     = UART_STOPBITS_1;
  huart2.Init.Parity       = UART_PARITY_NONE;
  huart2.Init.Mode         = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) { Error_Handler(); }
}

/**
 * @brief  Stub SystemClock_Config — remplacé par la config CubeMX réelle.
 */
void SystemClock_Config(void)
{
  /* Généré par STM32CubeMX selon la cible (HSI 4 MHz par défaut) */
}
