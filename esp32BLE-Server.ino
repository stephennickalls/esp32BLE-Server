#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic_temp = NULL;
BLEDescriptor *pDescr_temp;
BLE2902 *pBLE2902_temp;

BLECharacteristic* pCharacteristic_humidity = NULL;
BLEDescriptor *pDescr_humidity;
BLE2902 *pBLE2902_humidity;

bool deviceConnected = false;
bool oldDeviceConnected = false;

#define DHTPIN 4  
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TEMP_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define HUMIDITY_CHARACTERISTIC_UUID "aeb5483e-36e1-4688-b7f5-ea07361b26a9"


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

  // Create the BLE Device
  BLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic_temp = pService->createCharacteristic(
                      TEMP_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
                      
                    );                   

    // Create a BLE Characteristic
  pCharacteristic_humidity = pService->createCharacteristic(
                      HUMIDITY_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
                      
                    ); 
  // Create a BLE Descriptor
  // Add temp
  pDescr_temp = new BLEDescriptor((uint16_t)0x2901);
  pDescr_temp->setValue("Tempertature");
  pCharacteristic_temp->addDescriptor(pDescr_temp);

  pBLE2902_temp = new BLE2902();
  pBLE2902_temp->setNotifications(true);
  pCharacteristic_temp->addDescriptor(pBLE2902_temp);

  // Add humidity
  pDescr_humidity = new BLEDescriptor((uint16_t)0x2901);
  pDescr_humidity->setValue("Humidity");
  pCharacteristic_humidity->addDescriptor(pDescr_humidity);

  pBLE2902_humidity = new BLE2902();
  pBLE2902_humidity->setNotifications(true);
  pCharacteristic_humidity->addDescriptor(pBLE2902_humidity); 


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

    // Wake up and read data from the DHT sensor
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp)) {
        Serial.println("Failed to read from DHT sensor!");
        // Go to deep sleep again
        // esp_sleep_enable_timer_wakeup(sleepInterval);
        // esp_deep_sleep_start();
    }
    // notify changed value
    if (deviceConnected) {
        pCharacteristic_temp->setValue(temp);
        pCharacteristic_temp->notify();
        pCharacteristic_humidity->setValue(humidity);
        pCharacteristic_humidity->notify();
        delay(1000);
    }
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