# Calibração de Montagem do IMU (imu_mounting_cal)

Determina a **matriz de rotação 3×3** que alinha o frame do sensor MPU-9250 com o frame anatómico da prótese canina.

---

## 🎯 O Problema

O sensor está montado em orientação arbitrária no PCB. Os seus eixos internos (Xs, Ys, Zs) não correspondem aos eixos da prótese:

```
Frame do SENSOR (arbitrário)  →  Frame da PRÓTESE (anatómico)
        Xs                              +X_body (frente/marcha)
        Ys                              +Y_body (lateral)
        Zs                              +Z_body (proximal/cima)
```

**Sem esta calibração:** Roll/Pitch/Yaw do firmware não correspondem à orientação real.  
**Com esta calibração:** Leituras alinhadas com a anatomia da prótese.

---

## 📏 Definição do Frame da Prótese

```
        encaixe
        (proximal)
            ↑
            │ +Z_body
       [████████]  ← prótese
            │
            ↓
          pé
       (distal)

   medial ← | → lateral
   ← -Y   +Y →

   traseira ← | → frente
   ← -X    +X →
```

| Eixo | Direção | Significado |
|------|---------|-------------|
| **+X_body** | Frente | Direcção de marcha, "nariz" da pata |
| **+Y_body** | Lateral (esquerdo) | Medial → Lateral do cão |
| **+Z_body** | Proximal (cima) | Encaixe → Pé (oposto à gravidade em apoio) |

---

## 🔧 Procedimento Experimental

### Equipamento

- Prótese montada no cão (ou fixada numa superfície)
- Laptop com Serial Monitor (115200 baud)
- ~30 segundos de tempo tranquilo

### Passos

#### 1️⃣ Preparação — Posição de Referência

Coloca a prótese em **posição de apoio neutra**:

```
✓ Encaixe apontado para CIMA (não para o lado/atrás)
✓ Pé apontado para BAIXO (posição de suporte normal)
✓ Imóvel numa superfície plana nivelada
✓ Pé orientado aproximadamente para NORTE magnético
  (ou na direcção principal de marcha se dentro de casa)
```

**Porquê?** O acelerómetro mede a reacção ao suporte ≈ +g (oposto à gravidade). Desta forma, `+Z_sensor` fica alinhado com `+Z_body` (proximal).

#### 2️⃣ Flash do Sketch

```bash
cd firmware/arduino-experiments/sensors/imu_mounting_cal/
platformio run -t upload -e seeed_xiao_esp32c3
```

#### 3️⃣ Monitorização

Abre o Serial Monitor:

```bash
platformio device monitor -b 115200
```

Vês o menu:

```
─────────────────────────────────────────────
 petBionic — Calibração de Montagem do IMU
─────────────────────────────────────────────
 PROCEDIMENTO:
  1. Prótese em posição de apoio neutra:
     – encaixe para CIMA, pé para BAIXO
     – imóvel numa superfície plana
     – pé apontado aproximadamente para norte
  2. Envia 'c' → regista 2 s de dados
  3. Envia 's' → calcula e guarda
 ...
─────────────────────────────────────────────
```

#### 4️⃣ Captura de Dados

Envia `c` (seguido de Enter):

```
[CAL] A registar 2 s de dados... mantém a prótese imóvel.
[CAL] 200 amostras registadas.
      Envia 's' para calcular e guardar.
```

Aguarda 2 segundos com a prótese imóvel.

#### 5️⃣ Cálculo e Armazenamento

Envia `s`:

```
[CAL] Accel médio: (134, -298, 16521) — magnitude: 16544 (esperado ~16384)
[CAL] Mag médio  : (-2345, -8902, -1234)
[CAL] Matriz R calculada:
  [  0.1234  -0.9876   0.0876]
  [  0.8765   0.1234   0.4567]
  [ -0.4599   0.0932   0.8827]
[CAL] Erro de ortonormalidade: 0.000042 (ideal = 0)
[NVS] Matriz R guardada com sucesso.
```

**Importante:** O erro de ortonormalidade deve ser **< 0.001**. Se for > 0.1, algo correu mal (sensor não estava imóvel? Superfície inclinada?).

#### 6️⃣ Verificação

Envia `r` para relê e mostra a matriz guardada:

```
[NVS] Matriz R carregada:
  [  0.1234  -0.9876   0.0876]
  [  0.8765   0.1234   0.4567]
  [ -0.4599   0.0932   0.8827]
```

Envia `v` para **monitorizar em tempo real**:

```
[MON] A monitorizar (envia qualquer tecla para parar)...
       ax       ay       az  |  rotated_ax rotated_ay rotated_az
     156     -234    16501  |      127      -156     16539
     158     -232    16502  |      129      -154     16540
```

Verifica que os valores rotacionados fazem sentido:
- `rotated_az` ≈ 16000 (gravidade, vertical)
- `rotated_ax` pequeno quando pé apontado para norte
- `rotated_ay` pequeno (sem movimento lateral)

---

## 🔐 Armazenamento em NVS

A matriz R é guardada em **NVS (Non-Volatile Storage)** do ESP32:

```
Namespace: "imu_cal"
Chaves:    "r00", "r01", "r02", "r10", "r11", "r12", "r20", "r21", "r22"
Tipo:      float (4 bytes cada)
Total:     36 bytes
```

Para limpar a calibração anterior (reset):

```bash
# Usa a ferramenta esptool (vem com Arduino IDE / PlatformIO)
esptool.py --port /dev/ttyACM0 erase_region 0x8000 0x1000
```

---

## 🔌 Pinout

| Pino | Função |
|------|--------|
| D6   | SPI Clock (SCLK) |
| D5   | SPI MISO |
| D4   | SPI MOSI |
| D7   | CS IMU |

---

## 🧮 Matemática da Calibração

O sketch usa **acelerómetro estático** + **magnetómetro** para calcular a matriz:

1. **Eixo Z:** Normaliza acelerómetro médio → estes é `+Z_body` no frame do sensor

   ```
   Z_sensor = normalize(accel_media)
   ```

2. **Eixo X:** Remove componente vertical do magnetómetro, normaliza → este é `+X_body` no frame do sensor

   ```
   X_sensor = normalize(mag_media − (mag_media · Z_sensor) * Z_sensor)
   ```

3. **Eixo Y:** Produto vetorial → garante ortonormalidade

   ```
   Y_sensor = Z_sensor × X_sensor
   ```

Resultado: **matriz R** onde cada linha = um eixo do body frame em coordenadas do sensor:

```
     [ X_sensor[0]  X_sensor[1]  X_sensor[2] ]
R =  [ Y_sensor[0]  Y_sensor[1]  Y_sensor[2] ]
     [ Z_sensor[0]  Z_sensor[1]  Z_sensor[2] ]
```

---

## 🔄 Aplicação no Firmware Principal

Após calibração, o firmware aplica a rotação em **RawSensor.cpp**:

```cpp
// Em RawSensor::readImuAxes(), antes de retornar:

if (_calLoaded) {
  auto rot = [&](float r[3][3], int16_t& x, int16_t& y, int16_t& z) {
    float xf = x, yf = y, zf = z;
    x = (int16_t)(r[0][0]*xf + r[0][1]*yf + r[0][2]*zf);
    y = (int16_t)(r[1][0]*xf + r[1][1]*yf + r[1][2]*zf);
    z = (int16_t)(r[2][0]*xf + r[2][1]*yf + r[2][2]*zf);
  };
  
  rot(_calR, ax, ay, az);  // acelerómetro
  rot(_calR, gx, gy, gz);  // giroscópio
  rot(_calR, mx, my, mz);  // magnetómetro
}
```

Ver [instruções completas no código do sketch](./imu_mounting_cal.ino#L397-L440).

---

## 🐛 Troubleshooting

| Sintoma | Causa | Solução |
|---------|-------|---------|
| `WHO_AM_I=0x00 / 0xFF` | SPI não detecta sensor | Verifica D4/D5/D6 ligados? MODE3? 1 MHz? |
| Erro de ortonormalidade > 0.1 | Sensor acelerado durante captura | Mantém mais tempo imóvel; repete `c` e `s` |
| `rotated_az` muito pequeno | Superfície inclinada | Usa nível; coloca numa mesa plana |
| Matriz R toda zeros | NVS corrompida | Executa erase_region; recalibra |
| Magnetómetro overflow | Campo magnético forte | Afasta do PC/equipment; tenta noutro local |

---

## 📝 Notas e Limitações

- **Calibração de uma única posição:** Não precisas de rotações múltiplas — uma posição neutra é suficiente se:
  - A superfície está bem nivelada
  - A prótese está imóvel
  - O magnetómetro não está em overflow (perto de equipamento de ferro)

- **Repetibilidade:** Se voltas a calibrar noutra posição (p.ex., prótese inclinada), obténs uma matriz diferente. Usa sempre a mesma posição de referência.

- **Antes de calibrar:** Certifica-te que o firmware principal **carrega a matriz da NVS** no `RawSensor::begin()`. Ver comentário do sketch para código exacto.

- **Offset de acelerómetro:** Este sketch **não faz calibração de offset** (bias) — apenas alinha frames. Se precisas também de bias correction, adiciona-a separadamente em `RawSensor::readImuAxes()`.

---

## 🔗 Ver Também

- [Arduino Experiments README](../README.md) — overview de todos os sketches
- [CSV Analyzer](../../../../analysis/README.md) — verificar dados após calibração
- [RawSensor.cpp](../../platformio_petBionics/src/sensors/RawSensor.cpp) — implementação no firmware principal
