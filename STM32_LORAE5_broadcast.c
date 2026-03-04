/**
 *******************************************************************************
 * @file    STM32_LORAE5_broadcast.c
 * @author  manu
 * @brief   Implémentation du module broadcast P2P pour STM32_LORAE5.
 *          Utilise AT+MODE=TEST + AT+TEST RFCFG/TXLRPKT/RXLRPKT du Wio-E5.
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Libre sous licence MIT.
 *******************************************************************************
 */

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "STM32_LORAE5_broadcast.h"

#include <stdarg.h>   /* va_list, va_start, va_end */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* atoi */

/* ============================================================================
 * Helper privé — formatage dans tx_buf + envoi
 * ============================================================================ */

/**
 * @brief  Formate une commande directement dans handle->tx_buf puis envoie via
 *         LORAE5_SendRaw (DMA-safe : buffer persistant dans le handle).
 * @note   Identique au lorae5_send_fmt() interne, reproduit ici pour éviter
 *         d'exposer une fonction statique du module principal.
 */
static LORAE5_Status lorae5_bcast_sendfmt(LORAE5_Handle_t *handle,
                                          const char *fmt,
                                          ...) {
  int written;
  va_list args;

  if ((handle == NULL) || (fmt == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  va_start(args, fmt);
  written = vsnprintf((char *)handle->tx_buf, sizeof(handle->tx_buf), fmt, args);
  va_end(args);

  if ((written <= 0) || ((size_t)written >= sizeof(handle->tx_buf))) {
    return LORAE5_ERR_ARG;
  }

  return LORAE5_SendRaw(handle, (const char *)handle->tx_buf);
}

/* ============================================================================
 * Contrôle du mode
 * ============================================================================ */

LORAE5_Status LORAE5_BcastEnter(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  return LORAE5_SendAT(handle, "AT+MODE=TEST");
}

LORAE5_Status LORAE5_BcastExit(LORAE5_Handle_t *handle, LORAE5_Mode mode) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  switch (mode) {
  case LORAE5_MODE_LWOTAA:
    return LORAE5_SendAT(handle, "AT+MODE=LWOTAA");
  case LORAE5_MODE_LWABP:
    return LORAE5_SendAT(handle, "AT+MODE=LWABP");
  default:
    return LORAE5_ERR_ARG;
  }
}

/* ============================================================================
 * Configuration RF
 * ============================================================================ */

LORAE5_Status LORAE5_BcastRfConfig(LORAE5_Handle_t *handle,
                                    const LORAE5_BcastRfCfg_t *cfg) {
  if ((handle == NULL) || (cfg == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  if ((cfg->sf < 7U) || (cfg->sf > 12U)) {
    return LORAE5_ERR_ARG;
  }

  if ((cfg->bw_khz != 125U) && (cfg->bw_khz != 250U) && (cfg->bw_khz != 500U)) {
    return LORAE5_ERR_ARG;
  }

  if (cfg->freq_mhz == 0U) {
    return LORAE5_ERR_ARG;
  }

  /* Format : AT+TEST RFCFG=<freq>,SF<sf>,<bw>,<txpr>,<rxpr>,<pwr>,<crc>,<iq>,<net>\r\n
   * Exemple : AT+TEST RFCFG=868,SF7,125,8,8,14,ON,OFF,OFF\r\n                        */
  return lorae5_bcast_sendfmt(handle,
    "AT+TEST RFCFG=%u,SF%u,%u,%u,%u,%d,%s,%s,%s\r\n",
    (unsigned int)cfg->freq_mhz,
    (unsigned int)cfg->sf,
    (unsigned int)cfg->bw_khz,
    (unsigned int)cfg->tx_preamble,
    (unsigned int)cfg->rx_preamble,
    (int)cfg->power_dbm,
    cfg->crc        ? "ON" : "OFF",
    cfg->iq_invert  ? "ON" : "OFF",
    cfg->public_net ? "ON" : "OFF");
}

LORAE5_Status LORAE5_BcastRfConfigDefault(LORAE5_Handle_t *handle) {
  LORAE5_BcastRfCfg_t cfg = LORAE5_BCAST_RF_CFG_DEFAULT;
  return LORAE5_BcastRfConfig(handle, &cfg);
}

/* ============================================================================
 * Émission
 * ============================================================================ */

LORAE5_Status LORAE5_BcastTxCw(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  return LORAE5_SendAT(handle, "AT+TEST TXCW");
}

LORAE5_Status LORAE5_BcastTxP2P(LORAE5_Handle_t *handle, const char *payload_hex) {
  size_t hex_len;

  if ((handle == NULL) || (payload_hex == NULL)) {
    return LORAE5_ERR_NULL_PTR;
  }

  hex_len = strlen(payload_hex);

  if (hex_len == 0U) {
    return LORAE5_ERR_ARG;
  }

  if (hex_len > LORAE5_BCAST_P2P_HEX_MAX) {
    return LORAE5_ERR_ARG;  /* trop long pour tx_buf — augmenter LORAE5_TX_BUF_MAX dans conf.h */
  }

  /* Format : AT+TEST TXLRPKT "<hex>"\r\n */
  return lorae5_bcast_sendfmt(handle, "AT+TEST TXLRPKT \"%s\"\r\n", payload_hex);
}

/* ============================================================================
 * Réception
 * ============================================================================ */

LORAE5_Status LORAE5_BcastRxStart(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  return LORAE5_SendAT(handle, "AT+TEST RXLRPKT");
}

LORAE5_Status LORAE5_BcastGetRssi(LORAE5_Handle_t *handle) {
  if (handle == NULL) {
    return LORAE5_ERR_NULL_PTR;
  }

  return LORAE5_SendAT(handle, "AT+TEST RSSI");
}

/* ============================================================================
 * Parsers réponses AT
 * ============================================================================ */

bool LORAE5_BcastParseRxLine(const char *line, LORAE5_BcastRxPacket_t *out) {
  const char *p;
  const char *q_open;
  const char *q_close;
  size_t data_len;

  if ((line == NULL) || (out == NULL)) {
    return false;
  }

  /* Filtre rapide : la ligne doit contenir "+TEST:" et "RX" */
  if ((strstr(line, "+TEST:") == NULL) || (strstr(line, "RX") == NULL)) {
    return false;
  }

  memset(out, 0, sizeof(*out));

  /* Champ RSSI (présent dans les deux formats firmware) */
  p = strstr(line, "RSSI:");
  if (p != NULL) {
    p += 5U;
    while ((*p == ' ') || (*p == '\t')) { p++; }
    out->rssi = (int16_t)atoi(p);
  }

  /* Champ SNR */
  p = strstr(line, "SNR:");
  if (p != NULL) {
    p += 4U;
    while ((*p == ' ') || (*p == '\t')) { p++; }
    out->snr = (int8_t)atoi(p);
  }

  /* Champ DLEN (firmware v2.x) */
  p = strstr(line, "DLEN:");
  if (p != NULL) {
    p += 5U;
    out->dlen = (uint8_t)atoi(p);
  }

  /* Payload hex entre guillemets après "RX" */
  p = strstr(line, "RX");
  if (p == NULL) {
    return false;
  }

  q_open = strchr(p, '"');
  if (q_open == NULL) {
    return false;
  }
  q_open++;  /* saute le guillemet ouvrant */

  q_close  = strchr(q_open, '"');
  data_len = (q_close != NULL) ? (size_t)(q_close - q_open) : strlen(q_open);

  if (data_len >= sizeof(out->payload_hex)) {
    data_len = sizeof(out->payload_hex) - 1U;
  }

  strncpy(out->payload_hex, q_open, data_len);
  out->payload_hex[data_len] = '\0';

  return (data_len > 0U);
}

bool LORAE5_BcastParseRssiLine(const char *line, int16_t *rssi_out) {
  const char *p;

  if ((line == NULL) || (rssi_out == NULL)) {
    return false;
  }

  /* Attend : "+TEST: RSSI -80" ou "+TEST: RSSI:-80" */
  if (strstr(line, "+TEST:") == NULL) {
    return false;
  }

  p = strstr(line, "RSSI");
  if (p == NULL) {
    return false;
  }

  p += 4U;
  /* Saute ':' et espaces (les deux formats) */
  while ((*p == ':') || (*p == ' ') || (*p == '\t')) { p++; }

  *rssi_out = (int16_t)atoi(p);
  return true;
}
