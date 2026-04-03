# petBionics PlatformIO Firmware

Firmware do ESP32-C3 para a prótese petBionics, com aquisição de sensores, logging em SD e comunicação BLE via GATT.

## Board

- Board: Seeed Studio XIAO ESP32C3
- Framework: Arduino
- Projeto PlatformIO: `firmware/platformio_petBionics`

## Builds

O projeto tem dois ambientes principais:

- `seeed_xiao_esp32c3` - firmware principal
- `seeed_xiao_esp32c3_diag` - firmware de diagnóstico

Nota: o `platformio.ini` usa `board_build.partitions = huge_app.csv` para suportar BLE + Wi-Fi + sync cloud no mesmo firmware.

### Comandos úteis

```bash
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 -t upload
pio device monitor -e seeed_xiao_esp32c3
```

Para o modo diagnóstico:

```bash
pio run -e seeed_xiao_esp32c3_diag
pio run -e seeed_xiao_esp32c3_diag -t upload
pio device monitor -e seeed_xiao_esp32c3_diag
```

## Pinout

Os pinos definidos em `src/core/Pinout.h` são estes:

| Sinal        | Função             | GPIO   | Pino no XIAO |
| ------------ | ------------------ | ------ | ------------ |
| `kSpiSck`    | SPI SCK            | GPIO21 | D6           |
| `kSpiMiso`   | SPI MISO           | GPIO7  | D5           |
| `kSpiMosi`   | SPI MOSI           | GPIO6  | D4           |
| `kImuCs`     | Chip Select da IMU | GPIO20 | D7           |
| `kSdCs`      | Chip Select do SD  | GPIO11 | D8           |
| `kHx711Dout` | HX711 DOUT         | GPIO10 | D10          |
| `kHx711Sck`  | HX711 SCK          | GPIO9  | D9           |

## Ligações

### Barramento SPI

- SCK -> D6 / GPIO21
- MISO -> D5 / GPIO7
- MOSI -> D4 / GPIO6

### Sensores e armazenamento

- IMU CS -> D7 / GPIO20
- SD CS -> D8 / GPIO11
- HX711 DT -> D10 / GPIO10
- HX711 CLK -> D9 / GPIO9

## BLE

O firmware expõe um serviço BLE custom via GATT para comandos e estado:

- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Control UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Status UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a9`

Comandos suportados:

- `START`
- `STOP`
- `ALPHA=<0..1>`
- `THR=<valor>`
- `PERIOD=<ms>`
- `RATE=<hz>`
- `TIME=<epoch_s_ou_ms>`
- `TIME_SYNC_NOW`
- `WIFI_SSID=<nome_rede>`
- `WIFI_PASS=<password>`
- `WIFI_SAVE`
- `WIFI_CONNECT`
- `SYNC_LAST`
- `SYNC_ALL`
- `SYNC_FILES`

Notas:

- `PERIOD=<ms>` atualiza internamente `samplePeriodUs`.
- `RATE=<hz>` converte para periodo em microssegundos (`1000000/hz`).
- `TIME=` sincroniza o relogio interno usado para timestamps e nomeacao de sessao.
- Firestore e configurado por macros em `src/cloud/FirestoreSync.h`:
	- `PETBIONICS_FIREBASE_PROJECT_ID`
	- `PETBIONICS_FIREBASE_WEB_API_KEY`
- `SYNC_LAST`, `SYNC_ALL` e `SYNC_FILES` enviam todos os CSV pendentes em `/local_files_only`.
- Apos sync com sucesso, cada CSV e movido para `/files_sent_2_DB`, mantendo a pasta do dia.

## Aquisição e logging

- Amostragem com timeline fixa em microssegundos.
- Valor por omissao: `samplePeriodUs = 12500` (80 Hz).
- Sessao SD inicia no `START` e termina no `STOP`.
- Ficheiros CSV pendentes criados em `/local_files_only/<YYYYMMDD>/...`.
- Ficheiros ja enviados ficam em `/files_sent_2_DB/<YYYYMMDD>/...`.

Header CSV atual:

```csv
t_rel_ms,t_rel_us,time_local,load_cell_raw,load_cell_filt,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_mx,imu_my,imu_mz
```

- O CSV nao inclui mais as colunas `event` e `score`.
- O magnetometro (AK8963) e exportado como `imu_mx`, `imu_my`, `imu_mz`.

## Estrutura do projeto

- `src/main.cpp` - firmware principal
- `src/diagnostic_main.cpp` - firmware de diagnóstico
- `src/core` - configuração, tipos e pinout
- `src/pipeline` - pipeline da aplicação
- `src/ble` - controlo BLE
- `src/sensors` - leitura de sensores
- `src/storage` - logging em SD
