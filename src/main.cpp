#include <Arduino.h>
#include "pipeline/PetBionicsApp.h"

PetBionicsApp app;

void setup() {
    app.begin();
}

void loop() {
    app.update();
}
