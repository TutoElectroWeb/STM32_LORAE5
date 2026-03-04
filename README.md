# STM32_LORAE5 — Driver AT LoRa-E5 via UART + DMA

Version : **v1.0.0** — Licence : MIT

---

## Sommaire

1. [Objectif et périmètre](#1-objectif-et-périmètre)
2. [Fichiers et rôle de chacun](#2-fichiers-et-rôle-de-chacun)
3. [Pré-requis CubeMX et HAL](#3-pré-requis-cubemx-et-hal)
4. [Comment ça marche — architecture interne](#4-comment-ça-marche--architecture-interne)
5. [Démarrage pas à pas](#5-démarrage-pas-à-pas)
6. [API publique détaillée](#6-api-publique-détaillée)
7. [Configuration compile-time](#7-configuration-compile-time)
8. [Codes de retour](#8-codes-de-retour)
9. [Machine d'état OTAA managée](#9-machine-détat-otaa-managée)
10. [Événements applicatifs](#10-événements-applicatifs)
11. [Terminal AT debug](#11-terminal-at-debug)
12. [Diagnostic et détection d'overflow](#12-diagnostic-et-détection-doverflow)
13. [Limites connues](#13-limites-connues)
14. [Conformité et garanties runtime](#14-conformité-et-garanties-runtime)
15. [Vérification automatisée](#15-vérification-automatisée)
16. [Exemples disponibles](#16-exemples-disponibles)
17. [Dépannage rapide](#17-dépannage-rapide)

---

## 1. Objectif et périmètre

La librairie `STM32_LORAE5` pilote un module **LoRa-E5** (Seeed Wio-E5, basé STM32WLE5)
en commandes AT via UART, depuis un microcontrôleur STM32 hôte.

Elle résout les problèmes classiques d'intégration :

- La **réception UART est entièrement non-bloquante** grâce au DMA circulaire + IDLE. Il n'y a aucune boucle d'attente, pas de `HAL_Delay`, pas de polling actif.
- L'**émission** utilise DMA TX si disponible, sinon IT TX — toujours asynchrone.
- Le **parsing des lignes AT** est fait dans l'ISR (caractère par caractère) et les lignes complètes sont publiées vers le main via une file circulaire protégée.
- Une **machine d'état OTAA** entièrement managée gère la séquence complète (ping → version → mode → région → clés → join → uplink périodique → retry) sans que l'application ait à s'en préoccuper.
- Des **événements atomiques** (join ok, join fail, downlink) permettent à l'application de réagir proprement sans accès direct aux champs internes.

Ce que la librairie ne fait **pas** :

- Pas de FSM ABP dédiée (le mode ABP peut être configuré manuellement via `LORAE5_SetMode`).
- Pas de gestion RTOS intégrée (compatible RTOS si les règles d'usage sont respectées, voir §4).
- Pas d'allocation dynamique — zéro `malloc/free`.

Si tu forks le projet et que tu débutes, suis cet ordre simple :

1. Configure CubeMX/HAL (section 3).
2. Copie le **Cas 1** de la section 5 dans ton `main.c`.
3. Vérifie que `LORAE5_Task` tourne dans ta boucle principale.
4. Lance `bash dev/Prompt_Audit_Suivi/Audit_LIBS/check_host_at.sh --lib LORAE5` (section 15) pour valider la base.

---

## 2. Fichiers et rôle de chacun

| Fichier                     | Rôle                                                                                     |
| --------------------------- | ---------------------------------------------------------------------------------------- |
| `STM32_LORAE5.h`            | Seul fichier à inclure dans l'application. Contient types, handle, toute l'API publique. |
| `STM32_LORAE5.c`            | Implémentation : réception DMA, parsing AT, FSM OTAA, terminal, diagnostic.              |
| `STM32_LORAE5_conf.h`       | Paramètres compile-time surchargeables (tailles de buffers, clés OTAA, période uplink).  |
| `exemples/`                 | Exemples `main.c` prêts à intégrer dans un projet STM32CubeIDE.                          |
| `dev/tests/host_at/lorae5/` | Suite de tests unitaires host (Linux, sans matériel).                                    |

---

## 3. Pré-requis CubeMX et HAL

### UART LoRa-E5

- Mode asynchrone, **8N1**, sans flow control hardware.
- Débit par défaut du module LoRa-E5 AT : **9600 bauds**.
- Activer les interruptions UART dans NVIC.

### DMA RX — obligatoire

- Configurer **DMA RX** en mode **circulaire** pour l'UART LoRa-E5.
- API HAL utilisée : `HAL_UARTEx_ReceiveToIdle_DMA`.
- La librairie désactive automatiquement l'interruption half-transfer DMA RX (inutile, source de faux réveils).
- Le buffer DMA est fourni par l'application (tableau statique `uint8_t`).

### DMA TX — optionnel

- Si `huart->hdmatx != NULL` au moment de l'envoi, la librairie utilise `HAL_UART_Transmit_DMA`.
- Sinon, elle bascule sur `HAL_UART_Transmit_IT`.
- Dans les deux cas l'envoi est non-bloquant.

### Callbacks HAL à rediriger — obligatoire

Dans `stm32xx_it.c` ou dans `main.c` (selon la génération CubeMX) :

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USARTx)   /* remplacer USARTx par l'instance LoRa */
        LORAE5_OnRxEvent(&hlorae5, Size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USARTx)
        LORAE5_OnTxCplt(&hlorae5);
}
```

Sans `LORAE5_OnRxEvent`, aucune ligne AT ne sera parsée.
Sans `LORAE5_OnTxCplt`, le flag `tx_busy` ne sera jamais remis à zéro et toutes les émissions suivantes échoueront avec `LORAE5_ERR_BUSY`.

### UART debug (optionnel mais recommandé)

Un second UART console (vers PC via USB-série) permet d'utiliser `printf` pour les traces et d'activer le terminal AT interactif (§11).

---

## 4. Comment ça marche — architecture interne

Comprendre l'architecture évite les erreurs d'intégration.

### Flux de réception (RX)

```
Module LoRa-E5 (UART TX)
        │
        ▼
  Buffer DMA circulaire  ←── HAL_UARTEx_ReceiveToIdle_DMA (lancé par LORAE5_StartRx)
        │
        │ (ISR DMA IDLE → HAL_UARTEx_RxEventCallback)
        ▼
  LORAE5_OnRxEvent(handle, Size)
        │
        │ parse octet par octet dans line_buf
        │ '\r' ignoré, '\n' → ligne complète
        │ détecte "+JOIN: Network joined", "+JOIN: Join failed", "+MSG: PORT:…"
        │
        ▼
  lorae5_emit_line()
        │ publie la ligne dans line_queue[head]
        │ __DMB() puis head++   ← barrière mémoire, visible du main
        │ pose evt_join_ok / evt_join_fail / evt_downlink si détecté
        │ appelle on_line callback (si enregistré)
        ▼
  line_queue[0..LORAE5_LINE_QUEUE_DEPTH-1]   ← file circulaire, taille fixe
```

### Flux de consommation (main / LORAE5_Task)

```c
// Dans while(1) :
LORAE5_Task(&hlorae5, HAL_GetTick());     // avance la FSM OTAA

char line[128];
while (LORAE5_PopLine(&hlorae5, line, sizeof(line))) {
    // traitement ligne à ligne (printf, log, etc.)
}

if (LORAE5_ConsumeJoinOkEvent(&hlorae5))   { /* join réussi */ }
if (LORAE5_ConsumeJoinFailEvent(&hlorae5)) { /* join échoué */ }

uint8_t port; char data[64]; bool hex;
if (LORAE5_ConsumeDownlinkEvent(&hlorae5, &port, data, sizeof(data), &hex)) {
    /* downlink reçu */
}
```

### Séparation ISR / main — règle fondamentale

| Contexte ISR (IRQ)        | Contexte main (boucle principale) |
| ------------------------- | --------------------------------- |
| `LORAE5_OnRxEvent`        | `LORAE5_Task`                     |
| `LORAE5_OnTxCplt`         | `LORAE5_PopLine`                  |
| callback `on_line`        | `LORAE5_Consume*Event`            |
| _(écriture queue, flags)_ | _(lecture queue, flags)_          |

Ne jamais appeler `LORAE5_Task`, `LORAE5_PopLine` ou `Consume*` depuis une IRQ.
Ne jamais appeler `LORAE5_OnRxEvent` ou `LORAE5_OnTxCplt` depuis le main.

### Taille mémoire du handle

`sizeof(LORAE5_Handle_t)` ≈ **700 octets** avec les valeurs par défaut de `conf.h`.
Déclarer le handle en **statique** (global ou `static`) pour éviter de l'empiler sur la stack.

```c
static LORAE5_Handle_t hlorae5;   // ← correct
static uint8_t lora_rx_dma[256];  // ← buffer DMA, doit aussi être statique
```

---

## 5. Démarrage pas à pas

Checklist avant de compiler (évite 80% des erreurs de démarrage) :

- `LORAE5_Handle_t` et buffer DMA déclarés en `static`.
- `LORAE5_Init(...)` puis `LORAE5_StartRx(...)` appelés après l'init UART.
- Callbacks HAL reliés à `LORAE5_OnRxEvent` et `LORAE5_OnTxCplt`.
- Dans `while(1)`, `LORAE5_Task(...)` appelé à chaque tour.

### Cas 1 — OTAA managé (le plus courant)

```c
/* --- Variables globales --- */
static LORAE5_Handle_t hlorae5;
static uint8_t lora_rx_dma[256];

/* --- Dans main(), après MX_USART1_UART_Init() --- */
LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma));
LORAE5_StartRx(&hlorae5);

/* Clés OTAA dans STM32_LORAE5_conf.h, région "EU868", payload "HELLO",
   uplink toutes les 15 s, délai démarrage 200 ms */
LORAE5_StartManagedOtaa(&hlorae5, "EU868", "HELLO", 15000, 200);

/* --- Boucle principale --- */
while (1) {
    uint32_t now = HAL_GetTick();
    char line[128];

    LORAE5_Task(&hlorae5, now);

    while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
        printf("[LORA] %s\r\n", line);

    if (LORAE5_ConsumeJoinOkEvent(&hlorae5))
        printf("Join OK !\r\n");

    if (LORAE5_ConsumeJoinFailEvent(&hlorae5))
        printf("Join echoue, retry...\r\n");

    uint8_t port; char data[64]; bool hex;
    if (LORAE5_ConsumeDownlinkEvent(&hlorae5, &port, data, sizeof(data), &hex))
        printf("Downlink port=%u : %s%s\r\n", port, data, hex ? " (hex)" : "");
}
```

### Cas 2 — Clés OTAA explicites (sans passer par conf.h)

```c
LORAE5_StartManagedOtaaWithKeys(&hlorae5,
    "EU868",              // région
    "PING",               // payload périodique
    30000,                // période uplink 30 s
    500,                  // délai démarrage 500 ms
    "70B3D57ED0ABCDEF",   // DevEUI  (16 hex chars)
    "0000000000000000",   // AppEUI  (16 hex chars)
    "2B7E151628AED2A6ABF7158809CF4F3C"); // AppKey (32 hex chars)
```

### Cas 3 — Commandes AT manuelles (sans FSM managée)

```c
LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma));
LORAE5_StartRx(&hlorae5);

LORAE5_Ping(&hlorae5);       // envoie "AT\r\n"
LORAE5_GetVersion(&hlorae5); // envoie "AT+VER\r\n"
LORAE5_SetMode(&hlorae5, LORAE5_MODE_LWOTAA);
LORAE5_SetRegion(&hlorae5, "EU868");
LORAE5_Join(&hlorae5);       // envoie "AT+JOIN\r\n"

while (1) {
    char line[128];
    while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
        printf("%s\r\n", line);

    if (LORAE5_ConsumeJoinOkEvent(&hlorae5)) {
        LORAE5_SendMsg(&hlorae5, "HELLO", false); // AT+MSG="HELLO"
    }
}
```

---

## 6. API publique détaillée

### 6.1 Lifecycle

#### `LORAE5_Init`

```c
LORAE5_Status LORAE5_Init(LORAE5_Handle_t *handle,
                           UART_HandleTypeDef *huart,
                           uint8_t *rx_dma_buffer,
                           uint16_t rx_dma_buffer_size);
```

Initialise le handle et lie l'UART. Ne démarre pas le DMA — appeler `LORAE5_StartRx` ensuite.
Le buffer DMA doit rester valide toute la durée de vie du handle (déclarer statique).

#### `LORAE5_DeInit`

```c
LORAE5_Status LORAE5_DeInit(LORAE5_Handle_t *handle);
```

Arrête le DMA RX et remet le handle à zéro. Utile avant un reset applicatif.

---

### 6.2 Réception DMA

#### `LORAE5_StartRx`

```c
LORAE5_Status LORAE5_StartRx(LORAE5_Handle_t *handle);
```

Lance `HAL_UARTEx_ReceiveToIdle_DMA` sur le buffer fourni à `LORAE5_Init`.
Remet `rx_last_pos` à 0. Désactive l'interruption half-transfer DMA (inutile en mode circulaire).

#### `LORAE5_StopRx`

```c
LORAE5_Status LORAE5_StopRx(LORAE5_Handle_t *handle);
```

Arrête la réception DMA (`HAL_UART_DMAStop`).

---

### 6.3 Callbacks HAL (depuis IRQ)

#### `LORAE5_OnRxEvent`

```c
void LORAE5_OnRxEvent(LORAE5_Handle_t *handle, uint16_t dma_position);
```

Appelé depuis `HAL_UARTEx_RxEventCallback`. Parse les octets reçus depuis la dernière position connue, gère le wrap du buffer circulaire, parse les lignes, publie les événements join/downlink.

#### `LORAE5_OnTxCplt`

```c
void LORAE5_OnTxCplt(LORAE5_Handle_t *handle);
```

Appelé depuis `HAL_UART_TxCpltCallback`. Remet `tx_busy` à false, permettant le prochain envoi.

---

### 6.4 Callback ligne (optionnel, contexte ISR)

#### `LORAE5_SetLineCallback`

```c
void LORAE5_SetLineCallback(LORAE5_Handle_t *handle,
                             LORAE5_LineCallback cb,
                             void *user_ctx);
```

Enregistre une fonction appelée à chaque ligne complète, **depuis l'ISR**.
Le prototype du callback est `void my_cb(const char *line, void *ctx)`.

> **Attention** : ce callback s'exécute dans le contexte IRQ. Ne pas y faire de `printf`, `malloc` ou toute opération bloquante. Pour un traitement sûr depuis le main, utiliser `LORAE5_PopLine` à la place.

---

### 6.5 Envoi AT

#### `LORAE5_SendRaw`

```c
LORAE5_Status LORAE5_SendRaw(LORAE5_Handle_t *handle, const char *data);
```

Envoie une chaîne brute telle quelle (le `\r\n` doit être inclus si nécessaire).
Le pointeur `data` doit rester valide jusqu'à `LORAE5_OnTxCplt` (transfert DMA asynchrone).

> Préférer `LORAE5_SendAT` qui copie dans un buffer interne DMA-safe.

#### `LORAE5_SendAT`

```c
LORAE5_Status LORAE5_SendAT(LORAE5_Handle_t *handle, const char *cmd);
```

Ajoute `\r\n` à la commande, copie dans `handle->tx_buf` (buffer persistant, DMA-safe), puis appelle `LORAE5_SendRaw`. C'est la fonction d'envoi recommandée pour les commandes AT.

---

### 6.6 Commandes AT de base

| Fonction                                       | Commande AT envoyée              |
| ---------------------------------------------- | -------------------------------- |
| `LORAE5_Ping(handle)`                          | `AT\r\n`                         |
| `LORAE5_Reset(handle)`                         | `AT+RESET\r\n`                   |
| `LORAE5_GetVersion(handle)`                    | `AT+VER\r\n`                     |
| `LORAE5_SetMode(handle, LORAE5_MODE_LWOTAA)`   | `AT+MODE=LWOTAA\r\n`             |
| `LORAE5_SetMode(handle, LORAE5_MODE_LWABP)`    | `AT+MODE=LWABP\r\n`              |
| `LORAE5_SetMode(handle, LORAE5_MODE_TEST)`     | `AT+MODE=TEST\r\n`               |
| `LORAE5_SetRegion(handle, "EU868")`            | `AT+DR=EU868\r\n`                |
| `LORAE5_Join(handle)`                          | `AT+JOIN\r\n`                    |
| `LORAE5_SendMsg(handle, "HELLO", false)`       | `AT+MSG="HELLO"\r\n`             |
| `LORAE5_SendMsg(handle, "HELLO", true)`        | `AT+CMSG="HELLO"\r\n` (confirmé) |
| `LORAE5_SendMsgHex(handle, "DEADBEEF", false)` | `AT+MSGHEX="DEADBEEF"\r\n`       |
| `LORAE5_SendMsgHex(handle, "DEADBEEF", true)`  | `AT+CMSGHEX="DEADBEEF"\r\n`      |

Toutes ces fonctions sont **non-bloquantes**. Elles déclenchent l'envoi et rendent la main immédiatement. La réponse du module arrive ensuite via `LORAE5_OnRxEvent` et est accessible via `LORAE5_PopLine`.

---

### 6.7 Machine d'état OTAA managée

Voir aussi §9 pour le détail des étapes.

#### `LORAE5_StartManagedOtaa`

```c
LORAE5_Status LORAE5_StartManagedOtaa(LORAE5_Handle_t *handle,
                                       const char *region,
                                       const char *periodic_payload,
                                       uint32_t uplink_period_ms,
                                       uint32_t startup_delay_ms);
```

Démarre la FSM OTAA en utilisant les clés définies dans `STM32_LORAE5_conf.h`
(`LORAE5_APP_DEV_EUI`, `LORAE5_APP_APP_EUI`, `LORAE5_APP_APP_KEY`).
Si les trois macros sont vides, le module utilise ses propres clés internes.

- `region` : `"EU868"`, `"US915"`, etc.
- `periodic_payload` : chaîne ASCII envoyée périodiquement après le join.
- `uplink_period_ms` : période entre deux uplinks (en ms, > 0).
- `startup_delay_ms` : délai avant la première action AT (laisser le module démarrer).

#### `LORAE5_StartManagedOtaaWithKeys`

```c
LORAE5_Status LORAE5_StartManagedOtaaWithKeys(LORAE5_Handle_t *handle,
                                               const char *region,
                                               const char *periodic_payload,
                                               uint32_t uplink_period_ms,
                                               uint32_t startup_delay_ms,
                                               const char *dev_eui,
                                               const char *app_eui,
                                               const char *app_key);
```

Identique à `LORAE5_StartManagedOtaa` mais les clés sont passées en paramètre.

- `dev_eui` : 16 caractères hexadécimaux (ex : `"70B3D57ED0ABCDEF"`) ou NULL.
- `app_eui` : 16 caractères hexadécimaux ou NULL.
- `app_key` : 32 caractères hexadécimaux ou NULL.
  Si les trois sont NULL, le comportement est identique à `LORAE5_StartManagedOtaa` sans clés.

#### `LORAE5_Task`

```c
void LORAE5_Task(LORAE5_Handle_t *handle, uint32_t now_ms);
```

Avance la FSM OTAA managée. Appeler **à chaque tour de boucle principale**.

- `now_ms` = `HAL_GetTick()` — doit être monotone, ne pas passer une valeur artificielle.
- Non-bloquant : revient immédiatement si aucune action n'est due.
- Gère les timers avec `(int32_t)(now_ms - target) >= 0` — correct lors du wraparound 32 bits à ~49 jours.

---

### 6.8 Consommation des lignes et événements

#### `LORAE5_PopLine`

```c
bool LORAE5_PopLine(LORAE5_Handle_t *handle, char *out, size_t out_size);
```

Dépile une ligne depuis la file circulaire RX. Retourne `true` si une ligne a été copiée dans `out`, `false` si la file est vide. **Appeler en boucle** jusqu'à retour `false` pour vider toutes les lignes en attente.

```c
char line[128];
while (LORAE5_PopLine(&hlorae5, line, sizeof(line)))
    printf("[AT] %s\r\n", line);
```

La copie est protégée par une section critique courte (`__disable_irq`).

#### `LORAE5_ConsumeJoinOkEvent`

```c
bool LORAE5_ConsumeJoinOkEvent(LORAE5_Handle_t *handle);
```

Retourne `true` une seule fois après réception de `+JOIN: Network joined`. Remet le flag à zéro. Appels suivants retournent `false` jusqu'au prochain join réussi.

#### `LORAE5_ConsumeJoinFailEvent`

```c
bool LORAE5_ConsumeJoinFailEvent(LORAE5_Handle_t *handle);
```

Identique pour `+JOIN: Join failed`.

#### `LORAE5_ConsumeDownlinkEvent`

```c
bool LORAE5_ConsumeDownlinkEvent(LORAE5_Handle_t *handle,
                                  uint8_t *port_out,
                                  char *data_out, size_t data_out_size,
                                  bool *is_hex_out);
```

Retourne `true` si un downlink a été reçu. Renseigne :

- `port_out` : port applicatif LoRaWAN (1–223).
- `data_out` : payload sous forme de chaîne (ASCII ou hex selon `is_hex_out`).
- `is_hex_out` : `true` si le payload provient d'un `+MSGHEX:`.

Les trois pointeurs de sortie peuvent être NULL si l'information n'est pas nécessaire.

Le downlink est détecté sur les patterns AT :

- `+MSG: PORT: X; RX: "payload"` (uplink ASCII → downlink ASCII)
- `+MSGHEX: PORT: X; RX: "DEADBEEF"` (uplink hex → downlink hex)

#### `LORAE5_IsJoined`

```c
bool LORAE5_IsJoined(const LORAE5_Handle_t *handle);
```

Retourne l'état de connexion courant (flag `joined`). Lecture simple, pas de consommation d'événement.

---

## 7. Configuration compile-time

Toutes les valeurs sont dans `STM32_LORAE5_conf.h` et surchargeable via `-D` ou en modifiant le fichier.

| Macro                         |    Défaut | Description                                                                 |
| ----------------------------- | --------: | --------------------------------------------------------------------------- |
| `LORAE5_DEBUG_ENABLE`         |       `0` | `1` → active des `printf` de debug internes à la librairie                  |
| `LORAE5_RX_LINE_MAX`          |     `256` | Taille max d'une ligne RX en octets (inclut le `\0`)                        |
| `LORAE5_TX_BUF_MAX`           |     `192` | Taille du buffer TX interne (`handle->tx_buf`, utilisé par `LORAE5_SendAT`) |
| `LORAE5_TERM_CMD_MAX`         |     `128` | Taille max d'une commande saisie via le terminal AT debug                   |
| `LORAE5_LINE_QUEUE_DEPTH`     |       `4` | Nombre de lignes pouvant être en attente dans la file (min 2)               |
| `LORAE5_APP_REGION`           | `"EU868"` | Région LoRaWAN pour `LORAE5_StartManagedOtaa`                               |
| `LORAE5_APP_UPLINK_PAYLOAD`   | `"HELLO"` | Payload ASCII uplink périodique                                             |
| `LORAE5_APP_UPLINK_PERIOD_MS` |   `15000` | Période uplink en ms                                                        |
| `LORAE5_APP_STARTUP_DELAY_MS` |     `200` | Délai initial FSM avant la première commande AT                             |
| `LORAE5_APP_DEV_EUI`          |      `""` | DevEUI 16 hex chars — laisser vide pour utiliser les clés du module         |
| `LORAE5_APP_APP_EUI`          |      `""` | AppEUI 16 hex chars                                                         |
| `LORAE5_APP_APP_KEY`          |      `""` | AppKey 32 hex chars                                                         |
| `LORAE5_JOIN_TIMEOUT_MS`      |   `30000` | Watchdog join : si pas de réponse en 30 s, retry automatique                |
| `LORAE5_JOIN_RETRY_DELAY_MS`  |    `5000` | Délai entre deux tentatives de join                                         |
| `LORAE5_TERM_TX_TIMEOUT_MS`   |      `20` | Timeout `HAL_UART_Transmit` (mode bloquant) pour le terminal debug          |

Des `_Static_assert` valident à la compilation :

- La longueur de `LORAE5_APP_DEV_EUI` est 0 ou 16.
- La longueur de `LORAE5_APP_APP_EUI` est 0 ou 16.
- La longueur de `LORAE5_APP_APP_KEY` est 0 ou 32.
- `LORAE5_LINE_QUEUE_DEPTH >= 2`.

---

## 8. Codes de retour

```c
typedef enum {
    LORAE5_OK,           // Succès
    LORAE5_ERR_NULL_PTR, // Paramètre pointeur invalide (NULL non autorisé)
    LORAE5_ERR_BUSY,     // TX en cours — réessayer plus tard (non-bloquant normal)
    LORAE5_ERR_UART,     // Erreur HAL lors du démarrage TX ou DMA
    LORAE5_ERR_ARG,      // Argument invalide (longueur 0, format clé incorrect…)
    LORAE5_ERR_TIMEOUT,  // Réservé applicatif — la lib n'en génère pas
    LORAE5_ERR_OVERFLOW  // Overflow ligne RX (ligne trop longue ou file pleine)
} LORAE5_Status;
```

`LORAE5_ERR_BUSY` est un résultat **normal** en environnement non-bloquant.
La FSM managée le gère automatiquement (retry à la prochaine itération).
Dans du code applicatif direct, il faut retenter à l'itération suivante.

Convertir en chaîne lisible : `LORAE5_StatusToString(status)`.

---

## 9. Machine d'état OTAA managée

La FSM avance step par step, avec un timer anti-wraparound entre chaque action.

| Étape (`managed_step`) | Action AT envoyée                                  | Condition de passage               |
| ---------------------- | -------------------------------------------------- | ---------------------------------- |
| 0 → 1                  | `AT` (ping)                                        | Timer `startup_delay_ms` écoulé    |
| 1 → 2                  | `AT+VER` (version)                                 | Immédiat après step 0              |
| 2 → 3                  | `AT+MODE=LWOTAA`                                   | Immédiat                           |
| 3 → 4                  | `AT+DR=<region>`                                   | Immédiat                           |
| 4 → 5 _(si clés)_      | `AT+ID=DevEui,"<dev_eui>"`                         | Clés non NULL                      |
| 5 → 6 _(si clés)_      | `AT+ID=AppEui,"<app_eui>"`                         | Clés non NULL                      |
| 6 → 7 _(si clés)_      | `AT+KEY=AppKey,"<app_key>"`                        | Clés non NULL                      |
| 7 → 8                  | `AT+JOIN`                                          | Immédiat                           |
| 8                      | _(attente join)_                                   | `evt_join_ok` ou timeout watchdog  |
| join ok → 8            | `AT+MSG="<payload>"` toutes les `uplink_period_ms` | `joined == true`                   |
| join fail / timeout    | → step 7 (retry join)                              | Après `LORAE5_JOIN_RETRY_DELAY_MS` |

Quand les clés sont NULL (ou `LORAE5_APP_DEV_EUI == ""`), les étapes 4–6 sont sautées.

La FSM **ne bloque pas** : chaque step envoie une commande et revient. Si le TX est `BUSY`, la step est retentée à l'itération suivante de `LORAE5_Task`.

---

## 10. Événements applicatifs

Les événements sont des flags `volatile bool` posés depuis l'ISR et consommés depuis le main.
La lecture en main est protégée par une section critique courte.

| Événement       | Posé quand                        | Consommé par                  |
| --------------- | --------------------------------- | ----------------------------- |
| `evt_join_ok`   | `+JOIN: Network joined` reçu      | `LORAE5_ConsumeJoinOkEvent`   |
| `evt_join_fail` | `+JOIN: Join failed` reçu         | `LORAE5_ConsumeJoinFailEvent` |
| `evt_downlink`  | `+MSG: PORT:…RX:…` ou `+MSGHEX:…` | `LORAE5_ConsumeDownlinkEvent` |

Les flags sont remis à zéro lors de la consommation (pattern consume-and-clear).
Si plusieurs downlinks arrivent avant la consommation, seul le dernier est conservé.

---

## 11. Terminal AT debug

Le terminal AT debug est un pont interactif **UART console (PC) ↔ UART LoRa-E5**.
Il permet de taper des commandes AT à la main dans un terminal série et d'en voir les réponses en temps réel, sans modifier la logique applicatif.

### Initialisation

```c
static LORAE5_Terminal term;

/* Après LORAE5_Init et LORAE5_StartRx */
LORAE5_TerminalBegin(&term, &huart2, &hlorae5,
                      true,   /* echo : affiche les caractères tapés */
                      true);  /* show_prompt : affiche "> " après chaque réponse */
```

`LORAE5_TerminalBegin` lance `HAL_UART_Receive_IT` sur l'UART debug (1 octet par IT).

### Callbacks supplémentaires à ajouter

```c
/* Dans HAL_UART_RxCpltCallback — distinct de HAL_UARTEx_RxEventCallback */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    LORAE5_TerminalRxCallback(&term, huart);
}
```

### Boucle principale

```c
while (1) {
    LORAE5_TerminalTask(&term);   /* non-bloquant */
    /* ... reste de la boucle */
}
```

`LORAE5_TerminalTask` :

1. Si une commande est prête (`cmd_ready`), l'envoie via `LORAE5_SendAT`.
2. Vide la file de lignes RX et les affiche sur l'UART debug.

- Non-bloquant : WCET borné à `LORAE5_LINE_QUEUE_DEPTH` itérations de `PopLine`, pas de timeout `HAL_GetTick`.

---

## 12. Diagnostic et détection d'overflow

### Compteur d'overflow RX

Si la file de lignes RX (`line_queue`) est pleine au moment d'une nouvelle ligne, la ligne est perdue et le compteur `rx_overflow_count` est incrémenté (saturation à 255).

```c
/* Lire le compteur */
uint8_t ov = LORAE5_GetOverflowCount(&hlorae5);
if (ov > 0)
    printf("Lignes perdues : %u\r\n", ov);

/* Remettre à zéro */
LORAE5_ClearOverflowCount(&hlorae5);
```

Si des overflows apparaissent régulièrement :

- Augmenter `LORAE5_LINE_QUEUE_DEPTH` dans `conf.h`.
- Appeler `LORAE5_PopLine` plus fréquemment (boucle while, pas une seule fois par tour).
- Réduire la fréquence des commandes AT ou la verbosité du module.

### Dernier code d'erreur

`handle->last_error` contient le dernier code de retour non-OK de la librairie.
`handle->consecutive_errors` compte les erreurs UART consécutives (remis à 0 sur succès).

### `LORAE5_StatusToString`

```c
printf("Erreur : %s\r\n", LORAE5_StatusToString(handle->last_error));
```

---

## 13. Limites connues

- **FSM ABP non incluse** : la FSM managée couvre uniquement le flux OTAA. Pour ABP, configurer manuellement via `LORAE5_SetMode(LORAE5_MODE_LWABP)` et les commandes AT dédiées.
- **Callback `on_line` en ISR** : la fonction enregistrée via `LORAE5_SetLineCallback` est appelée dans le contexte IRQ DMA. Ne pas y faire de traitement lourd. Utiliser `LORAE5_PopLine` depuis le main pour un usage sûr.
- **WCET `LORAE5_Task`** : les étapes réseau (join, uplink) ont une durée dépendant du réseau LoRaWAN — non bornable statiquement.
- **`LORAE5_TerminalTask`** : non-bloquant, WCET borné à `LORAE5_LINE_QUEUE_DEPTH` itérations.
- **Thread unique** : la librairie est conçue pour un usage main + ISR. Si plusieurs tâches RTOS accèdent au handle, ajouter un mutex au niveau applicatif autour de `LORAE5_Task`, `PopLine` et `Consume*`.
- **Un seul downlink conservé** : si plusieurs downlinks arrivent sans consommation intermédiaire, seul le dernier est accessible via `ConsumeDownlinkEvent`.

---

## 14. Conformité et garanties runtime

- **0 allocation dynamique** — aucun `malloc`, `calloc`, `realloc`, `free`.
- **Sections critiques `__disable_irq`** uniquement côté consommateur (`PopLine`, `Consume*`) — le producteur ISR n'en a pas besoin (il est seul à écrire).
- **Barrière mémoire `__DMB()`** après écriture de `line_q_head` côté ISR — garantit la visibilité des données sur Cortex-M avant que le flag soit visible du main.
- **Snapshot atomique** de `evt_join_ok`/`evt_join_fail` dans `LORAE5_Task` : lecture sous section critique courte (safe Cortex-M0 où les bool ne sont pas atomiques).
- **Anti-wraparound `HAL_GetTick()`** : pattern `(int32_t)(now - target) >= 0` — correct modulo 2³² (environ 49 jours de fonctionnement continu).
- **Overflow queue saturé à 255** sans undefined behavior.
- **36 tests unitaires host** (sans matériel) reproduisibles via `check_host_at.sh --lib LORAE5`, couvrant : init, DMA wraparound, queue overflow, sections critiques, FSM OTAA, downlink parsing, terminal non-bloquant.

---

## 15. Vérification automatisée

Depuis la racine du workspace :

```bash
bash dev/Prompt_Audit_Suivi/Audit_LIBS/check_host_at.sh --lib LORAE5
```

Ce script compile et exécute la suite de tests host Linux (gcc, `-Wall -Wextra -Werror`).
Sortie attendue : `LORAE5 host tests: 36 run, 0 failed`.

Tests couverts : init, DMA wraparound, queue overflow+clear, join ok/fail, downlink ASCII/hex, sections critiques `__DMB`/`__disable_irq`, FSM OTAA (step 0, join timeout, snapshot), TerminalTask non-bloquant, `StatusToString`.

Procédure standard (fork initial ou reprise après plusieurs mois) :

Suivi STD centralisé : `dev/Prompt_Audit_Suivi/Suivi_LIBS/SUIVI_STM32_LORAE5/STD_LIB_DEV/`.

1. Valider la base logicielle

```bash
bash dev/Prompt_Audit_Suivi/Audit_LIBS/check_host_at.sh --lib LORAE5
```

Attendu : `LORAE5 host tests: 36 run, 0 failed`.

2. Revalider l'intégration STM32 minimale

- Vérifier UART LoRa-E5 en 8N1, DMA RX circulaire actif.
- Vérifier les callbacks HAL reliés : `LORAE5_OnRxEvent` et `LORAE5_OnTxCplt`.
- Vérifier que `LORAE5_Task(&h, HAL_GetTick())` tourne à chaque boucle principale.

3. Rejouer la campagne terrain minimale

- TC-A : boot + `LORAE5_StartRx` + réception de lignes AT.
- TC-B : OTAA join réussi (event join ok consommé).
- TC-C : OTAA join échoué + retry (event join fail observé).
- TC-D : downlink reçu puis consommé via `LORAE5_ConsumeDownlinkEvent`.
- TC-E : burst RX sans perte (pas d'overflow persistant).

4. Décider le niveau qualité

- Si tests host + campagne terrain sont OK et reproductibles : niveau STD robuste confirmé.
- Niveau STD seulement si mesures latence/WCET et compatibilité multi-cibles sont relevées et tracées.

Tu peux exécuter toute la reprise uniquement avec ce README : cette procédure suffit pour redémarrer proprement.

---

## 16. Exemples disponibles

| Fichier                         | Scénario                                    | Niveau        |
| ------------------------------- | ------------------------------------------- | ------------- |
| `exemple_lorae5_otaa_managed.c` | OTAA managé complet, flux nominal LoRaWAN   | Débutant      |
| `exemple_lorae5_api_coverage.c` | Démonstration large de l'API publique       | Intermédiaire |
| `exemple_lorae5_terminal_at.c`  | Terminal AT interactif UART debug ↔ LoRa-E5 | Intermédiaire |
| `exemple_lorae5_polling_at.c`   | Échanges AT directs, diagnostic liaison     | Intermédiaire |
| `exemple_lorae5_mode_rx.c`      | Mode test radio réception (`AT+MODE=TEST`)  | Avancé        |
| `exemple_lorae5_mode_tx.c`      | Mode test radio émission (`AT+MODE=TEST`)   | Avancé        |

Ordre conseillé pour un premier fork : `otaa_managed` → `polling_at` → `terminal_at`.

---

## 17. Dépannage rapide

| Symptôme                         | Cause probable                                                   | Solution                                                   |
| -------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------- |
| Aucune ligne reçue               | `LORAE5_OnRxEvent` non appelé                                    | Vérifier `HAL_UARTEx_RxEventCallback` → `LORAE5_OnRxEvent` |
| Aucune ligne reçue               | DMA RX non démarré                                               | Vérifier l'appel à `LORAE5_StartRx`                        |
| TX bloqué (`LORAE5_ERR_BUSY`)    | `LORAE5_OnTxCplt` non appelé                                     | Vérifier `HAL_UART_TxCpltCallback` → `LORAE5_OnTxCplt`     |
| Join échoue systématiquement     | Clés OTAA incorrectes ou couverture réseau absente               | Vérifier DevEUI/AppEUI/AppKey et signal LoRa               |
| Lignes perdues (overflow)        | `PopLine` appelé trop rarement                                   | Appeler `PopLine` en boucle à chaque tour de boucle        |
| Lignes perdues (overflow)        | `LORAE5_LINE_QUEUE_DEPTH` trop faible                            | Augmenter dans `conf.h` (rebuilder)                        |
| FSM OTAA ne démarre pas          | `LORAE5_Task` non appelé                                         | Appeler `LORAE5_Task(&h, HAL_GetTick())` dans `while(1)`   |
| Terminal AT sans écho            | `HAL_UART_RxCpltCallback` → `LORAE5_TerminalRxCallback` manquant | Ajouter le callback                                        |
| Module muet après `LORAE5_Reset` | Boot du module ~500 ms avec texte UART                           | Attendre et lire les lignes de boot via `PopLine`          |

---

## Version et licence

- `LORAE5_LIB_VERSION_MAJOR` = `1`
- `LORAE5_LIB_VERSION_MINOR` = `0`
- `LORAE5_LIB_VERSION_PATCH` = `0`

Licence : **MIT**
