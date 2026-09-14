# LoRaWAN — RAK4631 + MultiTech Conduit (MTCDT-210A)

Documentação da integração LoRaWAN atual: firmware do RAK4631 (Zephyr) enviando
uplinks OTAA para um MultiTech Conduit MTCDT-210A atuando como gateway **e**
Network Server local.

## 1. Arquitetura

```
RAK4631 (nRF52840 + SX1262)  --RF LoRa-->  MTCDT-210A (Conduit)
      firmware Zephyr                       ├─ Packet Forwarder (rádio)
   subsys/lorawan (loramac-node)             └─ LoRa Network Server (mLinux)
                                                    ├─ Key Store (Local/Cloud)
                                                    └─ MQTT broker local (mosquitto)
```

- O **RAK4631** roda a stack LoRaWAN do Zephyr (`subsys/lorawan`, backend
  `loramac-node`) inteiramente on-device: join OTAA, criptografia AES/CMAC
  (Secure Element em software) e envio de uplinks.
- O **Conduit** cumpre dois papéis ao mesmo tempo: é o **gateway** (recebe o
  RF e encaminha via protocolo Semtech UDP) e o **Network Server** (processa
  o Join Request, guarda as chaves, decripta o payload da aplicação).
- Toda mensagem recebida pelo Conduit é republicada em um broker MQTT local
  (`lora/#`), o que serve tanto para integração com aplicações quanto para
  debug.

## 2. Fluxo de comunicação

1. RAK4631 liga → inicializa a stack LoRaWAN → envia **Join Request** (OTAA).
2. Conduit recebe o Join Request via RF e repassa pro Network Server local.
3. Network Server valida o MIC do pacote usando o `AppKey` cadastrado,
   responde com **Join Accept** (RX1/RX2). As chaves de sessão
   (`AppSKey`/`NwkSKey`) são derivadas dos dois lados a partir do `AppKey`.
4. RAK4631 confirma o join e passa a mandar **uplinks não confirmados** a
   cada 10s na porta da aplicação (porta 2), repetindo o payload de teste
   `DE AD BE EF`.
5. Conduit decripta o uplink com a `AppSKey` da sessão e publica o resultado
   no broker MQTT (`lora/<deveui>/up`), já em claro.

## 3. Firmware (RAK4631)

Código relevante: [`source/lorawan/lorawan.c`](../source/lorawan/lorawan.c),
[`source/lorawan/lorawan.h`](../source/lorawan/lorawan.h),
chamado a partir de [`source/main.c`](../source/main.c).

### 3.1 Parâmetros de credenciais (topo de `lorawan.c`)

| Parâmetro | Define | Valor atual | Observação |
|---|---|---|---|
| DevEUI | `LORAWAN_DEV_EUI` | `DD:EE:AA:DD:BB:EE:EE:FF` | identifica o device na rede |
| JoinEUI (AppEUI) | `LORAWAN_JOIN_EUI` | `00:00:00:00:00:00:00:00` | identifica a "aplicação" no Join Server |
| AppKey | `LORAWAN_APP_KEY` | `2B7E...4F3C` (chave de teste FIPS-197) | usada para MIC do join e derivação de sessão (LoRaWAN 1.0.x usa a mesma chave para `app_key` e `nwk_key`) |
| Porta da aplicação | `LORAWAN_PORT` | `2` | porta usada em `lorawan_send()` |
| Intervalo de retry do join | `LORAWAN_JOIN_RETRY_DELAY_S` | `10` s | usado enquanto o join não é aceito |

**Esses três primeiros valores (DevEUI/JoinEUI/AppKey) precisam ser
idênticos ao que está cadastrado no device no Conduit** — qualquer diferença
faz o Network Server descartar o Join Request silenciosamente (sem erro
visível), resultando em timeout indefinido no firmware.

### 3.2 Kconfig (`prj.conf`)

| Config | Valor | Motivo |
|---|---|---|
| `CONFIG_LORA` | `y` | habilita o driver de rádio (SX1262) |
| `CONFIG_LORAWAN` | `y` | habilita o subsistema LoRaWAN do Zephyr |
| `CONFIG_LORAWAN_REGION_US915` | `y` | região/plano de canais — precisa bater com o rádio do gateway |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | `2048` | exigido pelo Secure Element em software (`HAS_SEMTECH_SOFT_SE`) do loramac-node |
| `CONFIG_MAIN_STACK_SIZE` | `2048` | `lorawan_join()`/`lorawan_send()` rodam de forma síncrona na thread `main()`, que executa a stack de cripto do loramac-node |

### 3.3 Fluxo de código

- `lorawan_init()` ([lorawan.c:109](../source/lorawan/lorawan.c#L109)): verifica o rádio, sobe a stack (`lorawan_start()`), registra callbacks de downlink/datarate, e entra em loop de join OTAA — retenta a cada `LORAWAN_JOIN_RETRY_DELAY_S`, incrementando o `DevNonce` a cada tentativa (obrigatório: Network Servers compatíveis rejeitam nonce repetido).
- `lorawan_send_payload()` ([lorawan.c:217](../source/lorawan/lorawan.c#L217)): envia um uplink não confirmado (`LORAWAN_MSG_UNCONFIRMED`) na porta configurada.
- `main()` ([main.c](../source/main.c)): chama `lorawan_init()` uma vez, depois envia o payload de teste a cada 10s em loop infinito.

## 4. Gateway / Network Server (MTCDT-210A)

Acesso via SSH (mLinux) ou painel web (AEP).

### 4.1 Cadastro do device (painel web → Devices → Add Device)

| Campo | Valor |
|---|---|
| Dev EUI | `DD:EE:AA:DD:BB:EE:EE:FF` (igual ao firmware) |
| Device Profile | `LW102-OTA-US915` (LoRaWAN 1.0.2, OTAA, região US915) |
| Network Profile | `DEFAULT-CLASS-A` |
| Serial/Product/HW/FW | opcionais, só inventário |

### 4.2 Key Management (painel web → Key Management)

- **Join Server → Location**: `Local Key Store` (não `Cloud Key Store` — o
  modo cloud delega a validação das chaves ao serviço DeviceHQ da
  MultiTech, exigindo internet e cadastro lá; para operação standalone,
  local é o correto).
- No device cadastrado, configurar:
  - **App Key**: `2B7E151628AED2A6ABF7158809CF4F3C`
  - **Join EUI / App EUI**: `0000000000000000`

### 4.3 Região / canais

O rádio do Conduit está operando em **US915**, sub-banda observada nos logs
como FSB1 (canais 902.3–903.7 MHz). Não foi necessário restringir o
`channel mask` no firmware (`lorawan_set_channels_mask()`) — o join e os
uplinks já chegam nessa faixa sem configuração adicional.

## 5. Verificação / debug

**No firmware (console serial USB, `/dev/ttyACMx`, 1500000 8N1):**
```
[inf] icc: Joining LoRaWAN network using OTAA (attempt N)...
[inf] icc: LoRaWAN initialized and joined successfully.
[inf] icc: LoRaWAN uplink sent successfully.
[inf] icc: LoRaWAN downlink received: port=0, RSSI=... dBm, SNR=... dB
```

**No gateway (SSH, mLinux), via MQTT local:**
```bash
mosquitto_sub -v -t 'lora/#'
```
Tópicos relevantes:
- `lora/<deveui>/join_request` — join recebido pelo rádio (RF ok)
- `lora/<deveui>/packet_recv` — uplink bruto recebido (ainda cifrado)
- `lora/<deveui>/up` — uplink **decodificado**: `data` já vem em claro
  (base64). Ex.: `data:"3q2+7w=="` → `DE AD BE EF`, confirmando que a sessão
  e a decriptação estão corretas.
- `lora/<deveui>/packet_missed` — contagem de uplinks perdidos no ar (RF,
  não é erro de firmware).

## 6. Pontos a revisar antes de produção

- `AppKey`/`DevEUI` atuais são placeholders de teste — gerar credenciais
  reais por device antes de ir a campo.
- `DevNonce` é reiniciado do zero a cada boot (não persiste em NVM). Após um
  join bem-sucedido, se o device resetar e tentar reconectar reusando um
  nonce já visto pelo Network Server, o join será rejeitado até o contador
  avançar de novo — hoje mitigado pelo retry automático, mas o ideal é
  persistir o `DevNonce` em flash.
- `source/lora/lora.c` (driver LoRa "puro", pré-LoRaWAN) ainda está no
  build ([`CMakeLists.txt`](../CMakeLists.txt)) mas não é mais usado —
  candidato a remoção.
