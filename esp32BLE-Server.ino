#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <EEPROM.h>


BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
BLEDescriptor *pDescr;
BLE2902 *pBLE2902;


// connection variables
bool deviceConnected = false;
bool oldDeviceConnected = false;

// timer, wake, and data transmission variables
bool transmitData = false;
int awakeTimeCounter = 0;
const unsigned long AWAKETIME = 1 * 30 * 1000;

// EEPROM variables
int currentCycleNumber;
int awakeCycleNumber;
const int maxAwakeCycleNumber = 4;
const int addr = 0; // memory address




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
    Serial.println("EEPROM initialized with 0");
  }
}

void connectDisconnect(){
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


void setup() {
  Serial.begin(115200);
  dht.begin();
  EEPROM.begin(512);

  // our sensor will sleep for 
  esp_sleep_enable_timer_wakeup(1 * 10 * 1000000);

  // initialize EEPROM
  setEEPROM();


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

}

void loop() {
    Serial.println("in loop");

    // read cycle number from EEPROM
    EEPROM.get(addr, currentCycleNumber);

    Serial.print("Current cycle number is:");
    Serial.println(currentCycleNumber);

    // if this is the 4th cycle then transmit the data. 
    // otherwise we write to sd card and go back to sleep
    if (currentCycleNumber == 4){
      transmitData = true;
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
          StaticJsonDocument<200> jsonDoc;
          jsonDoc["timestamp"] = "2023-10-18T02:45:00Z"; // Replace with actual timestamp
          jsonDoc["temp"] = temp;
          jsonDoc["humidity"] = humidity;
          char jsonString[200];
          serializeJson(jsonDoc, jsonString);
          // Notify to connected clients
          if (transmitData) {
              // Start the ble service
            pService->start();

            // Start advertising
            BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
            pAdvertising->addServiceUUID(SERVICE_UUID);
            pAdvertising->setScanResponse(false);
            pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
            BLEDevice::startAdvertising();
            Serial.println("Waiting a client connection to notify...");
            
            while (awakeTimeCounter >= AWAKETIME){
              pCharacteristic->setValue(jsonString);
              pCharacteristic->notify();
              // counting seconds for awake time timer
              // TODO: implement a propper timer.
              delay(1000); 
              awakeTimeCounter++;
              Serial.print("Notify time passed (sec): ");
              Serial.println(awakeTimeCounter);
            }
            // TODO: if success here then delete data and go back to sleep, maybe sync timer
            Serial.println("delete data from SD card here");
            awakeCycleNumber = 0;  
            // write awake cule number to EEPROM
            EEPROM.put(addr, awakeCycleNumber);
            EEPROM.commit();
            esp_deep_sleep_start(); 

          } else {
            awakeCycleNumber = (currentCycleNumber + 1) % (maxAwakeCycleNumber + 1); // increment the number of awake cycles we have had and set to 0 if more than 4 
            // TODO: save data then sleep untill next cycle
            Serial.println("Saving data to SD card here");
            EEPROM.put(addr, awakeCycleNumber);
            EEPROM.commit();
            esp_deep_sleep_start(); 
          }
        }
      Serial.println("skipped all the ifs and went right to the next cycle grrrr");
      awakeCycleNumber = (currentCycleNumber + 1) % (maxAwakeCycleNumber + 1); // increment the number of awake cycles we have had and set to 0 if more than 4 
      EEPROM.put(addr, awakeCycleNumber);
      EEPROM.commit();

    connectDisconnect();


}
// sudo chmod 666 /dev/ttyUSB0