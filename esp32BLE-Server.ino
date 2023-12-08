
#include <ArduinoJson.h>
#include <DHT.h>
#include <EEPROM.h>
#include <NimBLEDevice.h>

#define DHTPIN 4  
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define DEBUG 1

#if DEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

// EEPROM variables
int currentCycleNumber;
const int maxCycleNumber = 3;  
const int addr = 0; // memory address

enum class programState: uint8_t {
  READ_SAVE_SLEEP,
  ADVERTISE_DATA_SLEEP
};

// Function to convert programState to string
const char* programStateToString(programState state) {
  switch (state) {
    case programState::READ_SAVE_SLEEP: return "READ_SAVE_SLEEP";
    case programState::ADVERTISE_DATA_SLEEP: return "ADVERTISE_DATA_SLEEP";
    default: return "UNKNOWN";
  }
};

static NimBLEServer* pServer;
NimBLEAdvertising* pAdvertising = nullptr;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DATA_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RESPONSE_CHARACTERISTIC_UUID "aeb5483e-36e1-4688-b7f5-ea07361b26a9"
#define DESCRIPTION_UUID "3d069557-f145-4152-9fd8-8d2a61864949"


//################### BLE callbacks etc. ############################

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        debugln("Client connected");
        debugln("Multi-connect support: start advertising");
        NimBLEDevice::startAdvertising();
    };
    /** Alternative onConnect() method to extract details of the connection.
     *  See: src/ble_gap.h for the details of the ble_gap_conn_desc struct.
     */
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        debug("Client address: ");
        debugln(NimBLEAddress(desc->peer_ota_addr).toString().c_str());
        /** We can use the connection handle here to ask for different connection parameters.
         *  Args: connection handle, min connection interval, max connection interval
         *  latency, supervision timeout.
         *  Units; Min/Max Intervals: 1.25 millisecond increments.
         *  Latency: number of intervals allowed to skip.
         *  Timeout: 10 millisecond increments, try for 5x interval time for best results.
         */
        pServer->updateConnParams(desc->conn_handle, 24, 48, 0, 60);
    };
    void onDisconnect(NimBLEServer* pServer) {
        debugln("Client disconnected - start advertising");
        NimBLEDevice::startAdvertising();
    };
    void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) {
        // debugf("MTU updated: %u for connection ID: %u\n", MTU, desc->conn_handle);
    };
};

/** Handler class for characteristic actions */
class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic){
        debug(pCharacteristic->getUUID().toString().c_str());
        debug(": onRead(), value: ");
        debugln(pCharacteristic->getValue().c_str());
    };

    void onWrite(NimBLECharacteristic* pCharacteristic) {
        debug(pCharacteristic->getUUID().toString().c_str());
        debug(": onWrite(), value: ");
        debugln(pCharacteristic->getValue().c_str());
    };
    /** Called before notification or indication is sent,
     *  the value can be changed here before sending if desired.
     */
    void onNotify(NimBLECharacteristic* pCharacteristic) {
        debugln("Sending notification to clients");
    };


    /** The status returned in status is defined in NimBLECharacteristic.h.
     *  The value returned in code is the NimBLE host return code.
     */
    void onStatus(NimBLECharacteristic* pCharacteristic, Status status, int code) {
        String str = ("Notification/Indication status code: ");
        str += status;
        str += ", return code: ";
        str += code;
        str += ", ";
        str += NimBLEUtils::returnCodeToString(code);
        debugln(str);
    };

    void onSubscribe(NimBLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc, uint16_t subValue) {
        String str = "Client ID: ";
        str += desc->conn_handle;
        str += " Address: ";
        str += std::string(NimBLEAddress(desc->peer_ota_addr)).c_str();
        if(subValue == 0) {
            str += " Unsubscribed to ";
        }else if(subValue == 1) {
            str += " Subscribed to notfications for ";
        } else if(subValue == 2) {
            str += " Subscribed to indications for ";
        } else if(subValue == 3) {
            str += " Subscribed to notifications and indications for ";
        }
        str += std::string(pCharacteristic->getUUID()).c_str();

        debugln(str);
    };
};

/** Handler class for descriptor actions */
class DescriptorCallbacks : public NimBLEDescriptorCallbacks {

    void onRead(NimBLEDescriptor* pDescriptor) {
        debug(pDescriptor->getUUID().toString().c_str());
        debugln(" Descriptor read");
    };
};


/** Define callback instances globally to use for multiple Charateristics \ Descriptors */
static DescriptorCallbacks dscCallbacks;
static CharacteristicCallbacks chrCallbacks;


//################### setup ############################
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  // initialize EEPROM   
  setEEPROM();
  dht.begin();

  // set deep sleep timer  
  // esp_sleep_enable_timer_wakeup(1 * 10 * 1000000);

  debugln("Starting NimBLE Server");
  /** sets device name */
  NimBLEDevice::init("Beevibe DHT Server");

    /** Optional: set the transmit power, default is 3db */
#ifdef ESP_PLATFORM
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
#else
    NimBLEDevice::setPower(9); /** +9db */
#endif


  NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());


  NimBLEService* pDHTService = pServer->createService(SERVICE_UUID);

  NimBLECharacteristic* pDHTCharacteristic = pDHTService->createCharacteristic(
                                              DATA_CHARACTERISTIC_UUID,
                                              NIMBLE_PROPERTY::READ |
                                              NIMBLE_PROPERTY::NOTIFY
                                            );
  pDHTCharacteristic->setCallbacks(&chrCallbacks);

  /** Custom descriptor: Arguments are UUID, Properties, max length in bytes of the value */
  NimBLEDescriptor* pDHTSensorDescriptor = pDHTCharacteristic->createDescriptor(
                                              DESCRIPTION_UUID,
                                              NIMBLE_PROPERTY::READ,
                                              50
                                            );
  pDHTSensorDescriptor->setValue("DHT22 temp and humidity readings");
  pDHTSensorDescriptor->setCallbacks(&dscCallbacks);

  NimBLECharacteristic* pDHTResponseCharacteristic = pDHTService->createCharacteristic(
                                          RESPONSE_CHARACTERISTIC_UUID,
                                          NIMBLE_PROPERTY::READ |
                                          NIMBLE_PROPERTY::WRITE
                                        );

  pDHTResponseCharacteristic->setValue("Test value from response characteristic");
  pDHTResponseCharacteristic->setCallbacks(&chrCallbacks);

  /** Start the services when finished creating all Characteristics and Descriptors */
  pDHTService->start();

  // NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising = NimBLEDevice::getAdvertising();
  /** Add the services to the advertisment data **/
  pAdvertising->addServiceUUID(pDHTService->getUUID());
  /** If your device is battery powered you may consider setting scan response
    *  to false as it will extend battery life at the expense of less data sent.
    */
  pAdvertising->setScanResponse(true);

  debugln("Setup has finished");
}

//################### LOOP Nothing much in here, just calls state machine ############################
void loop() {
    programStateMachine();
} // end loop


//################### Finite state machine handles sensor operation, sleep and BLE comms ############################
void programStateMachine(){

  // read cycle number from EEPROM
  EEPROM.get(addr, currentCycleNumber);

  static unsigned long timer = millis();
  static unsigned long MAXADVERTISINGTIME = 10000;

  float temp;
  float humidity;

  // track current state
  static programState currentState = programState::READ_SAVE_SLEEP;

  switch (currentState) {
    // ################# CASE READ_SAVE_SLEEP ##################
    case programState::READ_SAVE_SLEEP:
      debug("currentCycleNumber = ");
      debugln(String(currentCycleNumber));
      // print state
      debugln("program state READ_SAVE_SLEEP called ");
      // condition to switch state
      if (currentCycleNumber == maxCycleNumber){
          currentState = programState::ADVERTISE_DATA_SLEEP;
          //set timer to current millis to be used in ADVERTISE_DATA_SLEEP state to trigger stop advertising
          timer = millis();
          break;
      }else{
        // read sensor
        temp = dht.readTemperature();
        humidity = dht.readHumidity();

        if (isnan(temp) || isnan(humidity)){
          debugln("Failed to read DHT sensor");
          break;
        };

        String jsonString = formatJson(temp, humidity);
        // TODO: save data then sleep untill next cycle
        debugln("Saving data to SD card here");
        delay(2000);
        currentCycleNumber++;
        EEPROM.put(addr, currentCycleNumber);
        EEPROM.commit();
        // set deep sleep timer  
        debugln("Going to sleep");
        esp_sleep_enable_timer_wakeup(1 * 10 * 1000000);
        esp_deep_sleep_start();
      };
      break;
    // ################# ADVERTISE_DATA_SLEEP ##################
    case programState::ADVERTISE_DATA_SLEEP:
      debug("currentCycleNumber = ");
      debugln(String(currentCycleNumber));  

      debug("MAXADVERTISINGTIME = ");
      debugln(String(MAXADVERTISINGTIME));
      debug("timer = ");
      debugln(String(timer));
      debug("millis = ");
      debugln(String(millis()));


      if (millis() - timer >= MAXADVERTISINGTIME){
        currentCycleNumber = 0;
        EEPROM.put(addr, currentCycleNumber);
        EEPROM.commit();
        debugln("Going to sleep");
        // set deep sleep timer  
        esp_sleep_enable_timer_wakeup(3 * 10 * 1000000);
        esp_deep_sleep_start();
      };
      debug("program state ADVERTISE_DATA_SLEEP called ");

      // read sensor
      temp = dht.readTemperature();
      humidity = dht.readHumidity();

      if (isnan(temp) || isnan(humidity)){
        debugln("Failed to read DHT sensor");
        break;
      };
      pAdvertising->start();
      debugln("Advertising....");
      delay(2000);

      if(pServer->getConnectedCount()) {
        NimBLEService* pSvc = pServer->getServiceByUUID(SERVICE_UUID);
        if(pSvc) {
            NimBLECharacteristic* pChr = pSvc->getCharacteristic(DATA_CHARACTERISTIC_UUID);
            if(pChr) {
                pChr->setValue(String(temp));
                pChr->notify(true);
            }
        }
    }

    break;

  default:
    debugln("In the default....something terrible has happend");
  }
}

// ################# Some Helpers ##################
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
    debugln("EEPROM initialized with 1");
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

