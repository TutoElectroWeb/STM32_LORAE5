/**
 *******************************************************************************
 * @file    STM32_LORAE5_gateway.c
 * @author  manu
 * @brief   Implémentation du module passerelle LoRaWAN (FSM OTAA managée).
 *          Voir STM32_LORAE5_gateway.h pour la documentation de l'API.
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Libre sous licence MIT.
 *******************************************************************************
 */

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "main.h"
#include "STM32_LORAE5_gateway.h"

#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Private helpers
 * ============================================================================ */

/** @brief Retourne true si la chaîne est non NULL et non vide. */
static bool gw_is_non_empty(const char *s) {
  return (s != NULL) && (s[0] != '\0');
}

/** @brief Retourne true si le caractère est un chiffre hexadécimal ASCII. */
static bool gw_is_hex_char(char c) {
  return ((c >= '0') && (c <= '9')) ||
         ((c >= 'a') && (c <= 'f')) ||
         ((c >= 'A') && (c <= 'F'));
}

/**
 * @brief  Retourne true si la chaîne est une suite hex de longueur exacte.
 * @param  s             Chaîne à vérifier (NULL → false)
 * @param  expected_len  Longueur attendue en caractères
 */
static bool gw_is_hex_string_exact(const char *s, size_t expected_len) {
  size_t len;

  if (s == NULL) {
    return false;
  }

  len = strlen(s);
  if (len != expected_len) {
    return false;
  }

  for (size_t i = 0U; i < len; i++) {
    if (!gw_is_hex_char(s[i])) {
      return false;
    }
  }

  return true;
}

/**
 * @brief  Formate une commande AT (fmt_rn doit inclure \\r\\n) dans tx_buf
 *         et l'envoie via LORAE5_SendRaw.
 * @param  handle   Handle actif (non NULL)
 * @param  fmt_rn   Chaîne de format incluant \\r\\n (ex: "AT+ID=DevEui,\"%s\"\\r\\n")
 * @param  arg      Unique argument chaîne à substituer dans fmt_rn
 * @retval LORAE5_ERR_ARG si le résultat dépasse tx_buf ; sinon code de LORAE5_SendRaw.
 * @note   Écrit dans handle->tx_buf (DMA-safe, persistant jusqu'au TxCplt).
 *         Appeler uniquement depuis le contexte main (non-IRQ).
 */
static LORAE5_Status gw_send_fmt(LORAE5_Handle_t *handle,
                                 const char *fmt_rn,
                                 const char *arg) {
  int written;

  written = snprintf((char *)handle->tx_buf, sizeof(handle->tx_buf), fmt_rn, arg);
  if ((written <= 0) || ((size_t)written >= sizeof(handle->tx_buf))) {
    return LORAE5_ERR_ARG;
  }

  return LORAE5_SendRaw(handle, (const char *)handle->tx_buf);
}

/* ============================================================================
 * Exported functions
 * ============================================================================ */

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

  if (gw_is_non_empty(dev_eui) && !gw_is_hex_string_exact(dev_eui, 16U)) {
    return LORAE5_ERR_ARG;
  }

  if (gw_is_non_empty(app_eui) && !gw_is_hex_string_exact(app_eui, 16U)) {
    return LORAE5_ERR_ARG;
  }

  if (gw_is_non_empty(app_key) && !gw_is_hex_string_exact(app_key, 32U)) {
    return LORAE5_ERR_ARG;
  }

  handle->managed_enabled           = true;
  handle->managed_region            = region;
  handle->managed_payload           = periodic_payload;
  handle->managed_dev_eui           = dev_eui;
  handle->managed_app_eui           = app_eui;
  handle->managed_app_key           = app_key;
  handle->managed_uplink_period_ms  = uplink_period_ms;
  /* L'addition peut déborder uint32_t si HAL_GetTick() est proche de UINT32_MAX.
   * C'est intentionnel : la comparaison FSM utilise (int32_t)(now_ms - target) >= 0,
   * qui gère correctement le wraparound en arithmétique modulaire 2^32. */
  handle->managed_next_action_ms    = HAL_GetTick() + startup_delay_ms;
  handle->managed_last_uplink_ms    = 0U;
  handle->managed_step              = 0U;
  handle->joined                    = false;
  handle->evt_join_ok               = false;
  handle->evt_join_fail             = false;

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
        handle->managed_step           = 1U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 1:
      if (LORAE5_GetVersion(handle) == LORAE5_OK) {
        handle->managed_step           = 2U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 2:
      if (LORAE5_SetMode(handle, LORAE5_MODE_LWOTAA) == LORAE5_OK) {
        handle->managed_step           = 3U;
        handle->managed_next_action_ms = now_ms + 300U;
      }
      break;

    case 3:
      if (LORAE5_SetRegion(handle, handle->managed_region) == LORAE5_OK) {
        if (gw_is_non_empty(handle->managed_dev_eui)) {
          handle->managed_step = 4U;
        } else if (gw_is_non_empty(handle->managed_app_eui)) {
          handle->managed_step = 5U;
        } else if (gw_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 4:
      if (gw_send_fmt(handle, "AT+ID=DevEui,\"%s\"\r\n",
                      handle->managed_dev_eui) == LORAE5_OK) {
        if (gw_is_non_empty(handle->managed_app_eui)) {
          handle->managed_step = 5U;
        } else if (gw_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 5:
      if (gw_send_fmt(handle, "AT+ID=AppEui,\"%s\"\r\n",
                      handle->managed_app_eui) == LORAE5_OK) {
        if (gw_is_non_empty(handle->managed_app_key)) {
          handle->managed_step = 6U;
        } else {
          handle->managed_step = 7U;
        }
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 6:
      if (gw_send_fmt(handle, "AT+KEY=APPKEY,\"%s\"\r\n",
                      handle->managed_app_key) == LORAE5_OK) {
        handle->managed_step           = 7U;
        handle->managed_next_action_ms = now_ms + 500U;
      }
      break;

    case 7:
      if (LORAE5_Join(handle) == LORAE5_OK) {
        handle->managed_step             = 8U;
        /* wraparound intentionnel — comparaison (int32_t) ci-dessous */
        handle->managed_next_action_ms   = now_ms + 10000U;
        handle->managed_join_deadline_ms = now_ms + LORAE5_JOIN_TIMEOUT_MS;
      }
      break;

    default:
      break;
    }
  }

  if (handle->joined) {
    /* Soustraction uint32_t modulo 2^32 : correcte pour elapsed >= period.
     * Différent des comparaisons de deadline (cast int32_t) car period_ms <= 2^31. */
    if ((now_ms - handle->managed_last_uplink_ms) >= handle->managed_uplink_period_ms) {
      if (LORAE5_SendMsg(handle, handle->managed_payload, false) == LORAE5_OK) {
        handle->managed_last_uplink_ms = now_ms;
      }
    }
  }
}
