# 🐾 IoT Smart Canine Prosthesis

Projeto desenvolvido no âmbito da disciplina de **Redes Móveis e Internet das Coisas**.

## 📌 Descrição

Este projeto consiste no desenvolvimento de uma **prótese canina instrumentada com sensores externos ao animal**, integrada numa arquitetura IoT com comunicação **BLE** e **Wi-Fi**.

O sistema permite:

- Recolher dados de carga exercida no solo
- Medir orientação e ângulo da prótese
- Armazenar dados localmente (SD Card)
- Comunicar via BLE com aplicação Android (GUI)
- Sincronizar dados via Wi-Fi para base de dados remota utilizando MQTT

O sistema segue um modelo **Store-and-Forward**, onde os dados são guardados localmente e enviados posteriormente quando existir ligação Wi-Fi.

### Estado atual (2026-04)

- BLE app e firmware alinhados no mesmo nome/servico/caracteristicas
- Sincronizacao horaria via comando `TIME=<epoch_ms>` com retry automatico no Android
- Logging em ficheiros por sessao no SD, organizados por dia e run
- Pipeline em 80 Hz com timeline fixa em microssegundos (`12500 us`)
- CSV simplificado: sem `event/score`, com acelerometro, giroscopio e magnetometro
- Teste dedicado de orientacao IMU (roll/pitch/yaw) em `test/test_imu_orientation`

---

# 🏗 Arquitetura do Sistema

## 📟 Edge Device (Prótese)

Hardware principal:

- Microcontrolador: Seeed Studio XIAO ESP32-C3
- Sensor inercial: MPU-9250
- Sensor de carga: HX711
- Módulo SD Card

Funções:

- Aquisição de dados
- Processamento básico
- Armazenamento local
- Comunicação BLE (controlo)
- Comunicação Wi-Fi (sincronização)

---

## 🔧 Especificações dos Componentes

### Microcontrolador - ESP32-C3

- Processador: RISC-V 160MHz
- Memória RAM: 320KB
- Flash: 4MB
- Comunicação: SPI, I2C, UART, BLE 5.0, Wi-Fi 802.11b/g/n

### Sensor Inercial - MPU9250 (9-axis IMU)

- Comunicação: SPI @1MHz
- Saída:
  - Acelerómetro 3-axis (XYZ)
  - Giroscópio 3-axis (XYZ)
  - Magnetómetro 3-axis (XYZ) via AK8963
- Taxa de amostragem máxima (IMU-only): **~6075 Hz** (intervalo ~165 μs)

### Sensor de Carga - HX711

- Comunicação: Pinos digitais (DT/CLK)
- Resolução: 24-bit ADC
- Modos de operação:
  - **Modo A (80 SPS)**: Menor filtragem digital, maior velocidade
  - **Modo B (10 SPS)**: Maior filtragem digital integrada, maior estabilidade
- Taxa máxima sincronizada (IMU+HX711): **~83-84 Hz** (limitado pelo HX711)
- Nota: HX711 é o gargalo em amostragem sincronizada; IMU consegue ~6 kHz sozinha

### Cartão SD

- Comunicação: SPI
- Função: Logging contínuo de dados em CSV
- Armazenamento: Dados de sensores e logs de operação

#### Estratégia de Escrita - Buffering em Memória

**Problema:** Escrever linha-a-linha no SD Card bloqueia as leituras dos sensores (~50-100ms por operação).

**Taxa de amostragem requerida:**

- IMU: 150 Hz (1 amostra a cada ~6.67ms)
- HX711: 80 Hz (1 amostra a cada 12.5ms)
- **Total de eventos: ~230/segundo**

**Solução implementada: Buffer circular de 2048 bytes**

```
Tamanho linha CSV (raw only): ~45 bytes/linha
2048 bytes buffer: ~45 linhas (~200ms de dados)

Estratégia:
1. Acumular dados em buffer de RAM (sem I/O)
2. Quando buffer atinge 2048 bytes, escrever TUDO de uma vez no SD
3. Tempo de flush medido para 2048 B: ~8.3ms (não bloqueia sensores)
4. Depois limpar o buffer e continuar

Benefício: Reduz ciclos de escrita em 20-50x
```

**Performance medida (teste real em 2026-03-26):**

Velocidade de escrita por tamanho de bloco (KB/s):

- 128 B: 209.15
- 256 B: 232.21
- 512 B: 273.11
- 1024 B: 337.22
- 2048 B: 384.42
- 4096 B: 415.08

Overhead de operações de ficheiro (μs):

- Open file: 28905
- Close file: 2035
- Open + write 1 byte + close: 42341
- Open + write 100 bytes + close: 42626

Tempo de flush vs open/close por tamanho de buffer (μs):

- 256 B: flush 3817 | open/write/close 14078
- 512 B: flush 3877 | open/write/close 14248
- 1024 B: flush 5232 | open/write/close 16094
- 2048 B: flush 8302 | open/write/close 18831
- 4096 B: flush 11698 | open/write/close 22324

Resultado adicional (teste buffered end-to-end, 2026-03-26):

- Teste: `test_buffered_sdcard_logging`
- Flush observado: 1600 B em 19618 μs (79.65 KB/s)
- Total escrito no ficheiro: 1635 B
- Estado: PASS (1 teste, 0 falhas)

Conclusões práticas:

- Escrever linha-a-linha é caro (open/close domina latência).
- Buffering é obrigatório para manter 150 Hz (IMU) + 80 Hz (HX711).
- 2048 B é um bom compromisso entre latência e eficiência.
- Registar apenas RAW reduz CPU e simplifica pós-processamento.

**Formato de LOG atual (sessao):**

```csv
t_rel_ms,t_rel_us,time_local,load_cell_raw,load_cell_filt,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_mx,imu_my,imu_mz
0,0,14:22:10.123,388125,388125.000,-340,-123,567,12,-7,18,134,-220,89
12,12500,14:22:10.135,388190,388138.000,-338,-120,571,10,-8,20,136,-219,92
```

- `t_rel_ms` e `t_rel_us`: tempo relativo da sessao
- `time_local`: hora local baseada no relogio sincronizado por BLE
- `load_cell_raw` e `load_cell_filt`: carga bruta e filtrada
- `imu_a*`, `imu_g*`, `imu_m*`: acelerometro, giroscopio e magnetometro (raw)

### Mapeamento de Pinos

```
SPI:
  - SCK  -> D6 (GPIO21)
  - MISO -> D5 (GPIO7)
  - MOSI -> D4 (GPIO6)

Sensores:
  - IMU CS    -> D7 (GPIO20)
  - SD CS     -> D8 (GPIO11)
  - HX711 DT  -> D10 (GPIO10)
  - HX711 CLK -> D9 (GPIO9)
```

---

## 📱 Aplicação Android

Responsável por:

- Conectar via BLE
- Enviar comandos (Start/Stop testes)
- Configurar parâmetros
- Configurar rede Wi-Fi
- Visualizar estado do sistema
- Interface gráfica (GUI)

A aplicação não recebe dados continuamente — apenas envia comandos e consulta estado.

---

## 🌐 Comunicação Wi-Fi

O ESP32 liga-se à rede Wi-Fi configurada e envia os dados armazenados para um broker MQTT.

### Protocolo Utilizado:

- MQTT

### Modelo:

- ESP32 → MQTT Client (Publisher)
- Broker → Recebe dados
- Backend → Armazena em base de dados

---

# 🔄 Fluxo de Funcionamento

### Durante o dia:

Sensores → ESP32 → SD Card

### Quando houver Wi-Fi disponível:

ESP32 → Wi-Fi → MQTT Broker → Base de Dados

### Comunicação BLE:

App Android ↔ ESP32  
(Comandos e configuração apenas)

---

# 📡 Comunicações

## BLE

- ESP32 atua como BLE Peripheral
- Android atua como BLE Central
- Implementação via GATT (Custom Service)

Funções BLE:

- Start/Stop aquisição
- Configuração de parâmetros
- Configuração de Wi-Fi
- Consulta de estado (bateria, memória)

---

## Wi-Fi + MQTT

- Envio em batch
- QoS configurável
- Reconexão automática
- Modelo assíncrono

Exemplo de tópicos:

prostese/ID01/dados  
prostese/ID01/status  
prostese/ID01/sync

---

# 🧠 Estrutura do Firmware (ESP32)

/src
├── main.cpp
├── SensorManager
├── StorageManager
├── BLEManager
├── WiFiManager
├── MQTTManager
├── SyncManager
└── ConfigManager

### Módulos

- **SensorManager** → Leitura de sensores
- **StorageManager** → Gestão do SD
- **BLEManager** → Serviço GATT
- **WiFiManager** → Ligação à rede
- **MQTTManager** → Comunicação MQTT
- **SyncManager** → Envio de dados armazenados
- **ConfigManager** → Parâmetros e credenciais

---

# 👥 Divisão do Trabalho

## Elemento 1 – Firmware

- Implementação sensores
- Armazenamento SD
- BLE GATT Server
- Wi-Fi + MQTT
- Sincronização

## Elemento 2 – Aplicação Android + Backend

- BLE Client
- GUI
- Envio de comandos
- Broker MQTT
- Base de dados
- Backend de receção

---

# 🎯 Objetivos do Projeto

- Implementar comunicação BLE bidirecional
- Implementar sincronização Wi-Fi com MQTT
- Desenvolver arquitetura IoT modular
- Aplicar modelo Store-and-Forward
- Garantir eficiência energética
- Desenvolver aplicação Android com GUI funcional

---

# 📊 Conceitos Aplicados

- IoT Architecture (Perception / Network / Application)
- Edge Computing
- Bluetooth Low Energy (GATT)
- MQTT
- Store-and-Forward Model
- Comunicação assíncrona
- Gestão de energia em dispositivos IoT

---

# 🚀 Estado do Projeto

🔲 Planeamento  
🔲 Implementação Firmware  
🔲 Implementação BLE  
🔲 Implementação MQTT  
🔲 Aplicação Android  
🔲 Testes Integrados

---

# 📚 Disciplina

Redes Móveis e Internet das Coisas  
Licenciatura/Engenharia

---

# 🐶 Objetivo Final

Permitir a recolha e análise de dados biomecânicos para avaliar a adaptação do cão à prótese, através de uma solução IoT eficiente, modular e escalável.

---

# 🗂 Plano de Reestruturação Profissional

## 🎯 Objetivo

Organizar o repositório para separar claramente:

- Firmware de produção
- Experiências e protótipos
- Hardware (PCB, fabricação e artefactos)
- Documentação técnica
- Arquivo histórico

---

## 🌳 Estrutura Alvo

```text
petBionic/
├── README.md
├── firmware/
│   ├── platformio/
│   │   └── petbionics/
│   └── arduino-experiments/
│       ├── deep-sleep/
│       ├── timestamp-rel/
│       ├── connectivity/
│       └── sensors/
├── hardware/
│   └── pcb/
│       ├── kicad/
│       ├── production/
│       ├── jlcpcb/
│       └── backups/
├── docs/
│   ├── architecture/
│   ├── firmware/
│   ├── hardware/
│   └── operations/
├── tools/
│   └── scripts/
└── archive/
```

---

## 🔁 Mapeamento Atual → Novo

### Firmware principal

- `platformIO/PetBionics` → `firmware/platformio/petbionics`

### Sketches Arduino (experiências)

- `arduinoIDE/main` → `firmware/arduino-experiments/sensors/main`
- `arduinoIDE/main1` → `archive/arduinoIDE/main1`
- `arduinoIDE/mainPet` → `archive/arduinoIDE/mainPet`
- `arduinoIDE/new_main` → `archive/arduinoIDE/new_main`
- `arduinoIDE/new_main_version_1` → `archive/arduinoIDE/new_main_version_1`
- `arduinoIDE/new_main_version_2` → `archive/arduinoIDE/new_main_version_2`
- `arduinoIDE/new_main_version_3` → `archive/arduinoIDE/new_main_version_3`
- `arduinoIDE/new_main_version_4` → `archive/arduinoIDE/new_main_version_4`

### Deep sleep

- `arduinoIDE/main_deep_sleep` → `firmware/arduino-experiments/deep-sleep/main_deep_sleep`
- `arduinoIDE/main_deep_sleep_v1` → `firmware/arduino-experiments/deep-sleep/main_deep_sleep_v1`
- `arduinoIDE/main_deep_sleep_v2` → `firmware/arduino-experiments/deep-sleep/main_deep_sleep_v2`

### Timestamp relativo

- `arduinoIDE/main_timestamp_rel` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel`
- `arduinoIDE/main_timestamp_rel_v1` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v1`
- `arduinoIDE/main_timestamp_rel_v2` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v2`
- `arduinoIDE/main_timestamp_rel_v3` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v3`
- `arduinoIDE/main_timestamp_rel_v4` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v4`
- `arduinoIDE/main_timestamp_rel_v5` → `firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v5`

### Testes por domínio

- `arduinoIDE/teste_ble` → `firmware/arduino-experiments/connectivity/teste_ble`
- `arduinoIDE/teste_wifi` → `firmware/arduino-experiments/connectivity/teste_wifi`
- `arduinoIDE/teste_sdcard` → `firmware/arduino-experiments/connectivity/teste_sdcard`
- `arduinoIDE/teste_IMU_load` → `firmware/arduino-experiments/sensors/teste_IMU_load`
- `arduinoIDE/teste_loadcell` → `firmware/arduino-experiments/sensors/teste_loadcell`
- `arduinoIDE/teste_led` → `firmware/arduino-experiments/sensors/teste_led`

### Hardware

- `PCB_Pet/PetBionic_PCB.kicad_pcb` → `hardware/pcb/kicad/PetBionic_PCB.kicad_pcb`
- `PCB_Pet/PetBionic_PCB.kicad_sch` → `hardware/pcb/kicad/PetBionic_PCB.kicad_sch`
- `PCB_Pet/PetBionic_PCB.kicad_pro` → `hardware/pcb/kicad/PetBionic_PCB.kicad_pro`
- `PCB_Pet/PetBionic_PCB.kicad_prl` → `hardware/pcb/kicad/PetBionic_PCB.kicad_prl`
- `PCB_Pet/PetBionic_PCB.kicad_sch-bak` → `hardware/pcb/backups/PetBionic_PCB.kicad_sch-bak`
- `PCB_Pet/PetBionic_PCB.pretty` → `hardware/pcb/kicad/PetBionic_PCB.pretty`
- `PCB_Pet/PetBionic_PCB.step` → `hardware/pcb/kicad/PetBionic_PCB.step`
- `PCB_Pet/jlcpcb` → `hardware/pcb/jlcpcb`
- `PCB_Pet/production` → `hardware/pcb/production`
- `PCB_Pet/PetBionic_PCB-backups` → `hardware/pcb/backups/PetBionic_PCB-backups`
- `PCB_Pet/fabrication-toolkit-options.json` → `hardware/pcb/kicad/fabrication-toolkit-options.json`
- `PCB_Pet/fp-lib-table` → `hardware/pcb/kicad/fp-lib-table`
- `PCB_Pet/fp-info-cache` → `hardware/pcb/kicad/fp-info-cache`

---

## 🧭 Passos de Migração (seguro e incremental)

1. Criar a nova árvore com diretórios vazios.
2. Mover com git mv para preservar histórico.
3. Confirmar que o build principal compila a partir de firmware/platformio/petbionics.
4. Atualizar caminhos em documentação.
5. Só depois remover diretórios antigos vazios.

Exemplo de comandos:

```bash
mkdir -p firmware/platformio firmware/arduino-experiments/{deep-sleep,timestamp-rel,connectivity,sensors} hardware/pcb/{kicad,production,jlcpcb,backups} docs/{architecture,firmware,hardware,operations} tools/scripts archive/arduinoIDE

git mv platformIO/PetBionics firmware/platformio/petbionics

git mv arduinoIDE/main_deep_sleep firmware/arduino-experiments/deep-sleep/main_deep_sleep
git mv arduinoIDE/main_deep_sleep_v1 firmware/arduino-experiments/deep-sleep/main_deep_sleep_v1
git mv arduinoIDE/main_deep_sleep_v2 firmware/arduino-experiments/deep-sleep/main_deep_sleep_v2

git mv arduinoIDE/main_timestamp_rel firmware/arduino-experiments/timestamp-rel/main_timestamp_rel
git mv arduinoIDE/main_timestamp_rel_v1 firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v1
git mv arduinoIDE/main_timestamp_rel_v2 firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v2
git mv arduinoIDE/main_timestamp_rel_v3 firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v3
git mv arduinoIDE/main_timestamp_rel_v4 firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v4
git mv arduinoIDE/main_timestamp_rel_v5 firmware/arduino-experiments/timestamp-rel/main_timestamp_rel_v5

git mv arduinoIDE/teste_ble firmware/arduino-experiments/connectivity/teste_ble
git mv arduinoIDE/teste_wifi firmware/arduino-experiments/connectivity/teste_wifi
git mv arduinoIDE/teste_sdcard firmware/arduino-experiments/connectivity/teste_sdcard
git mv arduinoIDE/teste_IMU_load firmware/arduino-experiments/sensors/teste_IMU_load
git mv arduinoIDE/teste_loadcell firmware/arduino-experiments/sensors/teste_loadcell
git mv arduinoIDE/teste_led firmware/arduino-experiments/sensors/teste_led

git mv arduinoIDE/main archive/arduinoIDE/main
git mv arduinoIDE/main1 archive/arduinoIDE/main1
git mv arduinoIDE/mainPet archive/arduinoIDE/mainPet
git mv arduinoIDE/new_main archive/arduinoIDE/new_main
git mv arduinoIDE/new_main_version_1 archive/arduinoIDE/new_main_version_1
git mv arduinoIDE/new_main_version_2 archive/arduinoIDE/new_main_version_2
git mv arduinoIDE/new_main_version_3 archive/arduinoIDE/new_main_version_3
git mv arduinoIDE/new_main_version_4 archive/arduinoIDE/new_main_version_4

mkdir -p hardware/pcb/{kicad,production,jlcpcb,backups}
git mv PCB_Pet/jlcpcb hardware/pcb/jlcpcb
git mv PCB_Pet/production hardware/pcb/production
git mv PCB_Pet/PetBionic_PCB-backups hardware/pcb/backups/PetBionic_PCB-backups
git mv PCB_Pet/PetBionic_PCB.kicad_pcb hardware/pcb/kicad/
git mv PCB_Pet/PetBionic_PCB.kicad_sch hardware/pcb/kicad/
git mv PCB_Pet/PetBionic_PCB.kicad_pro hardware/pcb/kicad/
git mv PCB_Pet/PetBionic_PCB.kicad_prl hardware/pcb/kicad/
git mv PCB_Pet/PetBionic_PCB.kicad_sch-bak hardware/pcb/backups/
git mv PCB_Pet/PetBionic_PCB.pretty hardware/pcb/kicad/
git mv PCB_Pet/PetBionic_PCB.step hardware/pcb/kicad/
git mv PCB_Pet/fabrication-toolkit-options.json hardware/pcb/kicad/
git mv PCB_Pet/fp-lib-table hardware/pcb/kicad/
git mv PCB_Pet/fp-info-cache hardware/pcb/kicad/
```

---

## ✅ Convenções recomendadas daqui para a frente

- Todo firmware de produção entra apenas em `firmware/platformio/petbionics`
- Protótipos entram em `firmware/arduino-experiments/<dominio>/<nome>`
- Versões congeladas ficam em `archive/`
- Cada subpasta principal deve ter um README curto com objetivo e como usar
