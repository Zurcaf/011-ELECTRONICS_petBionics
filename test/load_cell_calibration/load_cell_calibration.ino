//
// Load cell (HX711 + LCR02) calibration sketch for petBionic
//
// Run this ONCE after assembling the prosthesis. The result is saved to the
// ESP32's NVS flash so the main firmware loads it on every boot without
// needing a tare procedure.
//
// Two values are stored in NVS namespace "loadcell":
//   offset  (long)  — raw ADC reading with zero load (tare)
//   factor  (float) — ADC counts per kg
//
// Instructions:
//   1. Upload this sketch to the XIAO ESP32-C3
//   2. Open Serial Monitor at 115200 baud (line ending: Newline)
//   3. Follow the on-screen steps
//   4. When done, upload the main firmware — calibration persists in flash
//
// Pins: DOUT=D10, SCK=D9  (mirrors Pinout.h)
//

#include <HX711.h>
#include <Preferences.h>

static constexpr uint8_t kDout = D10;
static constexpr uint8_t kSck  = D9;
static constexpr int     kAvgSamples = 20;  // readings averaged per measurement

HX711      scale;
Preferences prefs;

// ---------- helpers ----------------------------------------------------------

static void flushSerial()
{
    while (Serial.available()) Serial.read();
}

static void waitEnter(const char *prompt)
{
    Serial.println(prompt);
    flushSerial();
    while (!Serial.available());
    flushSerial();
}

static float readFloatFromSerial()
{
    flushSerial();
    while (!Serial.available());
    String s = Serial.readStringUntil('\n');
    s.trim();
    return s.toFloat();
}

// ---------- setup ------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  petBionic — Load Cell Calibration");
    Serial.println("========================================");
    Serial.println();

    // ── Init HX711 ──────────────────────────────────────────────────────────
    scale.begin(kDout, kSck);
    if (!scale.wait_ready_timeout(3000))
    {
        Serial.println("[ERRO] HX711 nao responde. Verifica ligacoes DOUT/SCK.");
        while (true);
    }
    Serial.println("[OK] HX711 detectado.");
    Serial.println();

    // ── Check existing calibration ───────────────────────────────────────────
    prefs.begin("loadcell", true);
    bool alreadyCalibrated = prefs.isKey("offset") && prefs.isKey("factor");
    if (alreadyCalibrated)
    {
        long  oldOffset = prefs.getLong("offset", 0);
        float oldFactor = prefs.getFloat("factor", 0.0f);
        Serial.println("Ja existe calibracao guardada na NVS:");
        Serial.printf("  offset = %ld\n", oldOffset);
        Serial.printf("  factor = %.1f counts/kg\n", oldFactor);
        Serial.println("Esta calibracao sera substituida se continuares.");
        Serial.println();
    }
    prefs.end();

    // ── PASSO 1: Tare ────────────────────────────────────────────────────────
    Serial.println("PASSO 1 — Zero (tare)");
    Serial.println("-----------------------------------------");
    waitEnter("  Remove todo o peso da celula de carga e prime Enter...");

    Serial.printf("  A calcular zero com %d leituras...\n", kAvgSamples);
    scale.tare(kAvgSamples);
    long offset = scale.get_offset();
    Serial.printf("  OFFSET = %ld  (valor raw sem carga)\n", offset);
    Serial.println();

    // ── PASSO 2: Peso conhecido ──────────────────────────────────────────────
    Serial.println("PASSO 2 — Fator de escala");
    Serial.println("-----------------------------------------");
    Serial.println("  Coloca um peso conhecido em cima da celula.");
    Serial.print  ("  Envia o valor em kg (ex: 1.000) e prime Enter: ");
    float knownKg = readFloatFromSerial();

    if (knownKg <= 0.0f)
    {
        Serial.println("[ERRO] Peso invalido. Reinicia e tenta de novo.");
        while (true);
    }
    Serial.printf("\n  Peso introduzido: %.3f kg\n", knownKg);

    Serial.printf("  A ler %d amostras...\n", kAvgSamples);
    long avgRaw = scale.read_average(kAvgSamples);
    float factor = static_cast<float>(avgRaw - offset) / knownKg;

    Serial.printf("  RAW com carga     = %ld\n", avgRaw);
    Serial.printf("  RAW sem carga     = %ld  (offset)\n", offset);
    Serial.printf("  Diferenca         = %ld counts\n", avgRaw - offset);
    Serial.printf("  FACTOR            = %.1f counts/kg\n", factor);
    Serial.println();

    if (factor <= 0.0f)
    {
        Serial.println("[ERRO] Factor negativo ou zero — verifica se a celula esta");
        Serial.println("       a ser comprimida e nao traccionada.");
        while (true);
    }

    // ── PASSO 3: Verificação ─────────────────────────────────────────────────
    Serial.println("PASSO 3 — Verificacao (manten o peso)");
    Serial.println("-----------------------------------------");
    scale.set_scale(factor);
    float sumErr = 0.0f;
    for (int i = 0; i < 5; i++)
    {
        float measured = scale.get_units(5);
        float err = measured - knownKg;
        sumErr += err * err;
        Serial.printf("  Leitura %d: %+.4f kg   (erro: %+.4f kg)\n", i + 1, measured, err);
        delay(300);
    }
    float rmse = sqrtf(sumErr / 5.0f);
    Serial.printf("  RMSE = %.4f kg\n", rmse);

    if (rmse > 0.05f)
        Serial.println("  [AVISO] RMSE > 50g — considera aumentar kAvgSamples ou verificar fixacao.");
    else
        Serial.println("  [OK] Precisao dentro do esperado.");
    Serial.println();

    // ── PASSO 4: Guardar ─────────────────────────────────────────────────────
    Serial.println("Prima 'S' + Enter para guardar, ou qualquer outra tecla para cancelar:");
    flushSerial();
    while (!Serial.available());
    char c = Serial.read();
    flushSerial();

    if (c == 'S' || c == 's')
    {
        prefs.begin("loadcell", false);
        prefs.putLong ("offset", offset);
        prefs.putFloat("factor", factor);
        prefs.end();

        Serial.println();
        Serial.println("========================================");
        Serial.println("  Calibracao guardada na NVS.");
        Serial.println();
        Serial.printf ("  offset = %ld\n", offset);
        Serial.printf ("  factor = %.1f counts/kg\n", factor);
        Serial.println();
        Serial.println("  Podes agora carregar o firmware normal.");
        Serial.println("  A calibracao persiste mesmo apos reboot.");
        Serial.println("========================================");
    }
    else
    {
        Serial.println("[CANCELADO] Nada foi guardado.");
    }
}

void loop() {}
