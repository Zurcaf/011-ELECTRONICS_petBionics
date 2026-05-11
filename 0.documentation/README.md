# petBionic Documentation Hub

Este README centraliza a arquitetura do sistema e aponta para os READMEs especificos por dominio.

## Arquitetura do sistema (visao curta)

O sistema petBionic e composto por 4 blocos:

1. Firmware embarcado (ESP32-C3)
- Aquisiacao de sensores (HX711 + MPU9250/AK8963)
- Controlo BLE (START/STOP, parametros, time sync)
- Logging local em SD por sessao

2. Aplicacao Android
- Descoberta/conexao BLE
- Envio de comandos para o firmware
- Visualizacao de estado operacional
- Historico e detalhe de sessoes

3. Persistencia local no edge
- Ficheiros CSV por dia e por run
- Timestamps relativos e hora local sincronizada

4. Operacao de dados (store-and-forward)
- Coleta local continua
- Sincronizacao posterior quando ha conectividade

## Fluxo principal

1. Android liga ao dispositivo BLE.
2. App envia TIME e comandos de controlo.
3. Firmware inicia sessao e escreve CSV no SD.
4. Sessao termina com STOP e ficheiro fica fechado no cartao.

## READMEs por area

- App Android: `androidApp/README.md`
- Firmware PlatformIO: `firmware/platformio_petBionics/README.md`
- Testes de firmware: `firmware/platformio_petBionics/test/README`
- Hardware/PCB: `hardware/README.md`

## Estado de referencia (2026-04)

- Pipeline de amostragem em microsegundos (80 Hz por default)
- CSV sem `event/score`, com colunas de magnetometro
- Teste dedicado de orientacao IMU em `test/test_imu_orientation`
