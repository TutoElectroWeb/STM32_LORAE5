# Exemples complets (copier/coller)

Tu voulais des fichiers **complets** type `main.c` (pas des snippets). Ils sont ici:

- `main_terminal_at_example.c` : terminal AT interactif (style WifiESP)
- `main_managed_otaa_example.c` : OTAA géré automatiquement
- `main_test_mode_tx_example.c` : mode test radio TX périodique
- `main_test_mode_rx_example.c` : mode test radio RX

Voir aussi `README.md` dans ce dossier pour le détail de chaque scénario.

## Utilisation

1. Copie le contenu d’un exemple dans ton `Core/Src/main.c`.
2. Exclue les fichiers `examples/*.c` du build (comme tu l’as prévu).
3. Build/flash.

## Pré-requis matériels / CubeMX

- `USART1` connecté au LoRa-E5 en `9600 8N1`
- `USART2` pour console debug `115200 8N1`
- RX DMA sur `USART1` en mode **CIRCULAR** (`DMA2_Stream2`)
- IRQ actives: `USART1_IRQn`, `DMA2_Stream2_IRQn`

## Commandes à tester (terminal)

- `AT`
- `AT+VER`
- `AT+ID`
- `AT+MODE?`
- `AT+DR?`
