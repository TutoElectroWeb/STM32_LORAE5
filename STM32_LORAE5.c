/**
 *******************************************************************************
 * @file    STM32_LORAE5.c
 * @author  manu
 * @brief   Implémentation du driver AT-command STM32_LORAE5.
 * @version 1.0.0
 * @date    2026-02-26
 * @copyright Libre sous licence MIT.
 *******************************************************************************
 */

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "main.h"
#include "STM32_LORAE5.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Private macros
 * ============================================================================ */
#if (LORAE5_DEBUG_ENABLE)
#  define LORAE5_LOG(fmt, ...) printf("[LORAE5] " fmt "\r\n", ##__VA_ARGS__)
#else
#  define LORAE5_LOG(fmt, ...) ((void)0)
#endif

/* ============================================================================
 * Private function prototypes
 * ============================================================================ */
static LORAE5_Status lorae5_send_fmt(LORAE5_Handle_t *handle, const char *fmt_rn, const char *arg); /* fmt_rn doit inclure \r\n */
static void lorae5_feed_char(LORAE5_Handle_t *handle, char ch);
static void lorae5_emit_line(LORAE5_Handle_t *handle);
static bool lorae5_line_contains(const char *line, const char *token);
static bool lorae5_is_non_empty(const char *s);
static bool lorae5_is_hex_char(char c);
static bool lorae5_is_hex_string_exact(const char *s, size_t expected_len);
static void lorae5_term_write(UART_HandleTypeDef *uart, const char *s);
static void lorae5_parse_downlink(LORAE5_Handle_t *handle, const char *line, bool is_hex);

/* ============================================================================
 * Exported functions
 * ============================================================================ */

LORAE5_Status LORAE5_Init(LORAE5_Handle_t *handle,
                          UART_HandleTypeDef *huart,
                          uint8_t *rx_dma_buffer,
                          uint16_t rx_dma_buffer_size) {
  if ((handle == NULL) || (huart == NULL) || (rx_dma_buffer == NULL) || (rx_dma_buffer_size == 0U)) {
    return LORAE5_ERR_NULL_PTR;
  }

  memset(handle, 0, sizeof(*handle));
  handle->huart         = huart;
  handle->initialized   = true;
  handle->rx_dma = rx_dma_buffer;
  handle->rx_dma_size = rx_dma_buffer_size;

  return LORAE5_OK;
}

LORAE5_Status LORAE5_DeInit(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (handle->huart != NULL) {
    (void)HAL_UART_DMAStop(handle->huart);
  }

  memset(handle, 0, sizeof(*handle));
  return LORAE5_OK;
}

LORAE5_Status LORAE5_StartRx(LORAE5_Handle_t *handle) {
  if ((handle == NULL) || (handle->huart == NULL) || (handle->rx_dma == NULL) || (handle->rx_dma_size == 0U)) {
    return LORAE5_ERR_NULL_PTR;
  }

  handle->rx_last_pos = 0U;
  handle->line_len = 0U;
  handle->line_overflow = false;
  handle->line_ready = false;
  handle->line_q_head = 0U;
  handle->line_q_tail = 0U;

  if (HAL_UARTEx_ReceiveToIdle_DMA(handle->huart, handle->rx_dma, handle->rx_dma_size) != HAL_OK) {
    return LORAE5_ERR_UART;
  }

  if (handle->huart->hdmarx != NULL) {
    __HAL_DMA_DISABLE_IT(handle->huart->hdmarx, DMA_IT_HT);
  }
  return LORAE5_OK;
}

LORAE5_Status LORAE5_StopRx(LORAE5_Handle_t *handle) {
  if ((handle == NULL) || (handle->huart == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (HAL_UART_DMAStop(handle->huart) != HAL_OK) {
    return LORAE5_ERR_UART;
  }

  return LORAE5_OK;
}

void LORAE5_SetLineCallback(LORAE5_Handle_t *handle, LORAE5_LineCallback cb, void *user_ctx) {
  if (handle == NULL) {
    return;
  }

  handle->on_line = cb;
  handle->on_line_ctx = user_ctx;
}

void LORAE5_OnRxEvent(LORAE5_Handle_t *handle, uint16_t dma_position) {
  uint16_t old_pos;

  if ((handle == NULL) || (handle->rx_dma == NULL) || (handle->rx_dma_size == 0U)) {
    return;
  }

  old_pos = handle->rx_last_pos;
  if (dma_position > handle->rx_dma_size) {
    dma_position = handle->rx_dma_size;
  }

  if (dma_position >= old_pos) {
    for (uint16_t i = old_pos; i < dma_position; i++) {
      lorae5_feed_char(handle, (char)handle->rx_dma[i]);
    }
  } else {
    for (uint16_t i = old_pos; i < handle->rx_dma_size; i++) {
      lorae5_feed_char(handle, (char)handle->rx_dma[i]);
    }
    for (uint16_t i = 0U; i < dma_position; i++) {
      lorae5_feed_char(handle, (char)handle->rx_dma[i]);
    }
  }

  handle->rx_last_pos = dma_position;
}

void LORAE5_OnTxCplt(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return;
  }

  handle->tx_busy = false;
}

LORAE5_Status LORAE5_SendRaw(LORAE5_Handle_t *handle, const char *data) {
  size_t len;
  LORAE5_Status st;

  if ((handle == NULL) || (handle->huart == NULL) || (data == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (handle->tx_busy) {
    handle->last_error = LORAE5_ERR_BUSY;
    return LORAE5_ERR_BUSY;
  }

  len = strlen(data);
  if (len == 0U) {
    handle->last_error = LORAE5_ERR_ARG;
    return LORAE5_ERR_ARG;
  }
  if (len > 65535U) {
    handle->last_error = LORAE5_ERR_ARG;
    return LORAE5_ERR_ARG;
  }

  handle->tx_busy = true;

  if (handle->huart->hdmatx != NULL) {
    if (HAL_UART_Transmit_DMA(handle->huart, (uint8_t *)data, (uint16_t)len) != HAL_OK) {
      handle->tx_busy = false;
      handle->last_error = LORAE5_ERR_UART;
      if (handle->consecutive_errors < 255U) {
        handle->consecutive_errors++;
      }
      return LORAE5_ERR_UART;
    }
  } else {
    if (HAL_UART_Transmit_IT(handle->huart, (uint8_t *)data, (uint16_t)len) != HAL_OK) {
      handle->tx_busy = false;
      handle->last_error = LORAE5_ERR_UART;
      if (handle->consecutive_errors < 255U) {
        handle->consecutive_errors++;
      }
      return LORAE5_ERR_UART;
    }
  }

  st = LORAE5_OK;
  handle->last_error = st;
  handle->consecutive_errors = 0U;
  return st;
}

LORAE5_Status LORAE5_SendAT(LORAE5_Handle_t *handle, const char *cmd) {
  int written;

  if ((handle == NULL) || (cmd == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  written = snprintf((char *)handle->tx_buf, sizeof(handle->tx_buf), "%s\r\n", cmd);
  if ((written <= 0) || ((size_t)written >= sizeof(handle->tx_buf))) {
    return LORAE5_ERR_ARG;
  }

  return LORAE5_SendRaw(handle, (const char *)handle->tx_buf);
}

LORAE5_Status LORAE5_Ping(LORAE5_Handle_t *handle) {
  return LORAE5_SendAT(handle, "AT");
}

LORAE5_Status LORAE5_Reset(LORAE5_Handle_t *handle) {
  return LORAE5_SendAT(handle, "AT+RESET");
}

LORAE5_Status LORAE5_GetVersion(LORAE5_Handle_t *handle) {
  return LORAE5_SendAT(handle, "AT+VER");
}

LORAE5_Status LORAE5_SetMode(LORAE5_Handle_t *handle, LORAE5_Mode mode) {
  switch (mode) {
  case LORAE5_MODE_LWOTAA:
    return LORAE5_SendAT(handle, "AT+MODE=LWOTAA");
  case LORAE5_MODE_LWABP:
    return LORAE5_SendAT(handle, "AT+MODE=LWABP");
  case LORAE5_MODE_TEST:
    return LORAE5_SendAT(handle, "AT+MODE=TEST");
  default:
    return LORAE5_ERR_ARG;
  }
}

LORAE5_Status LORAE5_SetRegion(LORAE5_Handle_t *handle, const char *region) {
  return lorae5_send_fmt(handle, "AT+DR=%s\r\n", region);
}

LORAE5_Status LORAE5_Join(LORAE5_Handle_t *handle) {
  return LORAE5_SendAT(handle, "AT+JOIN");
}

LORAE5_Status LORAE5_SendMsg(LORAE5_Handle_t *handle, const char *payload_ascii, bool confirmed) {
  if (payload_ascii == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (confirmed) {
    return lorae5_send_fmt(handle, "AT+CMSG=\"%s\"\r\n", payload_ascii);
  }

  return lorae5_send_fmt(handle, "AT+MSG=\"%s\"\r\n", payload_ascii);
}

LORAE5_Status LORAE5_SendMsgHex(LORAE5_Handle_t *handle, const char *payload_hex, bool confirmed) {
  if (payload_hex == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (confirmed) {
    return lorae5_send_fmt(handle, "AT+CMSGHEX=\"%s\"\r\n", payload_hex);
  }

  return lorae5_send_fmt(handle, "AT+MSGHEX=\"%s\"\r\n", payload_hex);
}

LORAE5_Status LORAE5_StartManagedOtaa(LORAE5_Handle_t *handle,
                                      const char *region,
                                      const char *periodic_payload,
                                      uint32_t uplink_period_ms,
                                      uint32_t startup_delay_ms) {
  return LORAE5_StartManagedOtaaWithKeys(handle,
                                         region,
                                         periodic_payload,
                                         uplink_period_ms,
                                         startup_delay_ms,
                                         LORAE5_APP_DEV_EUI,
                                         LORAE5_APP_APP_EUI,
                                         LORAE5_APP_APP_KEY);
}

LORAE5_Status LORAE5_StartManagedOtaaWithKeys(LORAE5_Handle_t *handle,
                                              const char *region,
                                              const char *periodic_payload,
                                              uint32_t uplink_period_ms,
                                              uint32_t startup_delay_ms,
                                              const char *dev_eui,
                                              const char *app_eui,
                                              const char *app_key) {
  if ((handle == NULL) || (region == NULL) || (periodic_payload == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  if (uplink_period_ms == 0U) {
    return LORAE5_ERR_ARG;
  }

  if (lorae5_is_non_empty(dev_eui) && !lorae5_is_hex_string_exact(dev_eui, 16U)) {
    return LORAE5_ERR_ARG;
  }

  if (lorae5_is_non_empty(app_eui) && !lorae5_is_hex_string_exact(app_eui, 16U)) {
    return LORAE5_ERR_ARG;
  }

  if (lorae5_is_non_empty(app_key) && !lorae5_is_hex_string_exact(app_key, 32U)) {
    return LORAE5_ERR_ARG;
  }

  handle->managed_enabled = true;
  handle->managed_region = region;
  handle->managed_payload = periodic_payload;
  handle->managed_dev_eui = dev_eui;
  handle->managed_app_eui = app_eui;
  handle->managed_app_key = app_key;
  handle->managed_uplink_period_ms = uplink_period_ms;
  /* L'addition peut déborder uint32_t si HAL_GetTick() est proche de UINT32_MAX.
   * C'est intentionnel : la comparaison FSM utilise (int32_t)(now_ms - target) >= 0,
   * qui gère correctement le wraparound en arithmétique modulaire 2^32. */
  handle->managed_next_action_ms = HAL_GetTick() + startup_delay_ms;
  handle->managed_last_uplink_ms = 0U;
  handle->managed_step = 0U;
  handle->joined = false;
  handle->evt_join_ok = false;
  handle->evt_join_fail = false;

  return LORAE5_OK;
}

void LORAE5_Task(LORAE5_Handle_t *handle, uint32_t now_ms) {
  bool snap_join_ok;
  bool snap_join_fail;

  if ((handle == NULL) || (!handle->managed_enabled)) {
    return;
  }

  /* Snapshot atomique des flags volatils posés depuis ISR.
   * Lecture protégée par section critique courte (Cortex-M0 : bool non atomique).
   * Les flags ne sont pas remis à zéro ici — c'est le rôle de Consume*Event(). */
  __disable_irq();
  snap_join_ok   = handle->evt_join_ok;
  snap_join_fail = handle->evt_join_fail;
  __enable_irq();

  /* Watchdog JOIN : si AT+JOIN n'a pas reçu de réponse dans LORAE5_JOIN_TIMEOUT_MS,
   * forcer un retry sans attendre evt_join_fail (module silencieux, perte UART…). */
  if ((handle->managed_step == 8U) &&
      (!handle->joined) &&
      (handle->managed_join_deadline_ms != 0U) &&
      ((int32_t)(now_ms - handle->managed_join_deadline_ms) >= 0)) {
    handle->managed_join_deadline_ms = 0U;
    handle->managed_step             = 7U;
    handle->managed_next_action_ms   = now_ms + LORAE5_JOIN_RETRY_DELAY_MS;
  }

  if (snap_join_fail && !snap_join_ok) {
    handle->joined                   = false;
    handle->managed_join_deadline_ms = 0U;
    handle->managed_step             = 7U;
    handle->managed_next_action_ms   = now_ms + 5000U;
  }

  if (snap_join_ok) {
    handle->joined                   = true;
    handle->managed_join_deadline_ms = 0U;  /* watchdog désarmé */
    handle->managed_step             = 8U;
  }

  if ((int32_t)(now_ms - handle->managed_next_action_ms) >= 0) {
    switch (handle->managed_step) {
    case 0:
      if (LORAE5_Ping(handle) == LORAE5_OK) {
        handle->managed_step = 1U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 1:
      if (LORAE5_GetVersion(handle) == LORAE5_OK) {
        handle->managed_step = 2U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 2:
      if (LORAE5_SetMode(handle, LORAE5_MODE_LWOTAA) == LORAE5_OK) {
        handle->managed_step = 3U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 3:
      if (LORAE5_SetRegion(handle, handle->managed_region) == LORAE5_OK) {
        if (lorae5_is_non_empty(handle->managed_dev_eui)) {
          handle->managed_step = 4U;
        } else if (lorae5_is_non_empty(handle->managed_app_eui)) {
          handle->managed_step = 5U;
        } else if (lorae5_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 4:
      if (lorae5_send_fmt(handle, "AT+ID=DevEui,\"%s\"\r\n", handle->managed_dev_eui) == LORAE5_OK) {
        if (lorae5_is_non_empty(handle->managed_app_eui)) {
          handle->managed_step = 5U;
        } else if (lorae5_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 5:
      if (lorae5_send_fmt(handle, "AT+ID=AppEui,\"%s\"\r\n", handle->managed_app_eui) == LORAE5_OK) {
        if (lorae5_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 6:
      if (lorae5_send_fmt(handle, "AT+KEY=APPKEY,\"%s\"\r\n", handle->managed_app_key) == LORAE5_OK) {
        handle->managed_step = 7U;
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 7:
      if (LORAE5_Join(handle) == LORAE5_OK) {
        handle->managed_step              = 8U;
        handle->managed_next_action_ms    = now_ms + 10000U;           /* wraparound intentionnel — voir comparaison (int32_t) ci-dessus */
        handle->managed_join_deadline_ms  = now_ms + LORAE5_JOIN_TIMEOUT_MS; /* idem */
      }
      break;

    default:
      break;
    }
  }

  if (handle->joined) {
    /* Soustraction uint32_t modulo 2^32 : correcte pour elapsed >= period (anti-wraparound implicite).
     * Différent des comparaisons de deadline FSM (cast int32_t) car ici period_ms <= 2^31. */
    if ((now_ms - handle->managed_last_uplink_ms) >= handle->managed_uplink_period_ms) {
      if (LORAE5_SendMsg(handle, handle->managed_payload, false) == LORAE5_OK) {
        handle->managed_last_uplink_ms = now_ms;
      }
    }
  }
}

bool LORAE5_PopLine(LORAE5_Handle_t *handle, char *out, size_t out_size) {
  bool has_line;

  if ((handle == NULL) || (out == NULL) || (out_size == 0U)) {
    return false;
  }

  __disable_irq();
  has_line = (handle->line_q_head != handle->line_q_tail);
  if (!has_line) {
    handle->line_ready = false;
    __enable_irq();
    return false;
  }

  strncpy(out, handle->line_queue[handle->line_q_tail], out_size - 1U);
  out[out_size - 1U] = '\0';
  handle->line_q_tail = (uint16_t)((handle->line_q_tail + 1U) % LORAE5_LINE_QUEUE_DEPTH);
  handle->line_ready = (handle->line_q_head != handle->line_q_tail);
  __enable_irq();
  return true;
}

bool LORAE5_ConsumeJoinOkEvent(LORAE5_Handle_t *handle) {
  bool value;

  if (handle == NULL) {
    return false;
  }

  __disable_irq();
  value = handle->evt_join_ok;
  handle->evt_join_ok = false;
  __enable_irq();
  return value;
}

bool LORAE5_ConsumeJoinFailEvent(LORAE5_Handle_t *handle) {
  bool value;

  if (handle == NULL) {
    return false;
  }

  __disable_irq();
  value = handle->evt_join_fail;
  handle->evt_join_fail = false;
  __enable_irq();
  return value;
}

bool LORAE5_ConsumeDownlinkEvent(LORAE5_Handle_t *handle,
                                 uint8_t *port_out,
                                 char *data_out, size_t data_out_size,
                                 bool *is_hex_out) {
  bool has_event;

  if (handle == NULL) {
    return false;
  }

  __disable_irq();
  has_event = handle->evt_downlink;
  if (has_event) {
    if (port_out != NULL) {
      *port_out = handle->downlink_port;
    }
    if ((data_out != NULL) && (data_out_size > 0U)) {
      strncpy(data_out, handle->downlink_data, data_out_size - 1U);
      data_out[data_out_size - 1U] = '\0';
    }
    if (is_hex_out != NULL) {
      *is_hex_out = handle->downlink_is_hex;
    }
    handle->evt_downlink = false;
  }
  __enable_irq();
  return has_event;
}

bool LORAE5_IsJoined(const LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return false;
  }

  return handle->joined;
}

LORAE5_Status LORAE5_TerminalBegin(LORAE5_Terminal *term,
                                   UART_HandleTypeDef *uart_debug,
                                   LORAE5_Handle_t *lora,
                                   bool echo,
                                   bool show_prompt) {
  if ((term == NULL) || (uart_debug == NULL) || (lora == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  memset(term, 0, sizeof(*term));
  term->uart_debug = uart_debug;
  term->lora = lora;
  term->echo = echo;
  term->show_prompt = show_prompt;

  if (HAL_UART_Receive_IT(term->uart_debug, &term->rx_char, 1U) != HAL_OK) {
    return LORAE5_ERR_UART;
  }

  if (term->show_prompt) {
    lorae5_term_write(term->uart_debug, "\r\n[LORA-TERM] Pret\r\n");
  }

  return LORAE5_OK;
}

void LORAE5_TerminalRxCallback(LORAE5_Terminal *term, UART_HandleTypeDef *huart) {
  uint8_t ch;

  if ((term == NULL) || (huart == NULL) || (term->uart_debug == NULL)) {
    return;
  }

  if (huart != term->uart_debug) {
    return;
  }

  ch = term->rx_char;

  if ((ch == '\r') || (ch == '\n')) {
    if (term->cmd_len > 0U) {
      term->cmd_buf[term->cmd_len] = '\0';
      term->cmd_ready = true;
    }
  } else if ((ch == 0x08U) || (ch == 0x7FU)) {
    if (term->cmd_len > 0U) {
      term->cmd_len--;
    }
  } else {
    if (term->cmd_len < (uint16_t)(LORAE5_TERM_CMD_MAX - 1U)) {
      term->cmd_buf[term->cmd_len++] = (char)ch;
      term->last_input_ms = HAL_GetTick();
    } else {
      term->cmd_buf[term->cmd_len] = '\0';
      term->cmd_ready = true;
    }
  }

  (void)HAL_UART_Receive_IT(term->uart_debug, &term->rx_char, 1U); /* terminal: relance RX IT après chaque octet */
}

void LORAE5_TerminalTask(LORAE5_Terminal *term) {
  char line[LORAE5_RX_LINE_MAX];
  uint32_t now_ms;
  LORAE5_Status st;

  if ((term == NULL) || (term->lora == NULL) || (term->uart_debug == NULL)) {
    return;
  }

  now_ms = HAL_GetTick();

  if (term->show_prompt && (!term->prompt_displayed) && (!term->cmd_ready) && (term->cmd_len == 0U)) {
    lorae5_term_write(term->uart_debug, "[LORA-TERM] === Entrez une commande AT : ");
    term->prompt_displayed = true;
  }

#if (LORAE5_TERM_AUTOSEND_IDLE_MS > 0U)
  if ((!term->cmd_ready) && (term->cmd_len > 0U)) {
    if ((uint32_t)(now_ms - term->last_input_ms) >= LORAE5_TERM_AUTOSEND_IDLE_MS) {
      term->cmd_buf[term->cmd_len] = '\0';
      term->cmd_ready = true;
      lorae5_term_write(term->uart_debug, "\r\n");
    }
  }
#endif

  if (term->cmd_ready) {
    st = LORAE5_SendAT(term->lora, term->cmd_buf);
    if (st == LORAE5_OK) {
      term->cmd_len = 0U;
      term->cmd_ready = false;
      term->prompt_displayed = false;
    } else {
      lorae5_term_write(term->uart_debug, "[LORA-TERM] TX ");
      lorae5_term_write(term->uart_debug, LORAE5_StatusToString(st));
      lorae5_term_write(term->uart_debug, "\r\n");
      term->cmd_len = 0U;
      term->cmd_ready = false;
      term->prompt_displayed = false;
    }
  }

  /* Non-bloquant : vide toutes les lignes disponibles en queue, rend la main
   * immédiatement quand la queue est vide. WCET borné à LORAE5_LINE_QUEUE_DEPTH
   * itérations (pas de timeout HAL_GetTick). */
  for (;;) {
    if (!LORAE5_PopLine(term->lora, line, sizeof(line))) {
      break;
    }
    lorae5_term_write(term->uart_debug, "[LORA] >>> ");
    lorae5_term_write(term->uart_debug, line);
    lorae5_term_write(term->uart_debug, "\r\n");
    term->prompt_displayed = false;
  }
}

/* ============================================================================
 * Private functions
 * ============================================================================ */

static LORAE5_Status lorae5_send_fmt(LORAE5_Handle_t *handle, const char *fmt_rn, const char *arg) {
  int written;

  if ((handle == NULL) || (fmt_rn == NULL) || (arg == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  /* Formate directement dans handle->tx_buf (DMA-safe, persistant).
   * fmt_rn doit déjà inclure "\r\n" — un seul snprintf, pas de buffer stack intermédiaire. */
  written = snprintf((char *)handle->tx_buf, sizeof(handle->tx_buf), fmt_rn, arg);
  if ((written <= 0) || ((size_t)written >= sizeof(handle->tx_buf))) {
    return LORAE5_ERR_ARG;
  }

  return LORAE5_SendRaw(handle, (const char *)handle->tx_buf);
}

static void lorae5_feed_char(LORAE5_Handle_t *handle, char ch) {
  if (ch == '\r') {
    return;
  }

  if (ch == '\n') {
    lorae5_emit_line(handle);
    return;
  }

  if (handle->line_len < (uint16_t)(LORAE5_RX_LINE_MAX - 1U)) {
    handle->line_buf[handle->line_len++] = ch;
  } else {
    handle->line_overflow = true;
  }
}

static void lorae5_emit_line(LORAE5_Handle_t *handle) {
  uint16_t next_head;

  if (handle->line_len == 0U) {
    return;
  }

  if (!handle->line_overflow) {
    handle->line_buf[handle->line_len] = '\0';

    /* Producteur unique (ISR) : pas de __disable_irq nécessaire côté producteur.
     * __DMB() garantit que les données de la queue sont visibles avant que
     * line_q_head soit mis à jour (lu par le consommateur dans le main). */
    strncpy(handle->line_last, handle->line_buf, sizeof(handle->line_last) - 1U);
    handle->line_last[sizeof(handle->line_last) - 1U] = '\0';
    strncpy(handle->line_queue[handle->line_q_head], handle->line_buf, LORAE5_RX_LINE_MAX - 1U);
    handle->line_queue[handle->line_q_head][LORAE5_RX_LINE_MAX - 1U] = '\0';
    next_head = (uint16_t)((handle->line_q_head + 1U) % LORAE5_LINE_QUEUE_DEPTH);
    if (next_head == handle->line_q_tail) {
      /* Queue pleine : on écrase la ligne la plus ancienne */
      handle->line_q_tail = (uint16_t)((handle->line_q_tail + 1U) % LORAE5_LINE_QUEUE_DEPTH);
      handle->last_error = LORAE5_ERR_OVERFLOW;
      if (handle->rx_overflow_count < 255U) {
        handle->rx_overflow_count++;
      }
    }
    __DMB();  /* barrière mémoire : données visibles avant mise à jour de l'indice */
    handle->line_q_head = next_head;
    handle->line_ready = true;

    if (lorae5_line_contains(handle->line_buf, "+JOIN: Network joined") ||
        lorae5_line_contains(handle->line_buf, "+JOIN: Done") ||
        lorae5_line_contains(handle->line_buf, "+JOIN: Joined already")) {
      handle->joined = true;
      handle->evt_join_ok = true;
      handle->evt_join_fail = false;
    } else if (lorae5_line_contains(handle->line_buf, "+JOIN: Join failed")) {
      handle->joined = false;
      handle->evt_join_fail = true;
    }

    /* Parsing downlink : +MSG: PORT: X; RX: "..." ou +MSGHEX: PORT: X; RX: "..." */
    if (lorae5_line_contains(handle->line_buf, "+MSGHEX: PORT:")) {
      lorae5_parse_downlink(handle, handle->line_buf, true);
    } else if (lorae5_line_contains(handle->line_buf, "+MSG: PORT:")) {
      lorae5_parse_downlink(handle, handle->line_buf, false);
    }

    if (handle->on_line != NULL) {
      handle->on_line(handle->line_buf, handle->on_line_ctx);
    }
  }

  handle->line_len = 0U;
  handle->line_overflow = false;
}

static bool lorae5_line_contains(const char *line, const char *token) {
  if ((line == NULL) || (token == NULL)) {
    return false;
  }

  return (strstr(line, token) != NULL);
}

static bool lorae5_is_non_empty(const char *s) {
  return (s != NULL) && (s[0] != '\0');
}

static bool lorae5_is_hex_char(char c) {
  return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

static bool lorae5_is_hex_string_exact(const char *s, size_t expected_len) {
  size_t len;

  if (s == NULL) {
    return false;
  }

  len = strlen(s);
  if (len != expected_len) {
    return false;
  }

  for (size_t i = 0U; i < len; i++) {
    if (!lorae5_is_hex_char(s[i])) {
      return false;
    }
  }

  return true;
}

static void lorae5_term_write(UART_HandleTypeDef *uart, const char *s) {
  size_t len;
  HAL_StatusTypeDef hal_status;

  if ((uart == NULL) || (s == NULL)) {
    return;
  }

  len = strlen(s);
  if (len == 0U) {
    return;
  }

  hal_status = HAL_UART_Transmit(uart, (uint8_t *)s, (uint16_t)len, LORAE5_TERM_TX_TIMEOUT_MS);
  if (hal_status != HAL_OK) {
    return;
  }
}

/**
 * @brief  Parse une ligne de downlink LoRaWAN et stocke port + payload dans le handle.
 * @details Supporte les deux formats du firmware Wio-E5 :
 *            +MSG: PORT: 1; RX: "Hello"
 *            +MSGHEX: PORT: 1; RX: "48656C6C6F"
 */
static void lorae5_parse_downlink(LORAE5_Handle_t *handle, const char *line, bool is_hex) {
  const char *port_ptr;
  const char *rx_ptr;
  const char *quote_open;
  const char *quote_close;
  int port_val = 0;
  size_t data_len;

  if ((handle == NULL) || (line == NULL)) {
    return;
  }

  /* Cherche "PORT:" puis lit l'entier qui suit (avec éventuels espaces) */
  port_ptr = strstr(line, "PORT:");
  if (port_ptr == NULL) { return; }
  port_ptr += 5U;
  for (; *port_ptr == ' '; ++port_ptr) {
  }
  if (sscanf(port_ptr, "%d", &port_val) != 1) { return; }
  if ((port_val < 1) || (port_val > 223)) { return; }  /* port LoRaWAN valide : 1..223 */

  /* Cherche la valeur entre guillemets après "RX:" */
  rx_ptr = strstr(line, "RX:");
  if (rx_ptr == NULL) { return; }
  quote_open = strchr(rx_ptr, '"');
  if (quote_open == NULL) { return; }
  quote_open++; /* saute le guillemet ouvrant */

  quote_close = strchr(quote_open, '"');
  data_len = (quote_close != NULL)
             ? (size_t)(quote_close - quote_open)
             : strlen(quote_open);
  if (data_len >= sizeof(handle->downlink_data)) {
    data_len = sizeof(handle->downlink_data) - 1U;
  }

  /* Producteur unique (ISR) : pas de __disable_irq nécessaire côté producteur.
   * __DMB() garantit que port, data et is_hex sont visibles côté main avant
   * que evt_downlink (le flag consommé par ConsumeDownlinkEvent) soit posé. */
  handle->downlink_port = (uint8_t)((unsigned int)port_val & 0xFFU);
  strncpy(handle->downlink_data, quote_open, data_len);
  handle->downlink_data[data_len] = '\0';
  handle->downlink_is_hex = is_hex;
  __DMB();
  handle->evt_downlink = true;
}

/* ============================================================================
 * Exported functions — Diagnostic
 * ============================================================================ */

uint8_t LORAE5_GetOverflowCount(const LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return 0U;
  }

  return handle->rx_overflow_count;
}

void LORAE5_ClearOverflowCount(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return;
  }

  handle->rx_overflow_count = 0U;
}

const char *LORAE5_StatusToString(LORAE5_Status status) {
  switch (status) {
  case LORAE5_OK:
    return "LORAE5_OK";
  case LORAE5_ERR_NULL_PTR:
    return "LORAE5_ERR_NULL_PTR";
  case LORAE5_ERR_BUSY:
    return "LORAE5_ERR_BUSY";
  case LORAE5_ERR_UART:
    return "LORAE5_ERR_UART";
  case LORAE5_ERR_ARG:
    return "LORAE5_ERR_ARG";
  case LORAE5_ERR_TIMEOUT:
    return "LORAE5_ERR_TIMEOUT";
  case LORAE5_ERR_OVERFLOW:
    return "LORAE5_ERR_OVERFLOW";
  default:
    break;
  }
  (void)status; return "";
}
