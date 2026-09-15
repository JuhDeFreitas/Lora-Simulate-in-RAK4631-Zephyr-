# LoRa e LoRaWAN no Zephyr — devicetree, prj.conf, parâmetros e API

Documentação de como as duas implementações (`source/lora/lora.c` e
`source/lorawan/lorawan.c`) foram montadas, comparando lado a lado
devicetree, Kconfig, parâmetros de código e as funções/bibliotecas do
Zephyr usadas em cada uma.

Resumo do estado atual: as duas continuam no repositório, mas só a
LoRaWAN está com a chamada ativa em `main.c` — a LoRa pura ficou como
código de referência (comentado em `main.c`, ainda compilado pelo
`CMakeLists.txt`).

## 1. Devicetree

As duas implementações apontam para o **mesmo nó de hardware** — a
devicetree descreve o chip de rádio (SX1262) uma única vez, no nível do
board; o que muda entre LoRa e LoRaWAN é só a camada de software por
cima dele.

```
lora: lora@0 {
    compatible = "semtech,sx1262";
    reg = <0>;
    reset-gpios      = <&gpio1 6 GPIO_ACTIVE_LOW>;
    busy-gpios       = <&gpio1 14 GPIO_ACTIVE_HIGH>;
    rx-enable-gpios  = <&gpio1 5 GPIO_ACTIVE_LOW>;
    dio1-gpios       = <&gpio1 15 GPIO_ACTIVE_HIGH>;
    dio2-tx-enable;
    dio3-tcxo-voltage = <SX126X_DIO3_TCXO_3V3>;
    tcxo-power-startup-delay-ms = <5>;
    spi-max-frequency = <1000000>;
};

aliases {
    lora0 = &lora;
};
```

Ambos os arquivos C leem esse mesmo nó, do mesmo jeito:

```c
#define LORA_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LORA_NODE), "LoRa alias missing in device tree.");
static const struct device *const lora_dev = DEVICE_DT_GET(LORA_NODE);
```

| | LoRa | LoRaWAN |
|---|---|---|
| Nó usado | `lora0` | `lora0` (idêntico) |
| Configuração adicional na DT | nenhuma | nenhuma |

## 2. prj.conf

| | LoRa (`source/lora`) | LoRaWAN (`source/lorawan`) |
|---|---|---|
| Habilita driver do rádio | `CONFIG_LORA=y` | `CONFIG_LORA=y` |
| Habilita subsistema de rede | — | `CONFIG_LORAWAN=y` |
| Região/plano de canais | — (frequência é manual no código) | `CONFIG_LORAWAN_REGION_US915=y` |
| Pilha da workqueue | não relevante | `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048` — exigida pelo Secure Element em software (`HAS_SEMTECH_SOFT_SE`) do backend `loramac-node`; abaixo de 2048 a opção é descartada em silêncio e faltam símbolos `SecureElement*` no link |
| Pilha da thread principal | não relevante (envio simples, sem cripto) | `CONFIG_MAIN_STACK_SIZE=2048` — `lorawan_join()`/`lorawan_send()` executam a stack de criptografia de forma síncrona na thread chamadora |

Ou seja: a LoRa pura só precisa de uma linha (`CONFIG_LORA=y`). A LoRaWAN
precisa disso mais o subsistema de rede, a região, e duas pilhas maiores
que o padrão da placa — sem essas duas últimas, o build ou falha no
link ou estoura a pilha em runtime.

## 3. Parâmetros de configuração no código

### LoRa — configuração manual de RF (`struct lora_modem_config`)

```c
#define LORA_FREQ_HZ 902700000

static struct lora_modem_config lora_cfg = {
	.frequency    = LORA_FREQ_HZ,   /* 902.7 MHz */
	.bandwidth    = BW_125_KHZ,
	.datarate     = SF_10,
	.coding_rate  = CR_4_5,
	.preamble_len = 8,
	.tx_power     = 14,             /* dBm */
	.iq_inverted  = false,
	.public_network = false,
	.tx = true,
};
```

Cada parâmetro de RF (frequência, banda, spreading factor, coding rate,
potência) é escolhido manualmente pelo código — não existe conceito de
"região" aqui, é o rádio configurado ponto a ponto.

### LoRaWAN — credenciais e parâmetros de protocolo

```c
#define LORAWAN_DEV_EUI  { 0xDD, 0xEE, 0xAA, 0xDD, 0xBB, 0xEE, 0xEE, 0xFF }
#define LORAWAN_JOIN_EUI { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORAWAN_APP_KEY  { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, \
                            0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define LORAWAN_PORT 2
#define LORAWAN_JOIN_RETRY_DELAY_S 10
```

Aqui não se configura frequência/SF/BW diretamente — isso é derivado da
região (`CONFIG_LORAWAN_REGION_US915`). O que se configura é a
identidade do device na rede (DevEUI/JoinEUI/AppKey), a porta de
aplicação e o comportamento de retry do join.

| | LoRa | LoRaWAN |
|---|---|---|
| O que se configura | parâmetros de RF (frequência, SF, BW, potência) | identidade na rede (EUIs, chave), porta, retry |
| Quem define a frequência/canais | o código, manualmente | a região (Kconfig) + o Network Server |
| Existe conceito de "sessão"/join? | não | sim (OTAA) |

## 4. Funções do Zephyr e biblioteca utilizada

| | LoRa | LoRaWAN |
|---|---|---|
| Header incluído | `<zephyr/drivers/lora.h>` | `<zephyr/lorawan/lorawan.h>` |
| Camada Zephyr | driver de dispositivo (`drivers/lora/`) | subsistema (`subsys/lorawan/`) |
| Biblioteca por trás | nenhuma extra — fala direto com o driver do chip | backend `loramac-node` (stack de referência da Semtech, módulo `modules/lib/loramac-node`), com Secure Element em software para AES/CMAC |

Funções realmente chamadas em cada implementação:

**LoRa** (`source/lora/lora.c`):

| Função | Papel |
|---|---|
| `device_is_ready(dev)` | confirma que o driver inicializou |
| `lora_config(dev, &cfg)` | aplica a configuração de RF no rádio |
| `lora_send(dev, data, len)` | transmite os bytes (bloqueante) |

**LoRaWAN** (`source/lorawan/lorawan.c`):

| Função | Papel |
|---|---|
| `device_is_ready(dev)` | confirma que o driver inicializou |
| `lorawan_start()` | sobe a stack MAC (uma vez, antes do join) |
| `lorawan_register_downlink_callback()` | registra callback para downlinks recebidos |
| `lorawan_register_dr_changed_callback()` | registra callback para mudança de datarate (ADR) |
| `lorawan_join(&join_cfg)` | executa o handshake OTAA; bloqueia até aceitar/rejeitar/timeout |
| `lorawan_send(port, data, len, tipo)` | envia um uplink (aqui, `LORAWAN_MSG_UNCONFIRMED`) |
| `lorawan_get_payload_sizes()` | consulta o tamanho máximo de payload no datarate atual (usada dentro do callback de ADR) |

Funções da API LoRaWAN disponíveis no Zephyr mas não usadas neste
código (existem, mas não foram necessárias): `lorawan_set_region()`,
`lorawan_set_channels_mask()`, `lorawan_set_class()`.

## 5. Síntese comparativa

| Aspecto | LoRa | LoRaWAN |
|---|---|---|
| Complexidade de setup | baixa (1 config Kconfig + 1 struct) | maior (região, pilhas, credenciais, join) |
| Precisa de servidor de rede? | não | sim (Network Server) |
| Criptografia | nenhuma | AES/CMAC (via Secure Element em software) |
| Confirmação/retransmissão | manual, se implementado | suportado nativamente (`CONFIRMED`/ADR) |
| Uso neste repositório | código de referência, não chamado | ativo, chamado em `main.c` |
