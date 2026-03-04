/**
 *******************************************************************************
 * @file    STM32_LORAE5_conf.h
 * @author  manu
 * @brief   Fichier de configuration de la bibliothèque STM32_LORAE5.
 *          Toutes les macros sont surchargeables via flag de compilation :
 *            -DLORAE5_APP_DEV_EUI='"70B3D57ED0ABCDEF"'
 * @version 1.0.0
 * @date    2026-02-26
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

#define LORAE5_DEBUG_ENABLE 0  ///< Activer (1) les traces debug série (commentaires AT bruts)

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

_Static_assert(((sizeof(LORAE5_APP_DEV_EUI) - 1U) == 0U) || ((sizeof(LORAE5_APP_DEV_EUI) - 1U) == 16U),
			   "LORAE5_APP_DEV_EUI doit etre vide ou 16 caracteres hex");
_Static_assert(((sizeof(LORAE5_APP_APP_EUI) - 1U) == 0U) || ((sizeof(LORAE5_APP_APP_EUI) - 1U) == 16U),
			   "LORAE5_APP_APP_EUI doit etre vide ou 16 caracteres hex");
_Static_assert(((sizeof(LORAE5_APP_APP_KEY) - 1U) == 0U) || ((sizeof(LORAE5_APP_APP_KEY) - 1U) == 32U),
			   "LORAE5_APP_APP_KEY doit etre vide ou 32 caracteres hex");
_Static_assert(LORAE5_LINE_QUEUE_DEPTH >= 2U,
			   "LORAE5_LINE_QUEUE_DEPTH doit etre >= 2");

#endif
