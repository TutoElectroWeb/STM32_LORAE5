/**
 *******************************************************************************
 * @file    STM32_LORAE5.h
 * @author  manu
 * @brief   Driver AT-command pour module LoRa-E5 (Wio-E5 / STM32WLE5)
 *          via UART + DMA circulaire RX.
 * @version 0.9.2
 * @date    2026-03-04
 * @copyright Libre sous licence MIT.
 * @note    Délais bloquants : aucun — toutes les opérations sont non-bloquantes.
 *          Les timeouts applicatifs sont gérés dans LORAE5_Task() via HAL_GetTick().
 * @note    Compatibilité RTOS : oui (HAL_GetTick() ou osKernelGetTickCount()),
 *          Init() depuis une tâche init uniquement, jamais depuis IRQ.
 * @note    sizeof(LORAE5_Handle_t) ≈ 700 octets (dépend de LORAE5_RX_LINE_MAX /
 *          LORAE5_TX_BUF_MAX définis dans STM32_LORAE5_conf.h).
 * @warning Modèle thread unique : LORAE5_Task / LORAE5_PopLine depuis main uniquement.
 *          LORAE5_OnRxEvent / LORAE5_OnTxCplt depuis IRQ uniquement.
 *******************************************************************************
 */
#ifndef STM32_LORAE5_H
#define STM32_LORAE5_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "main.h"
#include "STM32_LORAE5_conf.h"
#include LORAE5_HAL_HEADER     ///< HAL multi-famille STM32 via CubeMX (UART_HandleTypeDef, HAL_GetTick…)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Exported constants
 * ============================================================================ */

/** @brief Version majeure de la librairie STM32_LORAE5. */
#define LORAE5_LIB_VERSION_MAJOR  0U  ///< Majeur : incrémenté sur rupture d'API
/** @brief Version mineure de la librairie STM32_LORAE5. */
#define LORAE5_LIB_VERSION_MINOR  9U  ///< Mineur : incrémenté sur fonctionnalité compatible
/** @brief Version patch de la librairie STM32_LORAE5. */
#define LORAE5_LIB_VERSION_PATCH  2U  ///< Patch : corrections rétrocompatibles

/* ============================================================================
 * Exported types
 * ============================================================================ */

/**
 * @brief Codes de retour de la librairie STM32_LORAE5.
 */
typedef enum {
  LORAE5_OK           = 0,  ///< Opération réussie
  LORAE5_ERR_NULL_PTR,          ///< Pointeur NULL non autorisé
  LORAE5_ERR_BUSY,          ///< Module TX occupé (réessayer)
  LORAE5_ERR_UART,          ///< Erreur HAL UART (TX ou RX)
  LORAE5_ERR_ARG,           ///< Argument invalide (longueur, format)
  LORAE5_ERR_TIMEOUT,       ///< Délai dépassé — réservé à l'applicatif (lib FSM async, jamais de boucle bloquante)
  LORAE5_ERR_OVERFLOW       ///< Buffer RX débordement ligne trop longue
} LORAE5_Status;

/**
 * @brief Modes LoRaWAN supportés par la commande AT+MODE.
 */
typedef enum {
  LORAE5_MODE_LWOTAA = 0,  ///< LoRaWAN Over-The-Air Activation (OTAA)
  LORAE5_MODE_LWABP,       ///< LoRaWAN Activation By Personalization (ABP)
  LORAE5_MODE_TEST         ///< Mode test RF (émission/réception directe)
} LORAE5_Mode;

/**
 * @brief Prototype de callback ligne reçue.
 * @note  ⚠️ Appelé depuis le contexte ISR/DMA (LORAE5_OnRxEvent).
 *        NE PAS faire de printf, malloc, ou opérations bloquantes.
 *        Utiliser LORAE5_PopLine() depuis main pour un usage sûr.
 */
typedef void (*LORAE5_LineCallback)(const char *line, void *user_ctx);

/**
 * @brief Handle principal du driver STM32_LORAE5.
 *
 * Encapsule l'état UART, DMA et la machine d'état LoRaWAN OTAA.
 * Toute instance est indépendante (multi-instance supporté).
 *
 * @warning Usage thread unique. LORAE5_Task/PopLine depuis main,
 *          LORAE5_OnRxEvent/OnTxCplt depuis IRQ uniquement.
 * @note    sizeof(LORAE5_Handle_t) ≈ 700 octets (cf. @file).
 */
typedef struct {
  /* ── Bus UART + DMA RX ─────────────────────────────────────────────────── */
  UART_HandleTypeDef *huart;         ///< Handle UART HAL (non NULL après Init)
  uint8_t tx_buf[LORAE5_TX_BUF_MAX]; ///< Buffer TX persistant (DMA-safe, dans le handle)
  uint8_t *rx_dma;                   ///< Buffer DMA RX circulaire (fourni par l'utilisateur)
  uint16_t rx_dma_size;              ///< Taille du buffer DMA RX en octets
  uint16_t rx_last_pos;              ///< Position courante dans le buffer DMA RX

  /* ── Assemblage ligne RX ───────────────────────────────────────────────── */
  char line_buf[LORAE5_RX_LINE_MAX]; ///< Buffer d'assemblage ligne en cours (ISR)
  uint16_t line_len;                 ///< Longueur courante de la ligne en cours
  volatile bool line_overflow;       ///< Ligne trop longue (> LORAE5_RX_LINE_MAX - 1)

  /* ── Synchronisation TX ────────────────────────────────────────────────── */
  volatile bool tx_busy;             ///< true si une transmission DMA/IT est en cours

  /* ── Événements join LoRaWAN ───────────────────────────────────────────── */
  volatile bool joined;              ///< true si le module a rejoint le réseau
  volatile bool evt_join_ok;         ///< Événement join réussi (consommer via ConsumeJoinOkEvent)
  volatile bool evt_join_fail;       ///< Événement join échoué (consommer via ConsumeJoinFailEvent)

  /* ── Événements downlink LoRaWAN ─────────────────────────────────────── */
  volatile bool evt_downlink;              ///< true si un downlink a été reçu (consommer via ConsumeDownlinkEvent)
  /* downlink_port / downlink_data / downlink_is_hex : écrits depuis ISR (lorae5_parse_downlink)
   * mais non-volatile intentionnellement — strncpy/memcpy ne supporte pas volatile char*.
   * La protection est assurée par : __DMB() côté ISR avant evt_downlink, et
   * __disable_irq() côté main (ConsumeDownlinkEvent) qui agit comme barrière compilateur (CPSID+memory). */
  uint8_t downlink_port;                   ///< Port applicatif du dernier downlink (1..223)
  char downlink_data[LORAE5_RX_LINE_MAX];  ///< Payload du downlink (ASCII ou hex string selon +MSG/+MSGHEX)
  bool downlink_is_hex;                    ///< true si le downlink provient d'un +MSGHEX: (payload hex)

  /* ── Publication lignes RX vers main ───────────────────────────────────── */
  volatile bool line_ready;          ///< Compatibilité : true si la file RX contient au moins une ligne
  char line_last[LORAE5_RX_LINE_MAX]; ///< Dernière ligne complète reçue (terminée par '\0')
  volatile uint16_t line_q_head;     ///< Index d'écriture de la file de lignes RX
  volatile uint16_t line_q_tail;     ///< Index de lecture de la file de lignes RX
  char line_queue[LORAE5_LINE_QUEUE_DEPTH][LORAE5_RX_LINE_MAX]; ///< File circulaire de lignes RX complètes

  /* ── Machine d'état OTAA managée ──────────────────────────────────────── */
  bool managed_enabled;              ///< true si LORAE5_StartManagedOtaa* a été appelé
  const char *managed_region;        ///< Région LoRaWAN (ex : "EU868"), pointeur applicatif
  const char *managed_payload;       ///< Payload uplink périodique, pointeur applicatif
  const char *managed_dev_eui;       ///< DevEUI 16 hex chars ou NULL (clés OTAA optionnelles)
  const char *managed_app_eui;       ///< AppEUI 16 hex chars ou NULL
  const char *managed_app_key;       ///< AppKey 32 hex chars ou NULL
  uint32_t managed_uplink_period_ms; ///< Période uplink périodique en ms
  uint32_t managed_next_action_ms;   ///< Timestamp HAL_GetTick() de la prochaine action FSM
  uint32_t managed_last_uplink_ms;   ///< Timestamp HAL_GetTick() du dernier uplink envoyé
  uint32_t managed_join_deadline_ms; ///< Deadline watchdog JOIN (0 = inactif) — mis à jour par LORAE5_Task
  uint8_t managed_step;              ///< Étape courante de la FSM OTAA (0 = démarrage)

  /* ── Callback utilisateur ──────────────────────────────────────────────── */
  LORAE5_LineCallback on_line;       ///< Callback appelé à chaque ligne (depuis ISR — voir @note)
  void *on_line_ctx;                 ///< Contexte utilisateur passé à on_line

  /* ── Diagnostic ─────────────────────────────────────────────────────────── */
  bool initialized;                  ///< true après un appel réussi à LORAE5_Init()
  uint8_t consecutive_errors;        ///< Compteur d'erreurs UART consécutives (remis à 0 sur succès)
  LORAE5_Status last_error;          ///< Dernier code d'erreur rencontré (LORAE5_OK si aucun)
  uint8_t rx_overflow_count;         ///< Nombre de lignes perdues par overflow de queue RX (sature à 255) — lire via LORAE5_GetOverflowCount()
} LORAE5_Handle_t;

/**
 * @brief Contexte terminal AT debug (UART → LoRa-E5 bridge interactif).
 * @note  Usage optionnel — permet d'envoyer des commandes AT via UART debug.
 */
typedef struct {
  UART_HandleTypeDef *uart_debug;       ///< Handle UART debug (console série)
  LORAE5_Handle_t *lora;                  ///< Handle LoRa-E5 associé (non NULL)
  uint8_t rx_char;                      ///< Caractère reçu en réception IT (HAL_UART_Receive_IT)
  char cmd_buf[LORAE5_TERM_CMD_MAX];    ///< Buffer d'assemblage commande AT saisie
  uint16_t cmd_len;                     ///< Longueur courante de la commande en cours
  volatile bool cmd_ready;              ///< true si Enter saisi → commande prête à envoyer
  uint32_t last_input_ms;               ///< Horodatage HAL_GetTick() du dernier caractère saisi
  bool prompt_displayed;                ///< true si le prompt terminal a deja ete affiche et qu'aucune cmd n'est prete
  bool echo;                            ///< Echo local des caractères saisis
  bool show_prompt;                     ///< Afficher le prompt "> " après chaque réponse
} LORAE5_Terminal;

/* ============================================================================
 * Exported functions prototypes
 * ============================================================================ */

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise le handle STM32_LORAE5 et lie le périphérique UART.
 * @param  handle            Handle à initialiser (non NULL, alloué par l'utilisateur)
 * @param  uart              Handle UART HAL (non NULL, configuré par CubeMX)
 * @param  rx_dma_buffer     Buffer DMA circulaire RX (fourni statiquement par l'utilisateur)
 * @param  rx_dma_buffer_size Taille du buffer DMA RX en octets (> 0)
 * @retval LORAE5_OK         Initialisation réussie
 * @retval LORAE5_ERR_NULL_PTR   Paramètre NULL ou taille nulle
 * @note   Ne démarre pas la réception DMA — appeler LORAE5_StartRx() ensuite.
 * @warning Ne pas appeler depuis une IRQ.
 */
LORAE5_Status LORAE5_Init(LORAE5_Handle_t *handle,
                          UART_HandleTypeDef *huart,
                          uint8_t *rx_dma_buffer,
                          uint16_t rx_dma_buffer_size);

/**
 * @brief  Arrête le DMA RX et remet le handle à zéro.
 * @param  handle    Handle actif (non NULL)
 * @retval LORAE5_OK Succès
 * @retval LORAE5_ERR_NULL_PTR handle NULL
 */
LORAE5_Status LORAE5_DeInit(LORAE5_Handle_t *handle);

/* ── DMA RX ──────────────────────────────────────────────────────────────── */

/**
 * @brief  Démarre la réception DMA circulaire RX (HAL_UARTEx_ReceiveToIdle_DMA).
 * @param  handle    Handle initialisé (non NULL)
 * @retval LORAE5_OK Démarrage réussi
 * @retval LORAE5_ERR_NULL_PTR Paramètre NULL ou buffer DMA non configuré
 * @retval LORAE5_ERR_UART Erreur HAL lors du démarrage DMA
 * @pre    LORAE5_Init() doit avoir été appelé avec succès.
 * @post   handle->rx_last_pos == 0, DMA circulaire actif, HT interrupt désactivé.
 */
LORAE5_Status LORAE5_StartRx(LORAE5_Handle_t *handle);

/**
 * @brief  Arrête la réception DMA RX (HAL_UART_DMAStop).
 * @param  handle    Handle actif (non NULL)
 * @retval LORAE5_OK Succès
 * @retval LORAE5_ERR_NULL_PTR handle NULL
 * @retval LORAE5_ERR_UART Erreur HAL lors de l'arrêt DMA
 */
LORAE5_Status LORAE5_StopRx(LORAE5_Handle_t *handle);

/* ── Callback utilisateur ─────────────────────────────────────────────────── */

/**
 * @brief  Enregistre un callback appelé à chaque ligne RX complète.
 * @param  handle  Handle actif (ignoré si NULL)
 * @param  cb      Fonction callback (NULL pour désinscrire)
 * @param  user_ctx Contexte passé en paramètre à chaque appel
 * @note   ⚠️ Le callback est appelé depuis le contexte ISR/DMA.
 *         Ne pas faire de printf, malloc, HAL_Delay dedans.
 *         Préférer LORAE5_PopLine() depuis main pour un usage sûr.
 */
void LORAE5_SetLineCallback(LORAE5_Handle_t *handle, LORAE5_LineCallback cb, void *user_ctx);

/* ── Callbacks HAL IRQ (à appeler depuis stm32xx_it.c) ──────────────────── */

/**
 * @brief  Callback DMA RX — appeler depuis HAL_UARTEx_RxEventCallback.
 * @param  handle       Handle actif (ignoré si NULL)
 * @param  dma_position Position courante du pointeur DMA (paramètre Size HAL)
 * @pre    Appelé exclusivement depuis le contexte IRQ/DMA HAL.
 * @post   Les octets reçus sont parsés, line_ready peut devenir true.
 * @note   Gère correctement le wrap du buffer DMA circulaire.
 */
void LORAE5_OnRxEvent(LORAE5_Handle_t *handle, uint16_t dma_position);

/**
 * @brief  Callback TX complet — appeler depuis HAL_UART_TxCpltCallback.
 * @param  handle  Handle actif (ignoré si NULL)
 * @pre    Appelé exclusivement depuis le contexte IRQ HAL.
 * @post   handle->tx_busy == false.
 */
void LORAE5_OnTxCplt(LORAE5_Handle_t *handle);

/* ── Envoi AT brut ───────────────────────────────────────────────────────── */

/**
 * @brief  Envoie une chaîne brute via UART (DMA TX si disponible, sinon IT TX).
 * @param  handle  Handle actif (non NULL)
 * @param  data    Chaîne à envoyer (non NULL, non vide, longueur ≤ 65535 octets)
 * @retval LORAE5_OK        Envoi déclenché
 * @retval LORAE5_ERR_NULL_PTR  Pointeur NULL
 * @retval LORAE5_ERR_BUSY  TX précédent encore en cours (réessayer)
 * @retval LORAE5_ERR_ARG   Longueur nulle ou > 65535
 * @retval LORAE5_ERR_UART  Erreur HAL lors du déclenchement TX
 * @note   LORAE5_ERR_BUSY est une contention normale (non-bloquant) —
 *         la FSM managée retente automatiquement.
 * @warning Le buffer @p data doit rester valide jusqu'à l'appel de LORAE5_OnTxCplt()
 *          (transfert DMA asynchrone). Ne pas passer un buffer local (stack) ou temporaire.
 *          Préférer LORAE5_SendAT() qui copie dans handle->tx_buf (DMA-safe garanti).
 */
LORAE5_Status LORAE5_SendRaw(LORAE5_Handle_t *handle, const char *data);

/**
 * @brief  Formate une commande AT (ajoute "\\r\\n") et l'envoie via LORAE5_SendRaw.
 * @param  handle  Handle actif (non NULL)
 * @param  cmd     Commande AT sans terminateur (ex : "AT+VER"), non NULL
 * @retval LORAE5_OK       Envoi déclenché
 * @retval LORAE5_ERR_NULL_PTR Pointeur NULL
 * @retval LORAE5_ERR_ARG  Commande trop longue pour le buffer TX (LORAE5_TX_BUF_MAX)
 * @retval LORAE5_ERR_BUSY TX occupé (voir LORAE5_SendRaw)
 * @retval LORAE5_ERR_UART Erreur HAL
 * @note   Utilise handle->tx_buf (buffer persistant — DMA-safe).
 */
LORAE5_Status LORAE5_SendAT(LORAE5_Handle_t *handle, const char *cmd);

/* ── Commandes AT de base ────────────────────────────────────────────────── */

/**
 * @brief  Envoie "AT" (ping) pour vérifier la communication avec le module.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 */
LORAE5_Status LORAE5_Ping(LORAE5_Handle_t *handle);

/**
 * @brief  Envoie "AT+RESET" pour redémarrer le module LoRa-E5.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 * @note   Le module redémarre et émet du texte de boot sur UART (~500 ms).
 */
LORAE5_Status LORAE5_Reset(LORAE5_Handle_t *handle);

/**
 * @brief  Envoie "AT+VER" pour lire la version du firmware du module.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 */
LORAE5_Status LORAE5_GetVersion(LORAE5_Handle_t *handle);

/**
 * @brief  Configure le mode LoRaWAN (OTAA / ABP / TEST) via AT+MODE.
 * @param  handle  Handle actif (non NULL)
 * @param  mode    Mode désiré (LORAE5_Mode)
 * @retval LORAE5_OK       Envoi déclenché
 * @retval LORAE5_ERR_ARG  Mode inconnu
 * @retval LORAE5_Status   Voir LORAE5_SendAT() pour les autres codes
 */
LORAE5_Status LORAE5_SetMode(LORAE5_Handle_t *handle, LORAE5_Mode mode);

/**
 * @brief  Configure la région LoRaWAN via AT+DR (ex : "EU868", "US915").
 * @param  handle  Handle actif (non NULL)
 * @param  region  Chaîne région conforme AT+DR (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 */
LORAE5_Status LORAE5_SetRegion(LORAE5_Handle_t *handle, const char *region);

/**
 * @brief  Lance la procédure Join OTAA via AT+JOIN.
 * @param  handle  Handle actif (non NULL)
 * @retval LORAE5_Status  Voir LORAE5_SendAT()
 * @note   Non-bloquant : la réponse "+JOIN: Network joined" ou "+JOIN: Join failed"
 *         arrive asynchrone. Surveiller via LORAE5_PopLine() ou evt_join_ok/fail.
 */
LORAE5_Status LORAE5_Join(LORAE5_Handle_t *handle);

/**
 * @brief  Envoie un message ASCII non-confirmé ou confirmé (AT+MSG / AT+CMSG).
 * @param  handle         Handle actif (non NULL)
 * @param  payload_ascii  Payload texte (non NULL)
 * @param  confirmed      true → AT+CMSG (ACK réseau requis), false → AT+MSG
 * @retval LORAE5_OK       Envoi déclenché
 * @retval LORAE5_ERR_NULL_PTR payload NULL
 * @retval LORAE5_Status   Voir LORAE5_SendAT()
 */
LORAE5_Status LORAE5_SendMsg(LORAE5_Handle_t *handle, const char *payload_ascii, bool confirmed);

/**
 * @brief  Envoie un payload hexadécimal (AT+MSGHEX / AT+CMSGHEX).
 * @param  handle       Handle actif (non NULL)
 * @param  payload_hex  Chaîne hexadécimale (ex : "DEADBEEF"), non NULL
 * @param  confirmed    true → AT+CMSGHEX, false → AT+MSGHEX
 * @retval LORAE5_OK       Envoi déclenché
 * @retval LORAE5_ERR_NULL_PTR payload NULL
 * @retval LORAE5_Status   Voir LORAE5_SendAT()
 */
LORAE5_Status LORAE5_SendMsgHex(LORAE5_Handle_t *handle, const char *payload_hex, bool confirmed);

/* ── File RX et événements LoRaWAN ───────────────────────────────────────── */
/* @note  La FSM OTAA managée (StartManagedOtaa*, Task) est dans STM32_LORAE5_gateway.h */

/**
 * @brief  Dépile une ligne RX depuis le buffer partagé (section critique interne).
 * @param  handle    Handle actif (non NULL)
 * @param  out       Buffer destination (non NULL, taille out_size)
 * @param  out_size  Taille du buffer destination (> 0)
 * @retval true  Une ligne a été copiée dans out
 * @retval false Aucune ligne disponible ou paramètre invalide
 * @note   Appeler en boucle jusqu'à retour false pour vider toutes les lignes en attente.
 */
bool LORAE5_PopLine(LORAE5_Handle_t *handle, char *out, size_t out_size);

/**
 * @brief  Consomme l'événement "join réussi" (atomique).
 * @param  handle  Handle actif (non NULL)
 * @retval true   Événement présent (evt_join_ok remis à false)
 * @retval false  Pas d'événement ou handle NULL
 */
bool LORAE5_ConsumeJoinOkEvent(LORAE5_Handle_t *handle);

/**
 * @brief  Consomme l'événement "join échoué" (atomique).
 * @param  handle  Handle actif (non NULL)
 * @retval true   Événement présent (evt_join_fail remis à false)
 * @retval false  Pas d'événement ou handle NULL
 */
bool LORAE5_ConsumeJoinFailEvent(LORAE5_Handle_t *handle);

/**
 * @brief  Consomme l'événement "downlink reçu" (atomique).
 * @details Détecté automatiquement dans les réponses AT :
 *          - @c +MSG: PORT: X; RX: "payload"   (AT+MSG / AT+CMSG)
 *          - @c +MSGHEX: PORT: X; RX: "DEADBEEF" (AT+MSGHEX / AT+CMSGHEX)
 * @param  handle        Handle actif (non NULL)
 * @param  port_out      Port applicatif du downlink [1..223] — peut être NULL
 * @param  data_out      Buffer destination pour le payload (peut être NULL)
 * @param  data_out_size Taille du buffer @p data_out
 * @param  is_hex_out    true si le payload est une chaîne hex (+MSGHEX:) — peut être NULL
 * @retval true   Événement présent (evt_downlink remis à false)
 * @retval false  Pas d'événement ou handle NULL
 * @note   Usage type :
 *         @code
 *         uint8_t port; char data[64]; bool hex;
 *         if (LORAE5_ConsumeDownlinkEvent(&lora, &port, data, sizeof(data), &hex)) {
 *             printf("DL port=%u  %s\r\n", port, data);
 *         }
 *         @endcode
 */
bool LORAE5_ConsumeDownlinkEvent(LORAE5_Handle_t *handle,
                                 uint8_t *port_out,
                                 char *data_out, size_t data_out_size,
                                 bool *is_hex_out);

/**
 * @brief  Retourne l'état de connexion LoRaWAN.
 * @param  handle  Handle (NULL → retourne false)
 * @retval true   Module joint au réseau LoRaWAN
 * @retval false  Non joint ou handle NULL
 */
bool LORAE5_IsJoined(const LORAE5_Handle_t *handle);

/* ── Terminal AT debug ────────────────────────────────────────────────────── */

/**
 * @brief  Initialise le terminal AT debug (bridge UART debug ↔ LoRa-E5).
 * @param  term        Contexte terminal (non NULL, alloué statiquement)
 * @param  uart_debug  Handle UART console debug (non NULL)
 * @param  lora        Handle LoRa-E5 actif (non NULL)
 * @param  echo        true → écho local des caractères saisis
 * @param  show_prompt true → affiche "> " après chaque réponse
 * @retval LORAE5_OK       Initialisation réussie
 * @retval LORAE5_ERR_NULL_PTR Paramètre NULL
 * @retval LORAE5_ERR_UART Erreur HAL démarrage IT
 * @note   Lance HAL_UART_Receive_IT sur uart_debug (1 octet par IT).
 */
LORAE5_Status LORAE5_TerminalBegin(LORAE5_Terminal *term,
                                   UART_HandleTypeDef *uart_debug,
                                   LORAE5_Handle_t *lora,
                                   bool echo,
                                   bool show_prompt);

/**
 * @brief  Callback UART IT debug — appeler depuis HAL_UART_RxCpltCallback.
 * @param  term  Contexte terminal (ignoré si NULL)
 * @param  huart Handle UART ayant déclenché l'IT (comparé avec term->uart_debug)
 * @pre    Appelé exclusivement depuis le contexte IRQ HAL.
 * @post   term->cmd_ready == true si Enter saisi et cmd_len > 0.
 */
void LORAE5_TerminalRxCallback(LORAE5_Terminal *term, UART_HandleTypeDef *huart);

/**
 * @brief  Traite les commandes AT et affiche les réponses — appeler depuis while(1).
 * @param  term  Contexte terminal (ignoré si NULL ou incomplètement initialisé)
 * @note   Envoie la commande AT si cmd_ready, puis affiche toutes les lignes
 *         disponibles via LORAE5_PopLine().
 */
void LORAE5_TerminalTask(LORAE5_Terminal *term);

/* ── Diagnostic ──────────────────────────────────────────────────────────── */

/**
 * @brief  Convertit un code LORAE5_Status en chaîne lisible.
 * @param  status  Code à convertir
 * @retval Pointeur vers une chaîne littérale statique (ne pas libérer)
 * @note   Toujours disponible — pas de compilation conditionnelle.
 */
const char *LORAE5_StatusToString(LORAE5_Status status);

/**
 * @brief  Retourne la version de la librairie sous forme de chaîne (ex : "1.0.0").
 * @retval Pointeur vers une chaîne littérale statique (ne pas libérer)
 * @note   Complément runtime des macros LORAE5_LIB_VERSION_MAJOR/MINOR/PATCH.
 *         Utile pour logger la version au démarrage :
 *         printf("LORAE5 v%s\r\n", LORAE5_GetVersionString());
 */
const char *LORAE5_GetVersionString(void);

/**
 * @brief  Retourne le nombre de lignes RX perdues par overflow de queue.
 * @param  handle  Handle actif (NULL → retourne 0)
 * @retval Nombre d'overflows cumulés depuis Init ou dernier ClearOverflowCount (sature à 255)
 * @note   Un overflow signifie que la queue RX était pleine au moment de la réception.
 *         Augmenter LORAE5_LINE_QUEUE_DEPTH ou appeler LORAE5_Task() plus fréquemment.
 */
uint8_t LORAE5_GetOverflowCount(const LORAE5_Handle_t *handle);

/**
 * @brief  Remet à zéro le compteur d'overflows RX.
 * @param  handle  Handle actif (ignoré si NULL)
 */
void LORAE5_ClearOverflowCount(LORAE5_Handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* STM32_LORAE5_H */
