/**
 *******************************************************************************
 * @file    STM32_LORAE5_broadcast.h
 * @author  manu
 * @brief   Module broadcast P2P pour STM32_LORAE5.
 *          Active AT+MODE=TEST et expose les commandes AT+TEST pour envoyer
 *          et recevoir des trames LoRa point-à-point (sans serveur LoRaWAN) :
 *            - RFCFG    : configure fréquence, SF, BW, puissance, CRC, IQ
 *            - TXCW     : émission onde porteuse continue (test RF / mesure)
 *            - TXLRPKT  : broadcast LoRa P2P (envoi trame brute sans NS)
 *            - RXLRPKT  : réception trame LoRa P2P
 *            - RSSI     : lecture RSSI bruit de fond
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Libre sous licence MIT.
 *
 * @note    Module optionnel — inclure en complément de STM32_LORAE5.h :
 *          @code
 *          #include "STM32_LORAE5.h"           // passerelle LoRaWAN (OTAA/ABP)
 *          #include "STM32_LORAE5_broadcast.h" // broadcast P2P LoRa
 *          @endcode
 *          Les deux modules partagent le même LORAE5_Handle_t et le même
 *          DMA RX circulaire — aucun buffer supplémentaire requis.
 *
 * @note    Modèle non-bloquant identique à la lib principale :
 *          - LORAE5_Bcast*() formate et envoie la commande AT, puis retourne.
 *          - Les réponses AT arrivent via DMA RX → disponibles via LORAE5_PopLine().
 *          - Utiliser LORAE5_BcastParseRxLine() / LORAE5_BcastParseRssiLine()
 *            pour décoder les réponses depuis while(1).
 *
 * @warning AT+MODE=TEST désactive la stack LoRaWAN.
 *          Pour revenir en LoRaWAN, appeler LORAE5_BcastExit() puis
 *          LORAE5_StartManagedOtaa*() de nouveau.
 *
 * @note    Compatibilité firmware Wio-E5 : v2.0.0 et suivants.
 *          Format réponse RX : "+TEST: RXLRPKT;RSSI:<v>;SNR:<v>;DLEN:<n>;RX \"<hex>\""
 *
 * ## Exemple broadcast TX P2P
 * @code
 * LORAE5_BcastRfCfg_t cfg = LORAE5_BCAST_RF_CFG_DEFAULT;
 * cfg.freq_mhz = 868;
 * if (LORAE5_BcastEnter(&lora) == LORAE5_OK) {
 *     // attendre réponse "+MODE: TEST" via LORAE5_PopLine() depuis while(1)...
 *     LORAE5_BcastRfConfig(&lora, &cfg);
 *     LORAE5_BcastTxP2P(&lora, "CAFEBABE0102");
 * }
 * @endcode
 *
 * ## Exemple RX P2P (dans while(1))
 * @code
 * char line[LORAE5_RX_LINE_MAX];
 * while (LORAE5_PopLine(&lora, line, sizeof(line))) {
 *     LORAE5_BcastRxPacket_t pkt;
 *     if (LORAE5_BcastParseRxLine(line, &pkt)) {
 *         // pkt.payload_hex, pkt.rssi, pkt.snr disponibles
 *     }
 * }
 * @endcode
 *******************************************************************************
 */
#ifndef STM32_LORAE5_BROADCAST_H
#define STM32_LORAE5_BROADCAST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "STM32_LORAE5.h"   /* LORAE5_Handle_t, LORAE5_Status, LORAE5_Mode */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Configuration RF pour le mode broadcast P2P (AT+TEST RFCFG).
 *
 * Initialiser via LORAE5_BCAST_RF_CFG_DEFAULT pour utiliser les valeurs de
 * STM32_LORAE5_conf.h, puis surcharger les champs voulus.
 *
 * @note  Format AT généré : AT+TEST RFCFG=<freq>,SF<sf>,<bw>,<txpr>,<rxpr>,<pwr>,<crc>,<iq>,<net>
 *        Exemple           : AT+TEST RFCFG=868,SF7,125,8,8,14,ON,OFF,OFF
 */
typedef struct {
  uint32_t freq_mhz;       ///< Fréquence en MHz (ex : 868 pour EU868, 915 pour US915)
  uint8_t  sf;             ///< Spreading Factor 7..12
  uint16_t bw_khz;         ///< Largeur de bande en kHz : 125, 250 ou 500
  uint16_t tx_preamble;    ///< Longueur préambule TX (2..65535, typique : 8)
  uint16_t rx_preamble;    ///< Longueur préambule RX (2..65535, typique : 8)
  int8_t   power_dbm;      ///< Puissance TX en dBm (ex : 14 pour EU868)
  bool     crc;            ///< true → CRC activé (ON)
  bool     iq_invert;      ///< true → IQ inversé (ON) — requis pour interop serveur LoRaWAN
  bool     public_net;     ///< true → sync word réseau public 0x34, false → privé 0x12
} LORAE5_BcastRfCfg_t;

/**
 * @brief Macro d'initialisation par défaut depuis STM32_LORAE5_conf.h.
 *        Usage : LORAE5_BcastRfCfg_t cfg = LORAE5_BCAST_RF_CFG_DEFAULT;
 */
#define LORAE5_BCAST_RF_CFG_DEFAULT { \
  .freq_mhz    = LORAE5_BCAST_DEFAULT_FREQ_MHZ,    \
  .sf          = LORAE5_BCAST_DEFAULT_SF,           \
  .bw_khz      = LORAE5_BCAST_DEFAULT_BW_KHZ,      \
  .tx_preamble = LORAE5_BCAST_DEFAULT_TXPR,         \
  .rx_preamble = LORAE5_BCAST_DEFAULT_RXPR,         \
  .power_dbm   = LORAE5_BCAST_DEFAULT_PWR_DBM,      \
  .crc         = (LORAE5_BCAST_DEFAULT_CRC != 0),   \
  .iq_invert   = (LORAE5_BCAST_DEFAULT_IQ != 0),    \
  .public_net  = (LORAE5_BCAST_DEFAULT_PUBLIC_NET != 0) \
}

/**
 * @brief Paquet P2P reçu, décodé depuis une ligne "+TEST: RXLRPKT…".
 * @note  Rempli par LORAE5_BcastParseRxLine().
 */
typedef struct {
  char    payload_hex[128]; ///< Payload reçu en hexadécimal ASCII (ex : "CAFEBABE01")
  int16_t rssi;             ///< RSSI en dBm (ex : -80)
  int8_t  snr;              ///< SNR en dB (ex : 8)
  uint8_t dlen;             ///< Longueur payload en octets (champ DLEN de la réponse AT)
} LORAE5_BcastRxPacket_t;

/* ============================================================================
 * Fonctions exportées
 * ============================================================================ */

/* ── Contrôle du mode ───────────────────────────────────────────────────────── */

/**
 * @brief  Entre en mode broadcast P2P (envoie AT+MODE=TEST).
 * @param  handle  Handle actif (non NULL, initialisé)
 * @retval LORAE5_OK        Commande envoyée
 * @retval LORAE5_ERR_NULL_PTR handle NULL
 * @retval LORAE5_ERR_BUSY  TX occupé (réessayer)
 * @retval LORAE5_ERR_UART  Erreur HAL
 * @note   La réponse "+MODE: TEST" arrive de façon asynchrone via LORAE5_PopLine().
 * @warning Désactive la stack LoRaWAN. Appeler LORAE5_BcastExit() pour revenir.
 */
LORAE5_Status LORAE5_BcastEnter(LORAE5_Handle_t *handle);

/**
 * @brief  Quitte le mode broadcast en basculant vers un mode LoRaWAN.
 * @param  handle  Handle actif (non NULL)
 * @param  mode    Mode cible : LORAE5_MODE_LWOTAA ou LORAE5_MODE_LWABP
 * @retval LORAE5_OK       Commande envoyée
 * @retval LORAE5_ERR_ARG  Mode non supporté
 * @retval LORAE5_Status   Voir LORAE5_SendAT()
 * @note   Appeler LORAE5_StartManagedOtaa*() ensuite pour relancer la FSM OTAA.
 */
LORAE5_Status LORAE5_BcastExit(LORAE5_Handle_t *handle, LORAE5_Mode mode);

/* ── Configuration RF ──────────────────────────────────────────────────────── */

/**
 * @brief  Configure les paramètres RF (envoie AT+TEST RFCFG=…).
 * @param  handle  Handle actif (non NULL)
 * @param  cfg     Configuration RF (non NULL)
 * @retval LORAE5_OK            Commande envoyée
 * @retval LORAE5_ERR_NULL_PTR  Paramètre NULL
 * @retval LORAE5_ERR_ARG       SF hors [7..12], BW non supporté, ou fréquence nulle
 * @retval LORAE5_Status        Voir LORAE5_SendRaw()
 * @note   Appeler après LORAE5_BcastEnter() une fois "+MODE: TEST" reçu.
 */
LORAE5_Status LORAE5_BcastRfConfig(LORAE5_Handle_t *handle, const LORAE5_BcastRfCfg_t *cfg);

/**
 * @brief  Configure le RF avec les valeurs par défaut de STM32_LORAE5_conf.h.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_BcastRfConfig()
 */
LORAE5_Status LORAE5_BcastRfConfigDefault(LORAE5_Handle_t *handle);

/* ── Émission ───────────────────────────────────────────────────────────────── */

/**
 * @brief  Envoie AT+TEST TXCW — émission onde porteuse continue.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 * @warning N'utiliser que pour mesures RF / conformité — libère le canal en continu.
 * @note   Pour arrêter : appeler LORAE5_BcastExit() ou LORAE5_BcastRxStart().
 */
LORAE5_Status LORAE5_BcastTxCw(LORAE5_Handle_t *handle);

/**
 * @brief  Envoie une trame LoRa P2P (broadcast sans NS) — AT+TEST TXLRPKT "hex".
 * @param  handle       Handle actif (non NULL)
 * @param  payload_hex  Payload en hexadécimal ASCII (ex : "CAFEBABE0102"), non NULL
 * @retval LORAE5_OK            Commande envoyée
 * @retval LORAE5_ERR_NULL_PTR  Pointeur NULL
 * @retval LORAE5_ERR_ARG       Payload vide ou trop long (> LORAE5_BCAST_P2P_HEX_MAX)
 * @retval LORAE5_Status        Voir LORAE5_SendRaw()
 * @note   Payload max = LORAE5_BCAST_P2P_HEX_MAX chars hex (dépend de LORAE5_TX_BUF_MAX).
 *         Avec la valeur par défaut 192 : 85 octets de payload. Augmenter
 *         LORAE5_TX_BUF_MAX dans conf.h pour des trames plus grandes.
 */
LORAE5_Status LORAE5_BcastTxP2P(LORAE5_Handle_t *handle, const char *payload_hex);

/* ── Réception ─────────────────────────────────────────────────────────────── */

/**
 * @brief  Active la réception broadcast P2P (AT+TEST RXLRPKT).
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 * @note   Les paquets reçus arrivent via LORAE5_PopLine() :
 *           "+TEST: RXLRPKT;RSSI:-80;SNR:8;DLEN:4;RX \"CAFEBABE\""
 *         Utiliser LORAE5_BcastParseRxLine() pour les décoder.
 * @note   La réception reste active jusqu'à la prochaine commande AT.
 */
LORAE5_Status LORAE5_BcastRxStart(LORAE5_Handle_t *handle);

/**
 * @brief  Demande la lecture du RSSI de bruit de fond (AT+TEST RSSI).
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 * @note   Réponse via LORAE5_PopLine() : "+TEST: RSSI -80"
 *         Utiliser LORAE5_BcastParseRssiLine() pour extraire la valeur.
 */
LORAE5_Status LORAE5_BcastGetRssi(LORAE5_Handle_t *handle);

/* ── Parsers réponses AT ────────────────────────────────────────────────────── */

/**
 * @brief  Décode une ligne "+TEST: RXLRPKT…" en LORAE5_BcastRxPacket_t.
 * @param  line  Ligne AT brute (issue de LORAE5_PopLine()), non NULL
 * @param  out   Structure destination (non NULL), remise à zéro avant remplissage
 * @retval true  Ligne décodée avec succès (payload présent)
 * @retval false Ligne ignorée (pas une réponse RXLRPKT ou payload absent)
 * @note   Supporte les deux formats firmware Wio-E5 :
 *           - v2.x : "+TEST: RXLRPKT;RSSI:-80;SNR:8;DLEN:4;RX \"CAFEBABE\""
 *           - v1.x : "+TEST: LEN:4;RSSI:-80;SNR:8;RX \"CAFEBABE\""
 */
bool LORAE5_BcastParseRxLine(const char *line, LORAE5_BcastRxPacket_t *out);

/**
 * @brief  Décode une ligne "+TEST: RSSI -80" et retourne la valeur.
 * @param  line      Ligne AT brute (issue de LORAE5_PopLine()), non NULL
 * @param  rssi_out  Pointeur vers la valeur RSSI destination (non NULL)
 * @retval true  Ligne décodée avec succès
 * @retval false Ligne ignorée (pas une réponse RSSI)
 */
bool LORAE5_BcastParseRssiLine(const char *line, int16_t *rssi_out);

#ifdef __cplusplus
}
#endif

#endif /* STM32_LORAE5_BROADCAST_H */
