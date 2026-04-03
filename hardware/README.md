# petBionic Hardware

Documentacao de hardware e fabricacao da plataforma petBionic.

## Estrutura

- `hardware/pcb/kicad`
  - Projeto fonte de PCB (KiCad)

- `hardware/pcb/jlcpcb/gerber`
  - Gerbers para fabricacao

- `hardware/pcb/jlcpcb/production_files`
  - Pacote de producao para envio

- `hardware/pcb/production`
  - Artefactos de producao e revisoes

- `hardware/pcb/backups`
  - Backups de trabalho

## Bill of Materials (resumo funcional)

- Seeed Studio XIAO ESP32-C3
- MPU9250 (IMU 9-axis)
- HX711 + celula de carga
- Modulo/cartao SD

## Pinout funcional esperado

- SPI SCK -> D6 (GPIO21)
- SPI MISO -> D5 (GPIO7)
- SPI MOSI -> D4 (GPIO6)
- IMU CS -> D7 (GPIO20)
- SD CS -> D8 (GPIO11)
- HX711 DT -> D10 (GPIO10)
- HX711 CLK -> D9 (GPIO9)

## Fluxo recomendado de revisao

1. Editar no KiCad.
2. Gerar Gerbers e atualizar pasta de producao.
3. Validar pinout contra `firmware/platformio_petBionics/src/core/Pinout.h`.
4. Executar testes de hardware em `firmware/platformio_petBionics/test` apos montagem.

## Integracao com firmware

Qualquer alteracao de pinout no hardware deve ser refletida primeiro em:
- `firmware/platformio_petBionics/src/core/Pinout.h`

Depois disso, validar:
- comunicacao IMU
- escrita no SD
- leitura HX711
