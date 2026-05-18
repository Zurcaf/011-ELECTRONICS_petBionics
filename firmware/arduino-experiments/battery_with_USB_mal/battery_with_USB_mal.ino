#define BATTERY_PIN A0

float batteryVoltageToPercent(float vbat) {
  // Estimativa simples para LiPo 1S
  if (vbat >= 4.20) return 100;
  if (vbat >= 4.10) return 90;
  if (vbat >= 4.00) return 80;
  if (vbat >= 3.92) return 70;
  if (vbat >= 3.85) return 60;
  if (vbat >= 3.79) return 50;
  if (vbat >= 3.74) return 40;
  if (vbat >= 3.68) return 30;
  if (vbat >= 3.60) return 20;
  if (vbat >= 3.50) return 10;
  if (vbat >= 3.30) return 5;
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BATTERY_PIN, INPUT);
}

void loop() {
  uint32_t sum_mV = 0;

  for (int i = 0; i < 50; i++) {
    sum_mV += analogReadMilliVolts(BATTERY_PIN);
    delay(10);
  }

  float adcVoltage = (sum_mV / 50.0) / 1000.0;

  // Como o divisor é 220k / 220k:
  float batteryVoltage = adcVoltage * 2.0;

  float percent = batteryVoltageToPercent(batteryVoltage);

  Serial.print("Tensao bateria: ");
  Serial.print(batteryVoltage, 3);
  Serial.print(" V | Percentagem estimada: ");
  Serial.print(percent, 0);
  Serial.println(" %");

  delay(1000);
}
