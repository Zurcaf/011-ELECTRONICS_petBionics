#include <SPI.h>
#include <SD.h>

// Mapear Dx para GPIO reais

#define PIN_SPI_SCK   D9
#define PIN_SPI_MISO  D10
#define PIN_SPI_MOSI  D8
#define PIN_CS_SD     D6

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println("Inicializando SPI e SD card...");

  // Inicializa SPI com pinos customizados
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  // Inicializa SD card com CS e SPI
  if (!SD.begin(PIN_CS_SD, SPI)) {
    Serial.println("Falha ao inicializar o SD card!");
    return;
  }

  Serial.println("SD card inicializado com sucesso.");

  // Teste de escrita
  File testFile = SD.open("/teste.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Olá, ESP32-C3 Xiao!");
    testFile.close();
    Serial.println("Arquivo teste.txt escrito com sucesso!");
  } else {
    Serial.println("Falha ao abrir arquivo para escrita!");
  }

  // Teste de leitura
  testFile = SD.open("/teste.txt");
  if (testFile) {
    Serial.println("Conteúdo do teste.txt:");
    while (testFile.available()) {
      Serial.write(testFile.read());
    }
    testFile.close();
  } else {
    Serial.println("Falha ao abrir arquivo para leitura!");
  }
}

void loop() {
  // nada no loop
}
