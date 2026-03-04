/**
 *******************************************************************************
 * @file    STM32_LORAE5_conf.h
 * @author  manu
 * @brief   Fichier de configuration de la bibliothèque STM32_LORAE5.
 *          Toutes les macros sont surchargeables via flag de compilation :
 *            -DLORAE5_APP_DEV_EUI='"70B3D57ED0ABCDEF"'
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Copyright (c) 2026 manu — Licence MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************
 */
#ifndef STM32_LORAE5_CONF_H
#define STM32_LORAE5_CONF_H

#ifndef LORAE5_DEBUG_ENABLE
#define LORAE5_DEBUG_ENABLE 0  ///< Activer (1) les traces debug série (commentaires AT bruts)
#endif

/* === Dimensions des buffers internes (overridables avant l'include) ======== */
#ifndef LORAE5_RX_LINE_MAX
#define LORAE5_RX_LINE_MAX  256U  ///< Taille max ligne RX
#endif

#ifndef LORAE5_TERM_CMD_MAX
#define LORAE5_TERM_CMD_MAX 128U  ///< Taille max cmd terminal AT
#endif

#ifndef LORAE5_TX_BUF_MAX
#define LORAE5_TX_BUF_MAX   192U  ///< Taille buffer TX handle
#endif

#ifndef LORAE5_LINE_QUEUE_DEPTH
#define LORAE5_LINE_QUEUE_DEPTH 4U  ///< Nombre de lignes RX complètes conservées (anti-perte en rafale)
#endif

#ifndef LORAE5_HAL_HEADER
#define LORAE5_HAL_HEADER "main.h"
#endif

#ifndef LORAE5_APP_REGION
#define LORAE5_APP_REGION "EU868"
#endif

#ifndef LORAE5_APP_UPLINK_PAYLOAD
#define LORAE5_APP_UPLINK_PAYLOAD "HELLO"
#endif

#ifndef LORAE5_APP_UPLINK_PERIOD_MS
#define LORAE5_APP_UPLINK_PERIOD_MS 15000U
#endif

#ifndef LORAE5_APP_STARTUP_DELAY_MS
#define LORAE5_APP_STARTUP_DELAY_MS 200U
#endif

/* Les EUI sont 8 octets = 16 caractères hex, ex : "70B3D57ED0ABCDEF"              */
/* La clé APP est 16 octets = 32 caractères hex, ex : "2B7E151628AED2A6ABF7158809CF4F3C" */
/* Surchargez via flag :  -DLORAE5_APP_DEV_EUI='"70B3D57ED0ABCDEF"'                 */
#ifndef LORAE5_APP_DEV_EUI
#define LORAE5_APP_DEV_EUI ""              ///< DevEUI : 16 car. hex (8 octets) ou vide
#endif

#ifndef LORAE5_APP_APP_EUI
#define LORAE5_APP_APP_EUI ""              ///< JoinEUI/AppEUI : 16 car. hex (8 octets) ou vide
#endif

#ifndef LORAE5_APP_APP_KEY
#define LORAE5_APP_APP_KEY ""              ///< AppKey : 32 car. hex (16 octets) ou vide
#endif

#ifndef LORAE5_TERM_TX_TIMEOUT_MS
#define LORAE5_TERM_TX_TIMEOUT_MS 20U  ///< Timeout HAL_UART_Transmit debug terminal (ms)
#endif

#ifndef LORAE5_TERM_AUTOSEND_IDLE_MS
#define LORAE5_TERM_AUTOSEND_IDLE_MS 1200U  ///< Auto-envoi cmd terminal après inactivité clavier (ms)
#endif

#ifndef LORAE5_JOIN_TIMEOUT_MS
#define LORAE5_JOIN_TIMEOUT_MS    30000U ///< Délai max d'attente réponse AT+JOIN — watchdog FSM (ms)
#endif

#ifndef LORAE5_JOIN_RETRY_DELAY_MS
#define LORAE5_JOIN_RETRY_DELAY_MS 5000U ///< Délai avant retry AT+JOIN après expiration watchdog (ms)
#endif

/* ============================================================================
 * Module broadcast P2P — STM32_LORAE5_broadcast.h (optionnel)
 * Configuration RF par défaut utilisée par LORAE5_BcastRfConfigDefault().
 * Active AT+MODE=TEST + AT+TEST TXLRPKT/RXLRPKT (P2P LoRa sans serveur NS).
 * Surchargeables via flag de compilation ou #define avant l'include.
 * ============================================================================ */

/** @brief Fréquence broadcast en MHz (868=EU868, 915=US915, 923=AS923…). */
#ifndef LORAE5_BCAST_DEFAULT_FREQ_MHZ
#define LORAE5_BCAST_DEFAULT_FREQ_MHZ    868U
#endif

/** @brief Spreading Factor broadcast (7..12). */
#ifndef LORAE5_BCAST_DEFAULT_SF
#define LORAE5_BCAST_DEFAULT_SF          7U
#endif

/** @brief Largeur de bande broadcast en kHz (125, 250 ou 500). */
#ifndef LORAE5_BCAST_DEFAULT_BW_KHZ
#define LORAE5_BCAST_DEFAULT_BW_KHZ      125U
#endif

/** @brief Longueur du préambule TX broadcast (2..65535, recommandé : 8). */
#ifndef LORAE5_BCAST_DEFAULT_TXPR
#define LORAE5_BCAST_DEFAULT_TXPR        8U
#endif

/** @brief Longueur du préambule RX broadcast (2..65535, recommandé : 8). */
#ifndef LORAE5_BCAST_DEFAULT_RXPR
#define LORAE5_BCAST_DEFAULT_RXPR        8U
#endif

/** @brief Puissance TX broadcast en dBm (ex : 14 pour EU868). */
#ifndef LORAE5_BCAST_DEFAULT_PWR_DBM
#define LORAE5_BCAST_DEFAULT_PWR_DBM     14
#endif

/** @brief CRC activé en mode broadcast (1=ON, 0=OFF). */
#ifndef LORAE5_BCAST_DEFAULT_CRC
#define LORAE5_BCAST_DEFAULT_CRC         1
#endif

/** @brief IQ inversé en mode broadcast (1=ON, 0=OFF). IQ=ON requis interop côté serveur. */
#ifndef LORAE5_BCAST_DEFAULT_IQ
#define LORAE5_BCAST_DEFAULT_IQ          0
#endif

/** @brief Sync word réseau public en broadcast (1=ON/0x34, 0=OFF/0x12 réseau privé). */
#ifndef LORAE5_BCAST_DEFAULT_PUBLIC_NET
#define LORAE5_BCAST_DEFAULT_PUBLIC_NET  0
#endif

/** @brief Taille max du payload broadcast hex (chars ASCII hex). Dépend de LORAE5_TX_BUF_MAX.
 *  Overhead commande : "AT+TEST TXLRPKT \"" (18) + "\"\\r\\n" (3) + NUL (1) = 22 chars.
 *  Avec LORAE5_TX_BUF_MAX=192 : 170 chars hex = 85 octets de payload utile. */
#ifndef LORAE5_BCAST_P2P_HEX_MAX
#define LORAE5_BCAST_P2P_HEX_MAX         (LORAE5_TX_BUF_MAX - 22U)
#endif

_Static_assert(((sizeof(LORAE5_APP_DEV_EUI) - 1U) == 0U) || ((sizeof(LORAE5_APP_DEV_EUI) - 1U) == 16U),
			   "LORAE5_APP_DEV_EUI doit etre vide ou 16 caracteres hex");
_Static_assert(((sizeof(LORAE5_APP_APP_EUI) - 1U) == 0U) || ((sizeof(LORAE5_APP_APP_EUI) - 1U) == 16U),
			   "LORAE5_APP_APP_EUI doit etre vide ou 16 caracteres hex");
_Static_assert(((sizeof(LORAE5_APP_APP_KEY) - 1U) == 0U) || ((sizeof(LORAE5_APP_APP_KEY) - 1U) == 32U),
			   "LORAE5_APP_APP_KEY doit etre vide ou 32 caracteres hex");
_Static_assert(LORAE5_LINE_QUEUE_DEPTH >= 2U,
			   "LORAE5_LINE_QUEUE_DEPTH doit etre >= 2");

#endif
