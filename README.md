# petBionic

Projeto de protese canina instrumentada com arquitetura IoT baseada em edge logging + controlo BLE.

## Resumo

O sistema petBionic combina:

- Firmware embarcado em ESP32-C3 para aquisicao de sensores
- App Android para controlo operacional via BLE
- Logging local em SD por sessao (modelo store-and-forward)
- Estrutura de hardware com PCB propria (KiCad + ficheiros de producao)

## Estado atual (2026-04)

- App e firmware BLE alinhados no mesmo contrato de comandos/estado
- Time sync por `TIME=<epoch_ms>` com retry automatico na app
- Pipeline de amostragem com timeline fixa em microsegundos (80 Hz por default)
- CSV por sessao, organizado por dia/run no SD
- CSV sem `event/score` e com eixos de magnetometro (`imu_mx/my/mz`)
- Teste dedicado de orientacao IMU em `test/test_imu_orientation`

## Arquitetura de alto nivel

1. Edge device (ESP32-C3)
- Le HX711 e MPU9250/AK8963
- Exponibiliza controlo e status por BLE
- Escreve dados em CSV no cartao SD por sessao

2. App Android
- Descobre e conecta por BLE
- Envia comandos de operacao (START/STOP/TIME/PERIOD/RATE)
- Mostra estado de aquisicao e saude de sensores

3. Persistencia local
- Ficheiros de sessao por dia e numero de run
- Timestamps relativos e hora local sincronizada

## Estrutura do repositorio

- `androidApp/` - aplicacao Android
- `firmware/platformio_petBionics/` - firmware principal PlatformIO
- `hardware/` - PCB, gerbers e artefactos de producao
- `docs/` - documentacao central e guias por dominio

## Documentacao recomendada

- Arquitetura central: `docs/README.md`
- App Android: `androidApp/README.md`
- Hardware/PCB: `hardware/README.md`
- Firmware PlatformIO: `firmware/platformio_petBionics/README.md`
- Testes de firmware: `firmware/platformio_petBionics/test/README`

## Quick start

Firmware (na pasta `firmware/platformio_petBionics`):

```bash
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 -t upload
pio device monitor -e seeed_xiao_esp32c3
```

App Android (na pasta `androidApp`):

```bash
./gradlew assembleDebug
```
