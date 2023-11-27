#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>


BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
BLEDescriptor *pDescr;
BLE2902 *pBLE2902;



bool deviceConnected = false;
bool oldDeviceConnected = false;

int counter = 0;

#define DHTPIN 4  
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DATA_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"


class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};
void setup() {
  Serial.begin(115200);
  dht.begin();

  esp_sleep_enable_timer_wakeup(1 * 20 * 1000000);

  // Create the BLE Device
  BLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      DATA_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
                      
                    );                   

  // Create a BLE Descriptor
  // Add temp
  pDescr = new BLEDescriptor((uint16_t)0x2901);
  pDescr->setValue("DHT22 Sensor Data");
  pCharacteristic->addDescriptor(pDescr);

  pBLE2902 = new BLE2902();
  pBLE2902->setNotifications(true);
  pCharacteristic->addDescriptor(pBLE2902);



  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop() {

    Serial.print("awake: ");
    Serial.println(counter);

    // if esp has been awake for 20s then it should sleep
    if(counter == 60){ 
      counter = 0;
      Serial.println("going to sleep now");
      delay(1000);
      esp_deep_sleep_start();
    }

    // read data from the DHT sensor
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(humidity) || isnan(temp)) {
            Serial.println("Failed to read from DHT sensor!");
        } else {
            // Prepare JSON data
            StaticJsonDocument<200> jsonDoc;
            jsonDoc["timestamp"] = "2023-10-18T02:45:00Z"; // Replace with actual timestamp
            jsonDoc["temp"] = temp;
            jsonDoc["humidity"] = humidity;
            char jsonString[200];
            serializeJson(jsonDoc, jsonString);

            // Notify connected clients
            if (deviceConnected) {
                pCharacteristic->setValue(jsonString);
                pCharacteristic->notify();
            }
    }
    // counting seconds for awake time timer
    delay(1000); 
    counter++;

    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
}
// sudo chmod 666 /dev/ttyUSB0