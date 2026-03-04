# Exemples STM32_LORAE5 (état actuel)

Ce document couvre uniquement les exemples actuellement présents dans le dossier `exemples/` de `STM32_LORAE5`.

---

## 1) Objectif des exemples

Les fichiers `exemple_lorae5_*.c` sont des `main.c` prêts à intégrer dans un projet STM32CubeIDE pour illustrer :

- l’initialisation HAL + UART + DMA,
- l’intégration correcte des callbacks,
- les scénarios LoRa-E5 AT les plus utiles,
- l’utilisation pédagogique de l’API publique.

---

## 2) Liste des exemples

| Fichier                         | Scénario principal                               | Niveau        |
| ------------------------------- | ------------------------------------------------ | ------------- |
| `exemple_lorae5_otaa_managed.c` | OTAA managé en boucle principale (`LORAE5_Task`) | Débutant      |
| `exemple_lorae5_api_coverage.c` | Démonstration large des fonctions API            | Intermédiaire |
| `exemple_lorae5_terminal_at.c`  | Terminal AT interactif UART debug ↔ LoRa-E5      | Intermédiaire |
| `exemple_lorae5_polling_at.c`   | Échanges AT simples orientés diagnostic          | Intermédiaire |
| `exemple_lorae5_mode_rx.c`      | Mode test radio réception (`AT+MODE=TEST`)       | Avancé        |
| `exemple_lorae5_mode_tx.c`      | Mode test radio émission (`AT+MODE=TEST`)        | Avancé        |

Important : inclure **un seul exemple** dans le build final d’une cible donnée.

---

## 3) Pré-requis CubeMX communs

| Élément             | Réglage attendu                          |
| ------------------- | ---------------------------------------- |
| UART LoRa-E5        | UART asynchrone 8N1, 9600 bauds          |
| DMA RX LoRa-E5      | DMA circulaire pour RX UART              |
| DMA TX LoRa-E5      | DMA TX si utilisé (sinon IT TX possible) |
| NVIC UART LoRa-E5   | Interruptions UART actives               |
| SysTick             | Tick 1 ms (`HAL_GetTick`)                |
| UART debug (option) | Recommandé pour traces et terminal       |

---

## 4) Squelette d’intégration commun

```c
static uint8_t lora_rx_dma[256];
static LORAE5_Handle_t hlorae5;

LORAE5_Init(&hlorae5, &huart1, lora_rx_dma, sizeof(lora_rx_dma));
LORAE5_StartRx(&hlorae5);

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        LORAE5_OnRxEvent(&hlorae5, Size);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        LORAE5_OnTxCplt(&hlorae5);
    }
}
```

---

## 5) Choisir le bon exemple

- Tu veux **le flux nominal LoRaWAN** : démarre par `exemple_lorae5_otaa_managed.c`.
- Tu veux **voir un maximum d’API** : utilise `exemple_lorae5_api_coverage.c`.
- Tu veux **envoyer des AT à la main** : prends `exemple_lorae5_terminal_at.c`.
- Tu veux **diagnostiquer rapidement la liaison** : prends `exemple_lorae5_polling_at.c`.
- Tu veux **tests radio bruts RX/TX** : utilise `exemple_lorae5_mode_rx.c` / `exemple_lorae5_mode_tx.c`.

---

## 6) Configuration LoRaWAN (OTAA)

Les paramètres OTAA utilisés par les exemples managés viennent de `STM32_LORAE5_conf.h` :

- `LORAE5_APP_REGION`
- `LORAE5_APP_DEV_EUI`
- `LORAE5_APP_APP_EUI`
- `LORAE5_APP_APP_KEY`
- `LORAE5_APP_UPLINK_PAYLOAD`
- `LORAE5_APP_UPLINK_PERIOD_MS`

Les longueurs des clés sont contrôlées par `_Static_assert` à la compilation.

---

## 7) Matrice API → exemples

Légende :

- ✅ : API utilisée explicitement dans l’exemple
- ➖ : non utilisée dans ce scénario

| Fonction API                      | api_coverage | otaa_managed | terminal_at | polling_at | mode_rx | mode_tx |
| --------------------------------- | :----------: | :----------: | :---------: | :--------: | :-----: | :-----: |
| `LORAE5_Init`                     |      ✅      |      ✅      |     ✅      |     ✅     |   ✅    |   ✅    |
| `LORAE5_DeInit`                   |      ➖      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_StartRx`                  |      ✅      |      ✅      |     ✅      |     ✅     |   ✅    |   ✅    |
| `LORAE5_StopRx`                   |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_SetLineCallback`          |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_OnRxEvent`                |      ✅      |      ✅      |     ✅      |     ✅     |   ✅    |   ✅    |
| `LORAE5_OnTxCplt`                 |      ✅      |      ✅      |     ✅      |     ✅     |   ✅    |   ✅    |
| `LORAE5_SendRaw`                  |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_SendAT`                   |      ➖      |      ➖      |     ➖      |     ✅     |   ✅    |   ✅    |
| `LORAE5_Ping`                     |      ➖      |      ➖      |     ➖      |     ✅     |   ✅    |   ✅    |
| `LORAE5_Reset`                    |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_GetVersion`               |      ➖      |      ➖      |     ✅      |     ➖     |   ➖    |   ➖    |
| `LORAE5_SetMode`                  |      ➖      |      ➖      |     ➖      |     ➖     |   ✅    |   ✅    |
| `LORAE5_SetRegion`                |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_Join`                     |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_SendMsg`                  |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_SendMsgHex`               |      ✅      |      ➖      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_StartManagedOtaa`         |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_StartManagedOtaaWithKeys` |      ➖      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_Task`                     |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_PopLine`                  |      ✅      |      ✅      |     ✅      |     ✅     |   ✅    |   ✅    |
| `LORAE5_ConsumeJoinOkEvent`       |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_ConsumeJoinFailEvent`     |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_ConsumeDownlinkEvent`     |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_IsJoined`                 |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_TerminalBegin`            |      ➖      |      ➖      |     ✅      |     ➖     |   ➖    |   ➖    |
| `LORAE5_TerminalRxCallback`       |      ➖      |      ➖      |     ✅      |     ➖     |   ➖    |   ➖    |
| `LORAE5_TerminalTask`             |      ➖      |      ➖      |     ✅      |     ➖     |   ➖    |   ➖    |
| `LORAE5_StatusToString`           |      ✅      |      ➖      |     ➖      |     ✅     |   ➖    |   ➖    |
| `LORAE5_GetOverflowCount`         |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |
| `LORAE5_ClearOverflowCount`       |      ✅      |      ✅      |     ➖      |     ➖     |   ➖    |   ➖    |

---

## 8) Procédure d’utilisation recommandée

1. Choisir un exemple adapté au besoin (section 5).
2. Copier son contenu dans `Core/Src/main.c` d’un projet STM32CubeIDE.
3. Ajouter `STM32_LORAE5.c/.h/.conf.h` au projet.
4. Configurer UART/DMA/NVIC dans CubeMX.
5. Renseigner région et clés OTAA dans `STM32_LORAE5_conf.h`.
6. Vérifier le mapping des callbacks HAL vers `LORAE5_OnRxEvent` et `LORAE5_OnTxCplt`.
7. Compiler et observer les traces UART debug.

---

## 9) Dépannage rapide

- Pas de réception AT : vérifier DMA RX circulaire + callback `HAL_UARTEx_RxEventCallback`.
- TX bloqué en `BUSY` : vérifier `HAL_UART_TxCpltCallback`.
- Join échoue : vérifier région, clés OTAA et couverture réseau.
- Lignes perdues : augmenter `LORAE5_LINE_QUEUE_DEPTH` ou dépiler plus souvent.

---

## 10) Portage vers une autre cible STM32

Les exemples sont transposables, à condition d’adapter :

- instances UART/DMA (`huartX`, `hdma_usartX_rx`),
- horloge système,
- configuration NVIC,
- éventuel retarget `printf`.

Le contrat d’intégration de la librairie reste identique.
