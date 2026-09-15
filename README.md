# RAK4631 Application

Zephyr RTOS application for the RAKwireless **RAK4631** (Nordic **nRF52840**) board.

## Layout

```
.
├── CMakeLists.txt              # application build entry point
├── Kconfig                     # application Kconfig options (+ ICC_LOG_LEVEL)
├── prj.conf                    # base Kconfig configuration (all boards)
├── app.overlay                 # base devicetree overlay (all boards)
├── sysbuild.conf               # multi-image / MCUboot configuration
├── VERSION                     # application version
├── boards/
│   ├── rak4631_nrf52840.conf       # board-specific Kconfig fragment
│   └── rak4631_nrf52840.overlay    # board-specific devicetree overlay
├── source/
│   ├── main.c                  # application entry point
│   ├── lora/                   # point-to-point LoRa (raw radio)
│   ├── lorawan/                # LoRaWAN (OTAA, active in main.c)
│   └── icc_common_core/        # shared ICC library (git submodule)
├── west-manifest/west.yml      # west manifest (Zephyr + module revisions)
└── zephyr/ modules/ …          # west-managed workspace (git-ignored)
```

## Prerequisites

The dev container already provides the toolchain:

- west (in `.venv`)
- Zephyr SDK 1.0.1 (`~/.zephyr_ide/toolchains/zephyr-sdk-1.0.1`)
- cmake, ninja, dtc, gperf

First checkout:

```bash
git submodule update --init --recursive
west update            # populates zephyr/ and modules/
```

## Build

```bash
west build -b rak4631/nrf52840 .
```

With MCUboot (sysbuild):

```bash
west build -b rak4631/nrf52840 --sysbuild . -- -DSB_CONFIG_BOOTLOADER_MCUBOOT=y
```

## Flash

```bash
west flash            # via J-Link / SEGGER RTT
```

## LoRa / LoRaWAN

O rádio SX1262 é declarado uma única vez no devicetree do board, sob o
alias `lora0`; as duas implementações em `source/` reutilizam o mesmo
nó de hardware.

- **`source/lora/`** — LoRa ponto a ponto puro, via
  `<zephyr/drivers/lora.h>` (driver, sem stack de rede). RF (frequência,
  SF, banda, coding rate) é configurada manualmente em runtime com
  `lora_config()`; envio com `lora_send()`. Sem join, sessão ou
  criptografia. Requer só `CONFIG_LORA=y`.
- **`source/lorawan/`** — LoRaWAN via `<zephyr/lorawan/lorawan.h>`
  (subsistema Zephyr, backend `loramac-node`). Join OTAA
  (`lorawan_join()`) com DevEUI/JoinEUI/AppKey fixos no código, uplinks
  com `lorawan_send()`. Região e plano de canais vêm do Kconfig
  (`CONFIG_LORAWAN_REGION_US915`), não do código. Requer
  `CONFIG_LORA=y`, `CONFIG_LORAWAN=y`, região, e stacks aumentadas
  (`CONFIG_MAIN_STACK_SIZE`, `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE`) para
  a stack de criptografia síncrona do join/send.

Estado atual: `main.c` chama apenas a LoRaWAN (`lorawan_init()` /
`lorawan_send_payload()`); o caminho LoRa puro está comentado em
`main.c`, mas continua compilado.

Detalhes de devicetree, Kconfig e parâmetros: [docs/lora-lorawan-readme.md](docs/lora-lorawan-readme.md).

## ICC common core

`source/icc_common_core` is a git submodule providing standardized error codes
(`icc_error.h`), portable types (`icc_types.h`) and a logging facade
(`icc_log.h` / `icc_log.c`). The Zephyr backend registers a log module named
`icc`; its level is controlled by `CONFIG_ICC_LOG_LEVEL` (see `Kconfig`).
