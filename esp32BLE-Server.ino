
#include <ArduinoJson.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ESP32Time.h>
#include <NimBLEDevice.h>
#include <SPI.h>
#include <SD.h>

File dataFile;
const int CS = 5; // Chip Select pin for SD card comms

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



//ESP32Time rtc;
ESP32Time rtc(3600);

// EEPROM variables
int currentCycleNumber;
const int maxCycleNumber = 3;  
const int addr = 0; // memory address



enum class programState: uint8_t {
  TIME_CONFIG,
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

NimBLECharacteristic* pDHTCharacteristic;  // Global declaration

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
class DHTCallbacks: public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic){
        debug(pCharacteristic->getUUID().toString().c_str());
        debug(": onRead(), value: ");
        debugln(pCharacteristic->getValue().c_str());
    };
    /** Called before notification or indication is sent,
     *  the value can be changed here before sending if desired.
     */
    void onNotify(NimBLECharacteristic* pCharacteristic) {
        debugln("Sending notification to clients");
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

/** Handler class for characteristic actions */
class ResponseCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        debug(pCharacteristic->getUUID().toString().c_str());
        debug(": onWrite(), value: ");
        debugln(pCharacteristic->getValue().c_str());
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
static DescriptorCallbacks dscriptionCallbacks;
static DHTCallbacks DHTCallbacks;
static ResponseCallbacks responseCallbacks;


//################### setup ############################
void setup() {
  Serial.begin(115200);
  // TODO: implement set time on power up
  rtc.setTime(30, 24, 15, 17, 1, 2021);  // 17th Jan 2021 15:24:30
  EEPROM.begin(512);
  // initialize EEPROM   
  setEEPROM();
  dht.begin();

  while (!Serial) { ; }  // wait for serial port to connect. Needed for native USB port only
  debugln("Initializing SD card...");
  if (!SD.begin(CS)) {
    debugln("initialization failed!");
    return;
  }
  Serial.println("initialization done.");

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

 pDHTCharacteristic = pDHTService->createCharacteristic(
                                              DATA_CHARACTERISTIC_UUID,
                                              NIMBLE_PROPERTY::READ |
                                              NIMBLE_PROPERTY::NOTIFY
                                            );
  pDHTCharacteristic->setCallbacks(&DHTCallbacks);

  /** Custom descriptor: Arguments are UUID, Properties, max length in bytes of the value */
  NimBLEDescriptor* pDHTSensorDescriptor = pDHTCharacteristic->createDescriptor(
                                              DESCRIPTION_UUID,
                                              NIMBLE_PROPERTY::READ,
                                              50
                                            );
  pDHTSensorDescriptor->setValue("DHT22 temp and humidity readings");
  pDHTSensorDescriptor->setCallbacks(&dscriptionCallbacks);

  NimBLECharacteristic* pDHTResponseCharacteristic = pDHTService->createCharacteristic(
                                          RESPONSE_CHARACTERISTIC_UUID,
                                          NIMBLE_PROPERTY::READ |
                                          NIMBLE_PROPERTY::WRITE
                                        );

  pDHTResponseCharacteristic->setValue("Test value from response characteristic");
  pDHTResponseCharacteristic->setCallbacks(&responseCallbacks);

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
    debug("################ wakeup reason: ");
    debugln(esp_sleep_get_wakeup_cause());
    programStateMachine();
    ReadFile("/data.txt");
    
} // end loop


//################### Finite state machine handles sensor operation, sleep and BLE comms ############################
void programStateMachine(){

  // read cycle number from EEPROM
  EEPROM.get(addr, currentCycleNumber);

  static unsigned long timer = millis();
  static unsigned long MAXADVERTISINGTIME = 1*10000;

  // track current state
  static programState currentState = programState::READ_SAVE_SLEEP;

  switch (currentState) {
    // ################# CASE READ_SAVE_SLEEP ##################
    case programState::READ_SAVE_SLEEP:
    delay(1000);
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
        
        String data = getDataReading();
        WriteFile("/data.txt", data);

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
    delay(1000);
      static bool getDataAndAdvertise = false; // Static flag
      debug("currentCycleNumber = ");
      debugln(String(currentCycleNumber));  

      debug("MAXADVERTISINGTIME = ");
      debugln(String(MAXADVERTISINGTIME));
      debug("timer = ");
      debugln(String(timer));
      debug("millis = ");
      debugln(String(millis()));

      // advertises for 1 minute then sleeps for 1 minute to maintain the 2 mins for each proccess 1 full proccess = 4 cycles (this version has 2 min cycles for a full process of 8 mins)
      if (millis() - timer >= MAXADVERTISINGTIME){ 
        getDataAndAdvertise = false; // Reset the flag
        currentCycleNumber = 0;
        EEPROM.put(addr, currentCycleNumber);
        EEPROM.commit();
        debugln("Going to sleep");
        // set deep sleep timer  
        esp_sleep_enable_timer_wakeup(1 * 10 * 1000000); // 1 minute
        esp_deep_sleep_start();
      };
      // debug("program state ADVERTISE_DATA_SLEEP called ");

      // If this flag is false then get the data and start the BLE
      // if it is true then we already have the data and started advertising in this cycle
      if (!getDataAndAdvertise) {
          pAdvertising->start();
          debugln("Advertising....");

          getDataAndAdvertise = true; // Set the flag
          // TODO: get data from SD card
          // get sensor data from sensor
          String data = getDataReading();
          // TODO: add this read to SD card data
          // start the BLE advertising
          pDHTCharacteristic->setValue(data);
          debug("Characteristic value: ");
          debugln(pDHTCharacteristic->getValue().c_str());
      }
    break;

  default:
    debugln("In the default....something terrible has happend");
  }
}





// ################# Some Helpers ##################

String getTimestamp(){
  String timestamp = rtc.getTime("%Y-%m-%dT%H:%M:%SZ");
  // Check if the timestamp is not empty or invalid
  if (timestamp.length() > 0 && timestamp != "1970-01-01T00:00:00Z") {
      debugln("Current time: " + timestamp);
      return timestamp;
  } else {
      debugln("Error: Invalid time");
  }
  return "1970-01-01T00:00:00Z";

}

String getDataReading(){
  // timestamp
  String timestamp = getTimestamp();
  // read sensor
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temp) || isnan(humidity)){
    // TODO: do something here on read fail
    debugln("Failed to read DHT sensor");
  };
  
  String jsonString = formatJson(timestamp, temp, humidity);
  return jsonString;

}

void WriteFile(const char * path, String data) {
    File dataFile = SD.open(path, FILE_APPEND);
    
    if (dataFile) {
        debugln("Writing to: " + String(path));
        dataFile.println(data);
        dataFile.close();
        debugln("Write completed.");
    } else {
        debugln("Error opening file: " + String(path));
    }
}


void ReadFile(const char * path){
  // open the file for reading:
  dataFile = SD.open(path);
  if (dataFile) {
     debug("Reading file from: ");
     debugln(path);
     // read from the file until there's nothing else in it:
    while (dataFile.available()) {
      Serial.write(dataFile.read());
    }
    dataFile.close(); // close the file:
  } 
  else {
    // if the file didn't open, print an error:
    debugln("error opening file");
  }
}

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

String formatJson(String timestamp, float temp, float humidity) {
  // Prepare JSON data
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["timestamp"] = timestamp; 
  jsonDoc["temp"] = temp;
  jsonDoc["humidity"] = humidity;

  char jsonString[200];
  serializeJson(jsonDoc, jsonString);
  return String(jsonString);

}

