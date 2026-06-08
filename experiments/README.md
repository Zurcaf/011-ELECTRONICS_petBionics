# petBionic — Arduino Experimental Sketches

Testes isolados e calibração de sensores / conectividade para o ESP32-C3.

---

## Estrutura

```
arduino-experiments/
├── sensors/                 — testes de sensores individuais
│   ├── teste_loadcell/      — HX711 load cell isolado
│   ├── teste_IMU_load/      — MPU-9250 + HX711 simultâneos
│   ├── teste_led/           — LED de diagnóstico
│   └── imu_mounting_cal/    — 🆕 calibração de orientação do IMU
├── connectivity/            — testes de rede e armazenamento
│   ├── teste_ble/           — BLE (Bluetooth Low Energy)
│   ├── teste_wifi/          — WiFi + NTP time sync
│   └── teste_sdcard/        — SD card via SPI
└── deep-sleep/              — gestão de energia
    ├── main_deep_sleep/     — versão estável
    ├── main_deep_sleep_v1/  — iteração 1
    └── main_deep_sleep_v2/  — iteração 2
└── timestamp-rel/           — sincronização de tempo
    ├── main_timestamp_rel/  — versão estável
    ├── main_timestamp_rel_v1/ → v5/ — iterações experimentais
```

---

## 📡 Sensores

### 🔋 `teste_loadcell/` — HX711 Load Cell

Testa a célula de carga isoladamente.

| Pino | Função |
|------|--------|
| D10  | HX711 Data (DT) |
| D9   | HX711 Clock (SCK) |

```bash
# Flash e monitoriza
platformio run -t upload -e seeed_xiao_esp32c3 -d experiments/sensors/teste_loadcell/
```

**Output esperado:**
```
=== HX711 Load Cell Test ===
Checking if HX711 is ready...
HX711 is ready!
Reading raw values (no calibration)...
Raw ADC: -13918    <- valor típico sem carga
Raw ADC: -13917
...
```

**Nota:** Não aplica calibração; lê raw ADC directamente. Para calibrar:

```
1. Coloca scale.tare() no setup
2. Adiciona peso conhecido
3. Calcula factor = (delta_raw) / peso_kg
```

---

### 🎛️ `teste_IMU_load/` — IMU (MPU-9250) + Load Cell

Lê **simultânea e independentemente** acelerómetro (I2C master, magnetómetro AK8963), giroscópio e load cell via HX711.

| Pino | Função |
|------|--------|
| D6   | SPI Clock (SCLK) |
| D5   | SPI MISO |
| D4   | SPI MOSI |
| D7   | CS IMU |
| D10  | HX711 Data (DT) |
| D9   | HX711 Clock (SCK) |

**Teste de integração:** verifica que ambos os sensores funcionam no mesmo loop sem bloqueios mútuous.

---

### 💡 `teste_led/` — Diagnóstico visual

LED output para sinalizar estado (power-on, erro, etc).

---

### 🧭 `imu_mounting_cal/` — Calibração de Montagem do IMU (novo)

Determina a matriz de rotação 3×3 que alinha o frame do sensor com o frame anatómico da prótese.

**Ver [imu_mounting_cal.md](./sensors/imu_mounting_cal/README.md) para instruções completas.**

Resumo:
1. Coloca a prótese em apoio neutro (encaixe para cima, pé para baixo)
2. Envia `c` → regista 2 s de acelerómetro + magnetómetro
3. Envia `s` → calcula matriz R e guarda na NVS
4. Envia `v` → monitoriza accel antes/depois da rotação

Resultado: 9 floats em NVS namespace `imu_cal` (3×3 matriz).

---

## 🌐 Conectividade

### 📱 `teste_ble/` — Bluetooth Low Energy

Cria um servidor BLE com notificação periódica.

```
Service UUID:        6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Characteristic UUID: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

**Usa:**
- Callback de conexão (rastreia `deviceConnected`)
- Callback de subscrição (rastreia `bleSubscribed`)
- Notificações periódicas enquanto conectado

---

### 📶 `teste_wifi/` — WiFi + NTP

Conecta a rede WiFi e sincroniza hora via NTP.

```
SSID:     "composites"
Password: "Composites2019"
NTP Pool: pool.ntp.org
```

**Usa:** `WiFi.localIP()`, `time()` com `struct tm` para time zone.

---

### 💾 `teste_sdcard/` — SD Card via SPI

Escreve e lê ficheiros no cartão SD.

| Pino | Função |
|------|--------|
| D6   | SPI Clock |
| D8   | SD Chip Select |
| D10  | SPI MISO |
| D4   | SPI MOSI |

```
SD pinout: SCK=D9, MISO=D10, MOSI=D8, CS=D6
```

---

## 😴 Power Management

### `deep-sleep/` — Hibernação com Wake-up

Reduz consumo em modo stand-by. Existem 3 versões com progressão experimental.

**Versão recomendada:** `main_deep_sleep/` (mais estável).

Tipicamente:
- Setup de pinos e periféricos
- Lê sensores
- Escreve dados em SD
- Entra em `esp_deep_sleep_start()` com wake-up timer

---

## ⏱️ Timestamp Management

### `timestamp-rel/` — Sincronização de Tempo Relativo

Varias abordagens para manter o tempo consistente entre leituras de sensores.

| Versão | Abordagem |
|--------|-----------|
| `main_timestamp_rel/` | Estável — uses `micros()` local |
| `v1`, `v2`, `v3` | Experimental |
| `v4` | WiFiUDP + NTPClient (hora global) |
| `v5` | Iteração final |

**Nota:** A versão v4 é a única que traz NTP de verdade (`WiFiUdp + NTPClient`).

---

## 📋 Checklist de Debugging

| Problema | Verificação |
|----------|-------------|
| SPI não responde | Pinos SCK/MISO/MOSI corretos? CS alto por default? |
| HX711 timeout | DT=D10, SCK=D9 ligados? Alimentação 3.3V? |
| IMU WHO_AM_I=0x00 | SPI MODE3 + 1 MHz clock? |
| BLE não detecta | `BLEDevice::init()` antes de criar server? |
| WiFi timeout | SSID/password corretos? 2.4 GHz? |
| SD card erro | FAT32 formatted? Pinos SPI alinhados? |

---

## 🔗 Pinout Reference (ESP32-C3)

```
D0  = GPIO0  (bootstrap pin — cuidado ao usar)
D1  = GPIO1
D2  = GPIO2
D3  = GPIO3  (bootstrap pin — cuidado)
D4  = GPIO4  (SPI MOSI)
D5  = GPIO5  (SPI MISO)
D6  = GPIO6  (SPI SCK)
D7  = GPIO7  (CS IMU)
D8  = GPIO8  (CS SD)
D9  = GPIO9  (HX711 SCK)
D10 = GPIO10 (HX711 DT)

A0 = GPIO0  (ADC — battery voltage monitor)
A1 = GPIO1
...
A5 = GPIO5
```

---

## 🚀 Quick Start

```bash
# Coloca-te na pasta do sketch que queres testar
cd experiments/sensors/teste_loadcell/

# Flash + Serial Monitor (115200 baud)
platformio run -t upload -e seeed_xiao_esp32c3

# Ou manualmente:
pio device monitor -b 115200
```

---

## 📚 Leitura Complementar

- `../src/sensors/RawSensor.cpp` — implementação final de sensores no firmware principal
- `./sensors/imu_mounting_cal/README.md` — calibração de montagem do IMU em detalhe
