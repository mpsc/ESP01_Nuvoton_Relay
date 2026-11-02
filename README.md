# ESP-01 Nuvoton Relay Shield

Firmware para ESP8266-01 (ESP-01/ESP-01S) com shield de rele Nuvoton, controlando 2 reles via serial (Nuvoton MCU), com interface web, MQTT e auto-descoberta Home Assistant.

## Funcionalidades

- **2 Reles** controlaveis via web e MQTT
- **Nuvoton Shield** - Reles controlados via serial (Nuvoton MCU)
- **Portal de configuracao** proprio (sem WiFiManager)
- **MQTT + HA Discovery** - Auto-descoberta no Home Assistant
- **Interface web** - Pagina HTML para controle dos 2 reles
- **EEPROM** - Persistencia de configuracao (WiFi + MQTT)
- **LWT** - Last Will & Testament (disponibilidade)

## Hardware

### ESP-01 + Nuvoton Relay Shield

| Componente | Especificacao |
|-----------|---------------|
| **Placa** | ESP8266-01 / ESP-01S (512KB ou 1MB) |
| **Shield** | Nuvoton Relay Shield (2 reles) |
| **Rele 1** | Controlado via serial (Nuvoton) |
| **Rele 2** | Controlado via serial (Nuvoton) |
| **Serial TX** | GPIO1 (TX -> Nuvoton RX) |
| **Serial RX** | GPIO3 (liberado, SERIAL_TX_ONLY) |
| **Power** | 5V (shield tem regulador 3.3V para ESP-01) |

### Nuvoton Shield - Especificacoes

| Item | Valor |
|------|-------|
| **Microcontrolador interno** | Nuvoton (controla os reles) |
| **Reles** | 2 (active-LOW via Nuvoton) |
| **Baud rate** | 115200 |
| **Comandos** | A0 01 00 A1 (rele 1 OFF), A0 01 01 A2 (rele 1 ON) |
| **Power input** | 5V |
| **LED** | D6 pisca quando comandos funcionam |
| **GPIO usado pelo ESP-01** | Apenas TX (GPIO1) |

### Por que Nuvoton e diferente?

O shield Nuvoton **nao usa GPIOs do ESP-01 diretamente**. Ele tem um microcontrolador Nuvoton interno que controla os reles. A comunicacao e feita via serial:

1. ESP-01 envia string de inicializacao via TX
2. Nuvoton aguarda ~10 segundos
3. Apos inicializacao, ESP-01 envia comandos hex via TX
4. Nuvoton executa os comandos e controla os reles

### Inicializacao do Nuvoton

```
1. ESP-01 envia: "WIFI CONNECTED\nWIFI GOT IP\nAT+CIPMUX=1\nAT+CIPSERVER=1,8080\nAT+CIPSTO=360\n"
2. Aguarda ~10 segundos
3. LED D6 comeca a piscar
4. Comandos de rele passam a funcionar
```

### Comandos do Nuvoton

| Comando (hex) | Acao |
|---------------|------|
| `A0 01 00 A1` | Rele 1 OFF |
| `A0 01 01 A2` | Rele 1 ON |
| `A0 02 00 A2` | Rele 2 OFF |
| `A0 02 01 A3` | Rele 2 ON |

## Bibliotecas Necessarias

Apenas **PubSubClient** (v2.8+). Nenhuma outra biblioteca externa.

## Configuracao

### Portal de Configuracao
1. Na primeira inicializacao (ou apos reset), o ESP cria um AP: `ESP01_Config`
2. Conecte-se ao AP e acesse `http://192.168.4.1/`
3. Configure:
   - WiFi SSID e senha
   - MQTT servidor, porta, usuario e senha
4. Clique em "SALVAR E REINICIAR"

### Topicos MQTT (fixos)

| Topico | Descricao |
|--------|-----------|
| `esp01/nuvoton/relay1/set` | Comando Rele 1 (ON/OFF) |
| `esp01/nuvoton/relay1/state` | Estado Rele 1 (ON/OFF) |
| `esp01/nuvoton/relay2/set` | Comando Rele 2 (ON/OFF) |
| `esp01/nuvoton/relay2/state` | Estado Rele 2 (ON/OFF) |
| `esp01/nuvoton/availability` | Disponibilidade (online/offline) |

### Home Assistant Discovery

| Topico de Discovery | Entidade |
|---------------------|----------|
| `homeassistant/switch/ESP01_Nuvoton_R1/config` | Switch Rele 1 |
| `homeassistant/switch/ESP01_Nuvoton_R2/config` | Switch Rele 2 |

## Interface Web

Acesso: `http://<IP_DO_ESP>/`

- Status dos reles (LIGADO/DESLIGADO)
- Botoes LIGAR/DESLIGAR para cada rele
- Link para apagar configuracao e reiniciar

## Compilacao

### Arduino IDE
1. Instale a biblioteca PubSubClient (v2.8+)
2. Selecione a placa: **Generic ESP8266 Module**
3. Flash Size: **1MB (FS:256KB OTA:~470KB)** ou **512KB (FS:64KB OTA:~230KB)**
4. Upload Speed: 115200
5. Compile e grave

### PlatformIO
```ini
[env:esp01_1m]
platform = espressif8266@4.2.1
board = esp01_1m
framework = arduino
lib_deps = knolleary/PubSubClient@^2.8
build_flags =
    -D MQTT_MAX_PACKET_SIZE=512
    -D MQTT_KEEPALIVE=30
```

## Upload (Gravacao)

O ESP-01 nao suporta OTA pela web. A gravacao e feita pela serial:

1. Conecte o ESP-01 ao programador (GPIO0 -> GND para entrar em modo flash)
2. Execute o upload
3. Desconecte GPIO0 de GND
4. Reinicie o dispositivo

## EEPROM

A EEPROM e emulada em 512 bytes. A configuracao ocupa os primeiros bytes.

### Estrutura da Configuracao
```c
struct Config {
  uint8_t  magic;          // 0xA5 (validacao)
  char     ssid[33];       // WiFi SSID
  char     pass[65];       // WiFi senha
  char     mqttServer[41]; // MQTT servidor
  uint16_t mqttPort;       // MQTT porta (padrao: 1883)
  char     mqttUser[21];   // MQTT usuario
  char     mqttPass[21];   // MQTT senha
};
```

## Versoes

- **v1.0**: Versao inicial com Nuvoton Shield (2 reles via serial)

## Licenca

MIT - DIY