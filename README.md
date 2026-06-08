# PetBionicFirmware

Firmware embarcado do ESP32-C3 para a prótese canina instrumentada **petBionic**:
aquisição de sensores, logging local em cartão SD por sessão e controlo/estado via BLE.

> Este repositório contém **apenas o firmware**. Os restantes componentes do
> projeto vivem em repositórios próprios (app Android, hardware/PCB, análise de
> dados, relatório PIC2).

## Hardware

- Board: **Seeed Studio XIAO ESP32-C3**
- Framework: Arduino (via PlatformIO)
- Sensores: célula de carga **HX711**, IMU **MPU-9250** (+ magnetómetro AK8963)
- Armazenamento: cartão **SD** via SPI

## Estrutura do repositório

```
PetBionicFirmware/
├── platformio.ini          — configuração de build (raiz do projeto PlatformIO)
├── secrets.ini.template    — modelo de credenciais WiFi (copiar para secrets.ini)
├── src/                    — firmware principal
│   ├── core/               — configuração, tipos, pinout, relógio local
│   ├── pipeline/           — pipeline da aplicação (filtros, orientação, eventos)
│   ├── sensors/            — leitura de sensores (RawSensor)
│   ├── storage/            — logging em SD (RawSdLogger)
│   ├── wifi/               — gestão de WiFi
│   ├── web/                — interface web de diagnóstico
│   ├── main.cpp            — entry point do firmware principal
│   └── diagnostic_main.cpp — entry point do firmware de diagnóstico
├── include/                — headers partilhados
├── lib/                    — bibliotecas locais
├── test/                   — testes de bancada e rotinas de calibração
├── experiments/            — sketches Arduino isolados (sensores, BLE, WiFi, SD, deep-sleep)
└── docs/                   — datasheets, diagramas e documentação de referência
```

## Builds

Ambientes definidos em [platformio.ini](platformio.ini):

| Ambiente                    | Descrição                          |
| --------------------------- | ---------------------------------- |
| `seeed_xiao_esp32c3`        | firmware principal                 |
| `seeed_xiao_esp32c3_diag`   | firmware de diagnóstico            |
| `imu_orientation_debug`     | teste de orientação do IMU         |

### Quick start

```bash
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 -t upload
pio device monitor -e seeed_xiao_esp32c3
```

### Credenciais WiFi

`platformio.ini` carrega `secrets.ini` via `extra_configs`. Este ficheiro é
**gitignored** — copia o modelo e preenche os teus valores:

```bash
cp secrets.ini.template secrets.ini
# editar secrets.ini com SSID/PASSWORD reais
```

## Pinout (ESP32-C3 / XIAO)

| Sinal        | Função             | GPIO   | Pino XIAO |
| ------------ | ------------------ | ------ | --------- |
| `kSpiSck`    | SPI SCK            | GPIO21 | D6        |
| `kSpiMiso`   | SPI MISO           | GPIO7  | D5        |
| `kSpiMosi`   | SPI MOSI           | GPIO6  | D4        |
| `kImuCs`     | Chip Select da IMU | GPIO20 | D7        |
| `kSdCs`      | Chip Select do SD  | GPIO11 | D8        |
| `kHx711Dout` | HX711 DOUT         | GPIO10 | D10       |
| `kHx711Sck`  | HX711 SCK          | GPIO9  | D9        |

Definições em [src/core/Pinout.h](src/core/Pinout.h).

## Interface BLE

Serviço GATT custom para comandos e estado:

- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Control UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Status UUID:  `beb5483e-36e1-4688-b7f5-ea07361b26a9`

Comandos suportados: `START`, `STOP`, `ALPHA=<0..1>`, `THR=<valor>`,
`PERIOD=<ms>`, `RATE=<hz>`, `TIME=<epoch_s_ou_ms>`, `TIME_SYNC_NOW`.

- `PERIOD=<ms>` atualiza internamente `samplePeriodUs`.
- `RATE=<hz>` converte para período em microssegundos (`1000000/hz`).
- `TIME=` sincroniza o relógio interno usado para timestamps e nomeação de sessão.

## Aquisição e logging

- Amostragem com timeline fixa em microssegundos (default `samplePeriodUs = 12500`, **80 Hz**).
- A sessão SD inicia no `START` e termina no `STOP`.
- Ficheiros CSV por dia/run (ex.: `/20260403/raw_log_20260403_run001_142210_123.csv`).

Header CSV atual:

```csv
t_rel_ms,t_rel_us,time_local,load_cell_raw,load_cell_filt,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_mx,imu_my,imu_mz
```

## Experimentos

Sketches Arduino isolados para testar sensores e conectividade individualmente
estão em [experiments/](experiments/) — ver [experiments/README.md](experiments/README.md).
