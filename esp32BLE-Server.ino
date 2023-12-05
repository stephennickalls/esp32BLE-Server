#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <DHT.h>
#include <EEPROM.h>

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
NimBLECharacteristic* pResponseCharacteristic = nullptr;
BLEDescriptor *pDescr;
// NimBLE2902 *pBLE2902;

// connection variables
bool deviceConnected = false;
bool oldDeviceConnected = false;

// timer, wake, and data transmission variables
bool transmitData = false;
// int advertisingTimeCounter = 0;
// const unsigned long MAXADVERTISINGTIME = 30;  // this will represent seconds;

// EEPROM variables
int currentCycleNumber;
const int maxCycleNumber = 4;  // 0 index so == 4
const int addr = 0; // memory address


#define DHTPIN 4  
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DATA_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RESPONSE_CHARACTERISTIC_UUID "aeb5483e-36e1-4688-b7f5-ea07361b26a9"



class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
      deviceConnected = true;
    };

    void onDisconnect(NimBLEServer* pServer) override {
      deviceConnected = false;
    }
};

class MyResponseCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        Serial.print("The response value is: ");
        Serial.println(value.c_str());
        if (strcmp(value.c_str(), "success") == 0)  {
            // Process the response here
            // Take action based on the response
            Serial.println("Going to sleep");
            // TODO: if success here then delete data and go back to sleep, maybe sync timer
            Serial.println("Data transmission was successful. Log or remove old data from SD card here");
            Serial.println("Setting currentCycle to 0 and going to sleep");
            currentCycleNumber = 0; 
            EEPROM.put(addr, currentCycleNumber);
            EEPROM.commit();
            esp_deep_sleep_start(); 
        }else {
          Serial.println("Error: Data transmission was unsuccessful");
          // TODO: take some other action when data transmission fails like log to sd card
        }
    }
};

void setEEPROM() {
  const int addr = 0;
  const int value = 0;
  const int maxValue = 4;
  // read the current value at this address
  EEPROM.get(addr, value);
  // check that the current value is valid, if not then set to 0
  if (value < 0 || value > maxValue) {
    EEPROM.put(addr, value);
    EEPROM.commit();
    Serial.println("EEPROM initialized with 1");
  }
}

String formatJson(float temp, float humidity) {
  // TODO: read any data from SD card and add to the json string
  // Prepare JSON data
  StaticJsonDocument<200> jsonDoc;
  //TODO: implement read current time from RTC
  jsonDoc["timestamp"] = "2023-10-18T02:45:00Z"; // Replace with actual timestamp
  jsonDoc["temp"] = temp;
  jsonDoc["humidity"] = humidity;

  char jsonString[200];
  serializeJson(jsonDoc, jsonString);
  return String(jsonString);

}

void advertiseDataTransmission(String jsonString){
  Serial.println("advertiseDataTransmission called");
  int advertisingTimeCounter = 0;
  const int MAXADVERTISINGTIME = 15;  // this will represent seconds;
  while (advertisingTimeCounter <= MAXADVERTISINGTIME){
      Serial.print("advertisingTimeCounter:");
      Serial.println(advertisingTimeCounter);
      while (deviceConnected && (advertisingTimeCounter <= MAXADVERTISINGTIME)) {
        pCharacteristic->setValue(jsonString.c_str());
        pCharacteristic->notify();
        Serial.print("Notify time passed (sec): ");
        Serial.println(advertisingTimeCounter);
        delay(1000); 
        advertisingTimeCounter++;
      }
      // counting seconds for awake time timer
      Serial.println("waiting for hub to connect");
      delay(1000); 
      advertisingTimeCounter++;
    }
    transmitData = false;
}


void setup() {
  Serial.begin(115200);
  dht.begin();
  EEPROM.begin(512);

  // our sensor will sleep for 
  esp_sleep_enable_timer_wakeup(1 * 10 * 1000000);

  // initialize EEPROM
  setEEPROM();

  // Initialize NimBLE
  NimBLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic for data
  pCharacteristic = pService->createCharacteristic(
                      DATA_CHARACTERISTIC_UUID,
                      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
                    );

  // Response Characteristic
  pResponseCharacteristic = pService->createCharacteristic(
                        RESPONSE_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE
                    );
  pResponseCharacteristic->setCallbacks(new MyResponseCallbacks());

  // Create a BLE Descriptor for DHT22 sensor data
  NimBLEDescriptor* pDescr = new NimBLEDescriptor("2901", NIMBLE_PROPERTY::READ, 0, pCharacteristic);
  pDescr->setValue("DHT22 Sensor Data");
  pCharacteristic->addDescriptor(pDescr);

  pService->start();

  // Start advertising
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

} // end setup


void loop() {
    
    // read cycle number from EEPROM
    EEPROM.get(addr, currentCycleNumber);

    Serial.print("Current cycle number with zero index is:");
    Serial.println(currentCycleNumber);

    // if this is the 4th cycle then transmit the data. 
    // otherwise we write to sd card and go back to sleep
    if (currentCycleNumber == maxCycleNumber ){
      transmitData = true;
      Serial.print("transmit data set to: ");
      Serial.println(transmitData);
    }

    // read data from the DHT sensor
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (isnan(humidity) || isnan(temp)) {
          Serial.println("Failed to read from DHT sensor!");
          // TODO: what happens here?
      } else {
          // TODO Read any data from SD card and add to the transmission
          // Prepare JSON data
          String jsonString = formatJson(temp, humidity);
          // Notify to connected clients
          if (transmitData) {
              // Start the ble service
            delay(500); // give the bluetooth stack the chance to get things ready
            pServer->startAdvertising(); // restart advertising
            Serial.println("BLE advertising started");
            // advertise the data to be sent;
            advertiseDataTransmission(jsonString);

          } else {
            currentCycleNumber = (currentCycleNumber + 1) % (maxCycleNumber + 1); // increment the number of awake cycles we have had and set to 0 if more than 3 
            // TODO: save data then sleep untill next cycle
            Serial.println("Saving data to SD card here");
            EEPROM.put(addr, currentCycleNumber);
            EEPROM.commit();
            esp_deep_sleep_start(); 
          }
        }
      currentCycleNumber  = (currentCycleNumber + 1) % (maxCycleNumber + 1); // increment the number of awake cycles we have had and set to 0 if more than 3 
      EEPROM.put(addr, currentCycleNumber );
      EEPROM.commit();

          // handle connection/disconnection
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