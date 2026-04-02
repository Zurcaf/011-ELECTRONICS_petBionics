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

| Sinal | Função | GPIO | Pino no XIAO |
| --- | --- | --- | --- |
| `kSpiSck` | SPI SCK | GPIO21 | D6 |
| `kSpiMiso` | SPI MISO | GPIO7 | D5 |
| `kSpiMosi` | SPI MOSI | GPIO6 | D4 |
| `kImuCs` | Chip Select da IMU | GPIO20 | D7 |
| `kSdCs` | Chip Select do SD | GPIO11 | D8 |
| `kHx711Dout` | HX711 DOUT | GPIO10 | D10 |
| `kHx711Sck` | HX711 SCK | GPIO9 | D9 |

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

- Service UUID: `14f16000-9d9c-470f-9f6a-6e6fe401a001`
- Control UUID: `14f16001-9d9c-470f-9f6a-6e6fe401a001`
- Status UUID: `14f16002-9d9c-470f-9f6a-6e6fe401a001`

Comandos suportados:

- `START`
- `STOP`
- `ALPHA=<0..1>`
- `THR=<valor>`
- `PERIOD=<ms>`
- `TIME=<epoch_s_ou_ms>`
- `TIME_SYNC_NOW`

## Estrutura do projeto

- `src/main.cpp` - firmware principal
- `src/diagnostic_main.cpp` - firmware de diagnóstico
- `src/core` - configuração, tipos e pinout
- `src/pipeline` - pipeline da aplicação
- `src/ble` - controlo BLE
- `src/sensors` - leitura de sensores
- `src/storage` - logging em SD
