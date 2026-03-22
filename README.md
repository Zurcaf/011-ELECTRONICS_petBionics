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

---

# 🏗 Arquitetura do Sistema

## 📟 Edge Device (Prótese)

Hardware principal:

- Microcontrolador: Seeed Studio XIAO ESP32-C3
- Sensor inercial: MPU-9250
- Sensor de carga
- Módulo SD Card

Funções:

- Aquisição de dados
- Processamento básico
- Armazenamento local
- Comunicação BLE (controlo)
- Comunicação Wi-Fi (sincronização)

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
