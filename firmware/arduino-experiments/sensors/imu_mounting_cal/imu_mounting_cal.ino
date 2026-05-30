/*
 * petBionic — Calibração de Montagem do IMU
 * ==========================================
 * Target : Seeed XIAO ESP32-C3
 * Ficheiro: imu_mounting_cal.ino
 *
 * PROBLEMA
 * --------
 * O MPU-9250 está soldado no PCB em orientação arbitrária.
 * Os eixos do sensor (Xs, Ys, Zs) não correspondem aos eixos
 * anatómicos da prótese. Este sketch determina a matriz de
 * rotação R (3×3) que transforma leituras do frame do sensor
 * para o frame da prótese.
 *
 * FRAME DA PRÓTESE DEFINIDO AQUI
 * -------------------------------
 *   +X_body : direcção de marcha (para a frente)
 *   +Y_body : lateral esquerdo do cão (medial → lateral)
 *   +Z_body : proximal (de baixo/pé para cima/encaixe)
 *             = direcção oposta à gravidade quando o cão está em apoio
 *
 * PROCEDIMENTO (1 posição, ~30 segundos)
 * ----------------------------------------
 * 1. Coloca a prótese em posição de apoio neutra:
 *      – encaixe para cima, pé para baixo
 *      – imóvel em cima de uma superfície plana nivelada
 *      – orientar o "pé" aproximadamente para norte magnético
 *        (se quiseres que +X = frente = norte)
 *
 * 2. Abre o Serial Monitor a 115200 baud.
 *
 * 3. Aguarda o menu aparecer e envia 'c' → regista 2 s de dados.
 *
 * 4. Envia 's' → calcula R e guarda na NVS.
 *
 * 5. Envia 'r' → relê e imprime a matriz guardada.
 *
 * 6. Envia 'v' → mostra acelerómetro e giroscópio em tempo real
 *    (para verificares se o resultado faz sentido).
 *
 * RESULTADO
 * ---------
 * 9 floats em NVS, namespace "imu_cal":
 *   r00 r01 r02
 *   r10 r11 r12
 *   r20 r21 r22
 *
 * APLICAÇÃO NO FIRMWARE
 * ---------------------
 * Em RawSensor::readImuAxes(), antes de devolver os valores,
 * aplica a rotação:
 *
 *   [ax_b]   [r00 r01 r02] [ax_s]
 *   [ay_b] = [r10 r11 r12] [ay_s]
 *   [az_b]   [r20 r21 r22] [az_s]
 *
 * (idem para giroscópio e magnetómetro)
 *
 * Ver comentário no fim deste ficheiro com o código C++ a adicionar.
 */

#include <SPI.h>
#include <Preferences.h>
#include <math.h>

// ── Pinos (iguais ao firmware principal) ─────────────────────────────────────
#define PIN_SPI_SCK   D6
#define PIN_SPI_MISO  D5
#define PIN_SPI_MOSI  D4
#define PIN_CS_IMU    D7

// ── Registos MPU-9250 ─────────────────────────────────────────────────────────
#define REG_PWR_MGMT_1    0x6B
#define REG_USER_CTRL     0x6A
#define REG_I2C_MST_CTRL  0x24
#define REG_ACCEL_XOUT_H  0x3B   // 6 bytes accel, 2 temp, 6 gyro (14 bytes total)
#define REG_WHO_AM_I      0x75

// AK8963 (magnetómetro interno) via I2C master do MPU
#define REG_I2C_SLV0_ADDR 0x25
#define REG_I2C_SLV0_REG  0x26
#define REG_I2C_SLV0_DO   0x63
#define REG_I2C_SLV0_CTRL 0x27
#define REG_EXT_SENS_DATA 0x49   // 8 bytes do AK8963
#define AK8963_ADDR       0x0C
#define AK_REG_ST1        0x02
#define AK_REG_CNTL1      0x0A
#define AK_CNTL1_16BIT_100HZ 0x16

// ── NVS ──────────────────────────────────────────────────────────────────────
#define NVS_NS   "imu_cal"

// ── Globals ───────────────────────────────────────────────────────────────────
SPIClass SPIbus(FSPI);

// Matriz R[3][3] em memória — linha a linha
float R[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

// Acumuladores para a posição de referência
float  g_sum[3] = {0, 0, 0};   // acelerómetro (gravity proxy)
float  m_sum[3] = {0, 0, 0};   // magnetómetro
int    n_samples = 0;

// ── SPI helpers ───────────────────────────────────────────────────────────────
void imuWrite(uint8_t reg, uint8_t val) {
  SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg);
  SPIbus.transfer(val);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

void imuRead(uint8_t reg, uint8_t n, uint8_t *dst) {
  SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  for (uint8_t i = 0; i < n; i++) dst[i] = SPIbus.transfer(0);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

// ── AK8963 via I2C master ─────────────────────────────────────────────────────
void akWrite(uint8_t reg, uint8_t val) {
  imuWrite(REG_I2C_SLV0_ADDR, AK8963_ADDR);
  imuWrite(REG_I2C_SLV0_REG,  reg);
  imuWrite(REG_I2C_SLV0_DO,   val);
  imuWrite(REG_I2C_SLV0_CTRL, 0x81);
  delay(2);
  imuWrite(REG_I2C_SLV0_CTRL, 0x00);
}

bool akRead(uint8_t reg, uint8_t n, uint8_t *dst) {
  imuWrite(REG_I2C_SLV0_ADDR, AK8963_ADDR | 0x80);
  imuWrite(REG_I2C_SLV0_REG,  reg);
  imuWrite(REG_I2C_SLV0_CTRL, 0x80 | n);
  delay(3);
  imuRead(REG_EXT_SENS_DATA, n, dst);
  imuWrite(REG_I2C_SLV0_CTRL, 0x00);
  return true;
}

// ── Inicialização IMU ─────────────────────────────────────────────────────────
bool imuInit() {
  imuWrite(REG_PWR_MGMT_1, 0x00);  delay(20);

  uint8_t who = 0;
  imuRead(REG_WHO_AM_I, 1, &who);
  if (who == 0x00 || who == 0xFF) {
    Serial.printf("[IMU] WHO_AM_I=0x%02X — sensor não detectado\n", who);
    return false;
  }
  Serial.printf("[IMU] WHO_AM_I=0x%02X OK\n", who);

  // Activa I2C master para acesso ao AK8963
  imuWrite(REG_USER_CTRL,    0x20); delay(10);
  imuWrite(REG_I2C_MST_CTRL, 0x00); delay(10);

  // Configura AK8963 em modo contínuo 16-bit 100 Hz
  akWrite(AK_REG_CNTL1, 0x00); delay(30);
  akWrite(AK_REG_CNTL1, AK_CNTL1_16BIT_100HZ); delay(30);

  uint8_t cntl = 0;
  akRead(AK_REG_CNTL1, 1, &cntl);
  Serial.printf("[Mag] CNTL1=0x%02X %s\n", cntl,
                cntl == AK_CNTL1_16BIT_100HZ ? "OK" : "WARN: modo inesperado");
  return true;
}

// ── Leitura de uma amostra ────────────────────────────────────────────────────
struct Sample { float ax, ay, az, mx, my, mz; };

Sample readSample() {
  uint8_t raw[14];
  imuRead(REG_ACCEL_XOUT_H, 14, raw);
  Sample s;
  s.ax = (int16_t)((raw[0]<<8)|raw[1]);
  s.ay = (int16_t)((raw[2]<<8)|raw[3]);
  s.az = (int16_t)((raw[4]<<8)|raw[5]);
  // raw[6..7] = temperatura (ignorado)
  // raw[8..13] = giroscópio (não necessário para calibração de montagem)

  uint8_t magRaw[8] = {0};
  akRead(AK_REG_ST1, 8, magRaw);
  const bool overflow  = (magRaw[7] & 0x08) != 0;
  if (!overflow) {
    s.mx = (int16_t)((magRaw[2]<<8)|magRaw[1]);
    s.my = (int16_t)((magRaw[4]<<8)|magRaw[3]);
    s.mz = (int16_t)((magRaw[6]<<8)|magRaw[5]);
  } else {
    s.mx = s.my = s.mz = 0;
  }
  return s;
}

// ── Álgebra 3D (não usa nenhuma lib externa) ──────────────────────────────────
static void norm3(float v[3]) {
  float n = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
  if (n < 1e-6f) return;
  v[0] /= n; v[1] /= n; v[2] /= n;
}

static float dot3(const float a[3], const float b[3]) {
  return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1]*b[2] - a[2]*b[1];
  out[1] = a[2]*b[0] - a[0]*b[2];
  out[2] = a[0]*b[1] - a[1]*b[0];
}

// ── Cálculo da matriz R ───────────────────────────────────────────────────────
/*
 * Frame da prótese:
 *   +Z_body = proximal (do pé para o encaixe) = oposto à gravidade em apoio
 *   +X_body = frente (direcção de marcha) ≈ componente horizontal do campo magnético
 *   +Y_body = lateral (Z × X)
 *
 * O acelerómetro estático mede a reacção ao suporte ≈ −g_world = [0,0,+g] em world frame.
 * Portanto a sua leitura em sensor frame aponta na direcção de +Z_body em sensor frame.
 *
 * R[linha i] = componentes do i-ésimo eixo do body frame, expressas em sensor frame
 * → r.g.: linha 2 de R = Z_body em sensor frame = normalize(accel_mean)
 */
bool computeR() {
  if (n_samples < 5) {
    Serial.println("[CAL] Amostras insuficientes — executa 'c' primeiro");
    return false;
  }

  float g[3] = { g_sum[0]/n_samples, g_sum[1]/n_samples, g_sum[2]/n_samples };
  float m[3] = { m_sum[0]/n_samples, m_sum[1]/n_samples, m_sum[2]/n_samples };

  Serial.printf("[CAL] Accel médio: (%.0f, %.0f, %.0f) — magnitude: %.0f (esperado ~16384)\n",
                g[0], g[1], g[2], sqrtf(g[0]*g[0]+g[1]*g[1]+g[2]*g[2]));
  Serial.printf("[CAL] Mag médio  : (%.0f, %.0f, %.0f)\n", m[0], m[1], m[2]);

  // Z_body no frame do sensor = normalize(accel)
  float z_s[3] = {g[0], g[1], g[2]};
  norm3(z_s);

  // Componente horizontal do magnetómetro → X_body (frente / norte magnético)
  float proj = dot3(m, z_s);
  float x_s[3] = {
    m[0] - proj*z_s[0],
    m[1] - proj*z_s[1],
    m[2] - proj*z_s[2]
  };
  norm3(x_s);

  // Y_body = Z × X (lateral, regra mão direita)
  float y_s[3];
  cross3(z_s, x_s, y_s);
  norm3(y_s);

  // Recalcula X para garantir ortonormalidade
  cross3(y_s, z_s, x_s);
  norm3(x_s);

  // Preenche R (cada linha = eixo do body frame em coords do sensor)
  for (int j = 0; j < 3; j++) {
    R[0][j] = x_s[j];
    R[1][j] = y_s[j];
    R[2][j] = z_s[j];
  }

  Serial.println("[CAL] Matriz R calculada:");
  for (int i = 0; i < 3; i++) {
    Serial.printf("  [%7.4f  %7.4f  %7.4f]\n", R[i][0], R[i][1], R[i][2]);
  }

  // Verifica ortonormalidade: R*R^T deve ser ~identidade
  float err = 0;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      float s = R[i][0]*R[j][0] + R[i][1]*R[j][1] + R[i][2]*R[j][2];
      float expected = (i==j) ? 1.0f : 0.0f;
      err += fabsf(s - expected);
    }
  }
  Serial.printf("[CAL] Erro de ortonormalidade: %.6f (ideal = 0)\n", err);
  return true;
}

// ── NVS ──────────────────────────────────────────────────────────────────────
void saveToNVS() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  char key[6];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      snprintf(key, sizeof(key), "r%d%d", i, j);
      prefs.putFloat(key, R[i][j]);
    }
  prefs.end();
  Serial.println("[NVS] Matriz R guardada com sucesso.");
}

void loadFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  char key[6];
  bool hasAll = true;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      snprintf(key, sizeof(key), "r%d%d", i, j);
      if (!prefs.isKey(key)) { hasAll = false; break; }
      R[i][j] = prefs.getFloat(key, (i==j) ? 1.0f : 0.0f);
    }
  prefs.end();
  if (hasAll) {
    Serial.println("[NVS] Matriz R carregada:");
    for (int i = 0; i < 3; i++)
      Serial.printf("  [%7.4f  %7.4f  %7.4f]\n", R[i][0], R[i][1], R[i][2]);
  } else {
    Serial.println("[NVS] Sem calibração guardada — usando identidade.");
  }
}

// ── Menu ──────────────────────────────────────────────────────────────────────
void printMenu() {
  Serial.println("\n─────────────────────────────────────────────");
  Serial.println(" petBionic — Calibração de Montagem do IMU");
  Serial.println("─────────────────────────────────────────────");
  Serial.println(" PROCEDIMENTO:");
  Serial.println("  1. Prótese em posição de apoio neutra:");
  Serial.println("     – encaixe para CIMA, pé para BAIXO");
  Serial.println("     – imóvel numa superfície plana");
  Serial.println("     – pé apontado aproximadamente para norte");
  Serial.println("       (ou na direcção principal de marcha)");
  Serial.println("  2. Envia 'c' → regista 2 s de dados");
  Serial.println("  3. Envia 's' → calcula e guarda");
  Serial.println("");
  Serial.println(" COMANDOS:");
  Serial.println("  c – capturar posição de referência");
  Serial.println("  s – calcular R e guardar na NVS");
  Serial.println("  r – ler e mostrar calibração guardada");
  Serial.println("  v – monitorizar accel em tempo real");
  Serial.println("  h – mostrar este menu");
  Serial.println("─────────────────────────────────────────────\n");
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);

  pinMode(PIN_CS_IMU, OUTPUT);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);

  if (!imuInit()) {
    Serial.println("ERRO: IMU não inicializado. Verifica a ligação SPI.");
    while (true) delay(1000);
  }

  loadFromNVS();
  printMenu();
}

void loop() {
  if (!Serial.available()) return;
  char cmd = Serial.read();
  while (Serial.available()) Serial.read();  // limpa buffer

  switch (cmd) {

    case 'c': {
      // ── Captura posição de referência ─────────────────────────────────────
      Serial.println("[CAL] A registar 2 s de dados... mantém a prótese imóvel.");
      memset(g_sum, 0, sizeof(g_sum));
      memset(m_sum, 0, sizeof(m_sum));
      n_samples = 0;

      uint32_t t_end = millis() + 2000;
      while (millis() < t_end) {
        Sample s = readSample();
        g_sum[0] += s.ax; g_sum[1] += s.ay; g_sum[2] += s.az;
        m_sum[0] += s.mx; m_sum[1] += s.my; m_sum[2] += s.mz;
        n_samples++;
        delay(10);
      }
      Serial.printf("[CAL] %d amostras registadas.\n", n_samples);
      Serial.println("      Envia 's' para calcular e guardar.");
      break;
    }

    case 's':
      if (computeR()) saveToNVS();
      break;

    case 'r':
      loadFromNVS();
      break;

    case 'v': {
      // ── Monitor em tempo real ─────────────────────────────────────────────
      Serial.println("[MON] A monitorizar (envia qualquer tecla para parar)...");
      Serial.println("       ax       ay       az  |  rotated_ax rotated_ay rotated_az");
      while (!Serial.available()) {
        Sample s = readSample();
        float ax_b = R[0][0]*s.ax + R[0][1]*s.ay + R[0][2]*s.az;
        float ay_b = R[1][0]*s.ax + R[1][1]*s.ay + R[1][2]*s.az;
        float az_b = R[2][0]*s.ax + R[2][1]*s.ay + R[2][2]*s.az;
        Serial.printf("  %7.0f %7.0f %7.0f  |  %7.0f    %7.0f    %7.0f\n",
                      s.ax, s.ay, s.az, ax_b, ay_b, az_b);
        delay(100);
      }
      while (Serial.available()) Serial.read();
      printMenu();
      break;
    }

    case 'h':
    default:
      printMenu();
      break;
  }
}

/*
 ┌────────────────────────────────────────────────────────────────────────────┐
 │  MODIFICAÇÃO DO FIRMWARE PRINCIPAL (RawSensor.cpp)                         │
 ├────────────────────────────────────────────────────────────────────────────┤
 │                                                                             │
 │  1. Em RawSensor.h, adicionar membros privados:                             │
 │                                                                             │
 │       float _calR[3][3];    // matriz de rotação de montagem                │
 │       bool  _calLoaded;                                                     │
 │                                                                             │
 │  2. Em RawSensor::begin(), após a secção do HX711, carregar da NVS:        │
 │                                                                             │
 │       Preferences prefs;                                                    │
 │       prefs.begin("imu_cal", true);                                         │
 │       _calLoaded = prefs.isKey("r00");                                      │
 │       if (_calLoaded) {                                                     │
 │         char key[6];                                                        │
 │         for (int i=0;i<3;i++) for (int j=0;j<3;j++) {                     │
 │           snprintf(key,6,"r%d%d",i,j);                                     │
 │           _calR[i][j] = prefs.getFloat(key,(i==j)?1.f:0.f);               │
 │         }                                                                   │
 │         Serial.println("[IMU] Calibração de montagem carregada da NVS.");  │
 │       } else {                                                              │
 │         // sem calibração: usa identidade                                   │
 │         for(int i=0;i<3;i++) for(int j=0;j<3;j++)                         │
 │           _calR[i][j] = (i==j)?1.f:0.f;                                   │
 │       }                                                                     │
 │       prefs.end();                                                          │
 │                                                                             │
 │  3. Em RawSensor::readImuAxes(), ANTES de devolver os valores,              │
 │     aplicar a rotação (substitui as últimas linhas da função):              │
 │                                                                             │
 │       if (_calLoaded) {                                                     │
 │         auto rot = [&](float r[3][3], int16_t& x, int16_t& y, int16_t& z)│
 │         {                                                                   │
 │           float xf=x, yf=y, zf=z;                                         │
 │           x = (int16_t)(r[0][0]*xf + r[0][1]*yf + r[0][2]*zf);           │
 │           y = (int16_t)(r[1][0]*xf + r[1][1]*yf + r[1][2]*zf);           │
 │           z = (int16_t)(r[2][0]*xf + r[2][1]*yf + r[2][2]*zf);           │
 │         };                                                                  │
 │         rot(_calR, ax, ay, az);   // acelerómetro                          │
 │         rot(_calR, gx, gy, gz);   // giroscópio                            │
 │         rot(_calR, mx, my, mz);   // magnetómetro                          │
 │       }                                                                     │
 │                                                                             │
 │  Depois de aplicar estas alterações e flashar, as leituras do IMU          │
 │  passam a estar no frame da prótese.                                       │
 └────────────────────────────────────────────────────────────────────────────┘
 */
