#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool bleSubscribed = false;

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){ deviceConnected=true; }
  void onDisconnect(BLEServer* pServer){ deviceConnected=false; bleSubscribed=false; }
};

class MyDescriptorCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) override {
    uint8_t* value = pDescriptor->getValue();
    if(value[0] == 1) bleSubscribed = true;
    else bleSubscribed = false;
  }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32-BLE-Test");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );

  BLE2902* p2902 = new BLE2902();
  p2902->setCallbacks(new MyDescriptorCallbacks());
  pCharacteristic->addDescriptor(p2902);

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE Server Started. Connect your phone!");
}

void loop() {
  if(deviceConnected && bleSubscribed){
    String data = "Hello from ESP32 at " + String(millis());
    const int CHUNK_SIZE = 20; // must not exceed MTU payload
    int len = data.length();
    int pos = 0;
    while(pos < len){
      int size = min(CHUNK_SIZE, len - pos);
      pCharacteristic->setValue((uint8_t*)(data.c_str()+pos), size);
      pCharacteristic->notify();
      pos += size;
      delay(10); // very important, BLE stack needs time
    }
    const char nl = '\n';
    pCharacteristic->setValue((uint8_t*)&nl,1);
    pCharacteristic->notify();
    delay(500); // send every 0.5s
  }
}
