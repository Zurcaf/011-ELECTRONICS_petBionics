# Documentação — PetBionicFirmware

Documentação de referência para o firmware do ESP32-C3.

## Conteúdo

- [`hardware/MPU9250REV1.0.pdf`](hardware/MPU9250REV1.0.pdf) — datasheet da IMU MPU-9250.

## Onde está o resto

Este repositório é **só firmware**. Os outros componentes do projeto petBionic
estão versionados em repositórios próprios:

- **App Android** — controlo operacional via BLE
- **Hardware / PCB** — KiCad, gerbers e artefactos de produção
- **Análise de dados** — visualização e processamento das gravações
- **Relatório PIC2** — tese e materiais académicos

## Firmware — referência rápida

- Visão geral, builds, pinout e protocolo BLE: ver o [README principal](../README.md).
- Experimentos isolados: ver [../experiments/README.md](../experiments/README.md).

### Estado de referência (2026-06)

- Pipeline de amostragem em microssegundos (80 Hz por default).
- CSV sem `event/score`, com colunas de magnetómetro (`imu_mx/my/mz`).
- Teste dedicado de orientação IMU em `test/test_imu_orientation`.
