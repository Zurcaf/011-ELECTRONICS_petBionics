# petBionic — Ferramentas e Sketches — Índice

Documentação consolidada para as ferramentas de análise de dados e sketches experimentais de Arduino.

---

## 📊 Análise de Dados — `/analysis/`

**Local:** `/Users/afons/Desktop/petBionic/analysis/`  
**Documentação:** [`analysis/README.md`](./analysis/README.md)

### Ferramentas

| Ferramenta | Descrição | Uso |
|------------|-----------|-----|
| **csv_cleaner.py** | Corrige bugs nos CSVs do firmware antigo (kg congelado, lixo) | `.venv/bin/python csv_cleaner.py` |
| **csv_analyzer.py** | Visualizador interactivo com 6 tabs, modelo 3D, gráficos | `bash run_analysis.sh` |
| **run_analysis.sh** | Launcher — cria venv automaticamente | `bash run_analysis.sh [ficheiro.csv]` |

### Estrutura

```
analysis/
├── csv_cleaner.py
├── csv_analyzer.py
├── requirements.txt
├── run_analysis.sh
├── .venv/              (criado automaticamente)
└── scripts/
    └── ble_time_sync_test.py
```

---

## 🔬 Sketches Arduino — `firmware/arduino-experiments/`

**Local:** `/Users/afons/Desktop/petBionic/firmware/arduino-experiments/`  
**Documentação:** [`firmware/arduino-experiments/README.md`](./firmware/arduino-experiments/README.md)

### Categorias

| Pasta | Descrição | Sketches |
|-------|-----------|----------|
| **sensors/** | Testes isolados de sensores | `teste_loadcell`, `teste_IMU_load`, `teste_led`, **`imu_mounting_cal`** ✨ |
| **connectivity/** | Rede e armazenamento | `teste_ble`, `teste_wifi`, `teste_sdcard` |
| **deep-sleep/** | Hibernação | `main_deep_sleep*` (3 versões) |
| **timestamp-rel/** | Sincronização de tempo | `main_timestamp_rel*` (6 versões) |

### Novo: Calibração de Montagem do IMU

**Local:** `firmware/arduino-experiments/sensors/imu_mounting_cal/`  
**Documentação:** [`imu_mounting_cal/README.md`](./firmware/arduino-experiments/sensors/imu_mounting_cal/README.md)

Alinha o frame do sensor com o frame anatómico da prótese.

**Procedimento rápido:**
1. Prótese em posição de apoio neutra
2. Envia `c` → regista 2 s
3. Envia `s` → calcula e guarda na NVS
4. Envia `v` → verifica em tempo real

Resultado: Matriz R 3×3 guardada em NVS.

---

## 📍 Pinout Reference

| Pino | Função | Sketches |
|------|--------|----------|
| D4   | SPI MOSI | `teste_IMU_load`, `imu_mounting_cal`, `teste_sdcard` |
| D5   | SPI MISO | idem |
| D6   | SPI SCK | idem |
| D7   | CS IMU | idem |
| D8   | CS SD | `teste_sdcard` |
| D9   | HX711 SCK | `teste_loadcell`, `teste_IMU_load` |
| D10  | HX711 DT | idem |

---

## 🚀 Quick Start

### Análise de dados

```bash
# Lança o visualizador (automático)
cd /Users/afons/Desktop/petBionic/analysis
bash run_analysis.sh

# Limpeza de CSVs
.venv/bin/python csv_cleaner.py
```

### Arduino

```bash
# Calibração do IMU (novo!)
cd firmware/arduino-experiments/sensors/imu_mounting_cal/
platformio run -t upload -e seeed_xiao_esp32c3
platformio device monitor -b 115200
```

---

## 📚 Documentação Completa

- **Análise:** [`analysis/README.md`](./analysis/README.md)
- **Sketches gerais:** [`firmware/arduino-experiments/README.md`](./firmware/arduino-experiments/README.md)
- **Calibração IMU:** [`firmware/arduino-experiments/sensors/imu_mounting_cal/README.md`](./firmware/arduino-experiments/sensors/imu_mounting_cal/README.md)

---

## ✨ Novidades (Maio 2026)

- ✅ **CSV Analyzer** — visualizador interactivo com unwrapping de ângulos, modelo 3D, zoom
- ✅ **CSV Cleaner** — corrige bugs do firmware antigo
- ✅ **IMU Mounting Calibration** — alinhamento de frames sensor ↔ prótese
- ✅ **Reorganização de pastas** — `tools/` → `analysis/`
