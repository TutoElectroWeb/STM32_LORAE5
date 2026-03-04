/**
 *******************************************************************************
 * @file    STM32_LORAE5_gateway.h
 * @author  manu
 * @brief   Module passerelle LoRaWAN : FSM OTAA managée (join, uplink périodique, retry).
 *          Couche "gateway" au-dessus du driver core STM32_LORAE5.
 *          À inclure dans les projets qui utilisent la gestion automatique OTAA.
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Libre sous licence MIT.
 *
 * ## Architecture
 *
 * | Fichier                              | Rôle                                        |
 * |--------------------------------------|---------------------------------------------|
 * | STM32_LORAE5.h / .c                  | Driver core : UART/DMA, AT, queue, évènts   |
 * | STM32_LORAE5_gateway.h / .c (ici)   | FSM OTAA : join auto, retry, uplink pério.   |
 * | STM32_LORAE5_broadcast.h / .c        | RF P2P : AT+TEST, TXLRPKT, RXLRPKT         |
 *
 * ## Utilisation typique
 *
 * @code
 * #include "STM32_LORAE5_gateway.h"   // inclut déjà STM32_LORAE5.h
 *
 * LORAE5_Handle_t lora;
 * uint8_t dma_buf[128];
 *
 * LORAE5_Init(&lora, &huart1, dma_buf, sizeof(dma_buf));
 * LORAE5_StartRx(&lora);
 * LORAE5_StartManagedOtaa(&lora, "EU868", "HELLO", 15000, 500);
 *
 * while (1) {
 *     LORAE5_Task(&lora, HAL_GetTick());
 *
 *     char line[LORAE5_RX_LINE_MAX];
 *     while (LORAE5_PopLine(&lora, line, sizeof(line))) { }
 *
 *     if (LORAE5_ConsumeJoinOkEvent(&lora))   { }   // joint
 *     if (LORAE5_ConsumeJoinFailEvent(&lora)) { }   // echec
 * }
 * @endcode
 *
 * ## Callbacks IRQ (à brancher dans stm32xx_it.c)
 * @code
 * void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
 *     if (huart == &huart1) LORAE5_OnRxEvent(&lora, Size);
 * }
 * void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
 *     if (huart == &huart1) LORAE5_OnTxCplt(&lora);
 * }
 * @endcode
 *******************************************************************************
 */
#ifndef STM32_LORAE5_GATEWAY_H
#define STM32_LORAE5_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "STM32_LORAE5.h"

/* ── FSM OTAA managée ───────────────────────────────────────────────────────────── */

/**
 * @brief  Démarre la machine d'état OTAA managée (clés depuis STM32_LORAE5_conf.h).
 * @param  handle            Handle actif (non NULL)
 * @param  region            Région LoRaWAN (non NULL, ex : "EU868")
 * @param  periodic_payload  Payload uplink périodique (non NULL)
 * @param  uplink_period_ms  Période uplink en ms (> 0)
 * @param  startup_delay_ms  Délai initial avant la première action AT (ms)
 * @retval LORAE5_OK          Démarrage réussi
 * @retval LORAE5_ERR_NULL_PTR    Paramètre NULL
 * @retval LORAE5_ERR_ARG     uplink_period_ms == 0
 * @note   Les clés DevEUI/AppEUI/AppKey sont lues depuis STM32_LORAE5_conf.h
 *         (LORAE5_APP_DEV_EUI, LORAE5_APP_APP_EUI, LORAE5_APP_APP_KEY).
 * @note   Appeler LORAE5_Task() périodiquement depuis la boucle principale.
 */
LORAE5_Status LORAE5_StartManagedOtaa(LORAE5_Handle_t *handle,
                                      const char *region,
                                      const char *periodic_payload,
                                      uint32_t uplink_period_ms,
                                      uint32_t startup_delay_ms);

/**
 * @brief  Démarre la machine d'état OTAA managée avec clés explicites.
 * @param  handle            Handle actif (non NULL)
 * @param  region            Région LoRaWAN (non NULL)
 * @param  periodic_payload  Payload uplink périodique (non NULL)
 * @param  uplink_period_ms  Période uplink en ms (> 0)
 * @param  startup_delay_ms  Délai initial en ms
 * @param  dev_eui           DevEUI 16 hex chars ou NULL (valeur du module utilisée)
 * @param  app_eui           AppEUI 16 hex chars ou NULL
 * @param  app_key           AppKey 32 hex chars ou NULL
 * @retval LORAE5_OK          Démarrage réussi
 * @retval LORAE5_ERR_NULL_PTR    Paramètre obligatoire NULL
 * @retval LORAE5_ERR_ARG     Format clé invalide ou uplink_period_ms == 0
 * @note   Appeler LORAE5_Task() périodiquement depuis la boucle principale.
 */
LORAE5_Status LORAE5_StartManagedOtaaWithKeys(LORAE5_Handle_t *handle,
                                              const char *region,
                                              const char *periodic_payload,
                                              uint32_t uplink_period_ms,
                                              uint32_t startup_delay_ms,
                                              const char *dev_eui,
                                              const char *app_eui,
                                              const char *app_key);

/**
 * @brief  Avance la machine d'état OTAA managée — appeler depuis while(1).
 * @param  handle  Handle actif (ignoré si NULL ou managed_enabled == false)
 * @param  now_ms  Valeur courante de HAL_GetTick()
 * @pre    LORAE5_StartManagedOtaa* doit avoir été appelé.
 * @pre    now_ms doit être HAL_GetTick() (monotone, non réinitialisé).
 * @note   Non-bloquant. Gère automatiquement : ping, version, mode, région,
 *         injection des clés (si fournies), join, uplink périodique et retry.
 * @note   Timer anti-wraparound : utilise `(int32_t)(now_ms - target) >= 0`.
 */
void LORAE5_Task(LORAE5_Handle_t *handle, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* STM32_LORAE5_GATEWAY_H */
