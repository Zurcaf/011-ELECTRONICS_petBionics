#include "HX711.h"

// ================= ESP32-C3 PINOUT =================
#define PIN_HX_DT D10
#define PIN_HX_SCK D9

HX711 scale;

void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("=== HX711 Load Cell Test ===");

    scale.begin(PIN_HX_DT, PIN_HX_SCK);

    Serial.println("Checking if HX711 is ready...");
    if (scale.is_ready())
    {
        Serial.println("HX711 is ready!");
    }
    else
    {
        Serial.println("HX711 not found. Check wiring:");
        Serial.println("  DT  -> D10");
        Serial.println("  SCK -> D9");
        Serial.println("  VCC -> 3.3V or 5V");
        Serial.println("  GND -> GND");
    }

    Serial.println("\nReading raw values (no calibration)...");
    Serial.println("Remove all weight and wait for stable readings.");
}

void loop()
{
    if (scale.is_ready())
    {
        long rawValue = scale.read_average(10);
        Serial.print("Raw ADC: ");
        Serial.println(rawValue);

        // To calibrate:
        // 1. Note the zero value (no weight)
        // 2. scale.tare() to zero it
        // 3. Add known weight and note new reading
        // 4. Calculate scale factor: (new_reading - zero) / weight_in_units
    }
    else
    {
        Serial.println("HX711 not ready!");
    }

    delay(500);
}
