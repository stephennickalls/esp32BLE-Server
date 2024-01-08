
#include <ArduinoJson.h>
#include <DHT.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// debug settings
#define DEBUG 1

#if DEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif
// end debug settings

// real time clock
RTC_DS3231 rtc;

// non-volatile storage
Preferences preferences;

int timeslot_offset = 0; // Offset in minutes
const int cycleDuration = 2; // Duration of each cycle in minutes should be 15min, set to 1 for testing
esp_sleep_wakeup_cause_t wakeup_reason; // what cause the wake up of this device

// sd card file
File dataFile;
const int CS = 5; // Chip Select pin for SD card comms
bool sdCardInitialized = false;

// defining temp and humidity sensor
#define DHTPIN 4  
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

int currentCycleNumber;
const int cycleNumber = 0;  // the cyclke number that the system makes data available on

bool dataTransmissionSuccess = false;

// statmachine enum
enum class programState: uint8_t {
  SET_RTC_TIME,
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
NimBLECharacteristic* pRTCTimeCharacteristic; 

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DATA_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TIME_CHARACTERISTIC_UUID "7eb5483e-36e1-4688-b7f5-ea07361b26a7"
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
        Serial.printf("MTU updated: %u for connection ID: %u\n", MTU, desc->conn_handle);
    };


};

/** Handler class for characteristic actions */
class DHTCallbacks: public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic){
      debugln("############ onRead called, client read data");
      // TODO: logic around success or fail
      dataTransmissionSuccess = true;
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
class RTCTimeCallbacks: public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic){
      debugln("############ RTCTimeCallbacks onRead called, client read data");

      // Check if RTC is running
      if (!rtc.begin()) {
          debugln("RTC Not Running");
          pCharacteristic->setValue("RTC Error");
          return;
      }

      // Read current time from RTC
      DateTime now = rtc.now();

      // Format time into a string (e.g., "YYYY-MM-DD HH:MM:SS")
      char timeStr[20];
      snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d", 
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

      // Set the characteristic's value to the formatted time
      pCharacteristic->setValue(timeStr);


      debug(pCharacteristic->getUUID().toString().c_str());
      debug(": onRead(), value: ");
      debugln(pCharacteristic->getValue().c_str());
    };

    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        debugln("############ RTCTimeCallbacks onWrite called, client wrote data");

        std::string writtenValue = pCharacteristic->getValue();
        debug("Written value: ");
        debugln(writtenValue.c_str());

        // Parse the time string (assuming format: "YYYY-MM-DDTHH:MM:SS")
        int year, month, day, hour, minute, second;
        if (sscanf(writtenValue.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
            // Set the RTC time
            rtc.adjust(DateTime(year, month, day, hour, minute, second));
            debugln("RTC time adjusted based on written value.");
            preferences.begin("beevibe_sensor_server", false); // Open NVS in RW mode with namespace "beevibe_sensor_server"
            preferences.putBool("rtcSet", true); // Set the flag to true once RTC is set
            preferences.end(); // Close NVS storage
        } else {
            debugln("Invalid time format received.");
        }
    };


};

/** Handler class for characteristic actions */
class ResponseCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
      debugln("############ onWrite called, response from client recived");
      // TODO: logic around success or fail
      dataTransmissionSuccess = true;
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
static RTCTimeCallbacks RTCTimeCallbacks;
static ResponseCallbacks responseCallbacks;


//################### setup ############################
void setup() {
  Serial.begin(115200);

  wakeup_reason = esp_sleep_get_wakeup_cause();

  // SETUP RTC MODULE
  if (! rtc.begin()) {
    debugln("RTC module is NOT found");
    Serial.flush();
    while (1);
  }


  // initialize dht sensor
  dht.begin();

  debugln("Initializing SD card...");

  while (!Serial) {;}

  if (!SD.begin(CS)) {
      debugln("SD card initialization failed!");
      sdCardInitialized = false;
  } else {
      debugln("SD card initialization done.");
      sdCardInitialized = true;
  }


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
  // Request a specific MTU size
  NimBLEDevice::setMTU(512);
  pServer->setCallbacks(new ServerCallbacks());



  NimBLEService* pDHTService = pServer->createService(SERVICE_UUID);

 pRTCTimeCharacteristic = pDHTService->createCharacteristic(
                                TIME_CHARACTERISTIC_UUID,
                                NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
                             );
    pRTCTimeCharacteristic->setCallbacks(&RTCTimeCallbacks);

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
    debug("rtc.now() = ");
    debugln(String(rtc.now().hour()) + ":" + String(rtc.now().minute()) + "." + String(rtc.now().second()));
    programStateMachine();
    // ReadFile("/data.txt");
    
} // end loop


//################### Finite state machine handles sensor operation, sleep and BLE comms ############################
void programStateMachine(){
    programState currentState;

    // initialize NVS storage
    preferences.begin("beevibe_sensor_server", false); // Open NVS in RW mode with namespace "beevibe_sensor_server"

    // Check if RTC is set
    bool rtcSet = preferences.getBool("rtcSet", false); // Default to false

    if (!rtcSet) {
      debugln("RTC is not set. Staying awake to set RTC.");
      programState currentState = programState::SET_RTC_TIME;
      
    } else {
      debugln("RTC is already set. Proceeding with regular operation.");
      programState currentState = programState::READ_SAVE_SLEEP;
    }

    preferences.end(); // Close NVS storage
    
    DateTime now = rtc.now();
    int calculatedCurrentCycleNumber = getCurrentCycleNumber(now);
        debug("################ the calculatedCycleNumber is: ");
        debugln(calculatedCurrentCycleNumber);


    static unsigned long timer = millis();
    static unsigned long MAXADVERTISINGTIME = 1*10000;

  

  switch (currentState) {
    case programState::SET_RTC_TIME:
    static unsigned long startTime;
    static const unsigned long TIMEOUT_PERIOD = 60 * 60 * 1000; // 60 minutes in milliseconds
    static const uint64_t DEEP_SLEEP_DURATION = 15 * 60 * 1000000; // 15 minutes in microseconds
    
    
    // Check if already advertising
    if (!pAdvertising->isAdvertising()) {
        debugln("about to start advertising so the current time can be read and adjusted if nessuary");
        pAdvertising->start();
        debugln("Started advertising.");
    } else {
        debugln("Already advertising.");
    }

    // Start the timer for timeout
    if (startTime == 0) {
        startTime = millis();
    }

    // Check for timeout
    if (millis() - startTime > TIMEOUT_PERIOD) {
        debugln("Timeout reached, going to deep sleep for 15 minutes");
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION);
        esp_deep_sleep_start();
    }

    delay(2000);
    
    break;
    // ################# CASE READ_SAVE_SLEEP ##################
    case programState::READ_SAVE_SLEEP:
    debugln("READ_SAVE_SLEEP CALLED");
    delay(1000);
      // debug("currentCycleNumber = ");
      // debugln(String(currentCycleNumber));
      // print state
      debugln("program state READ_SAVE_SLEEP called ");
      // condition to switch state
      if (calculatedCurrentCycleNumber == cycleNumber){
          currentState = programState::ADVERTISE_DATA_SLEEP;
          //set timer to current millis to be used in ADVERTISE_DATA_SLEEP state to trigger stop advertising
          timer = millis();
          break;
      }else{  
        debugln("Going to sleep");
        esp_sleep_enable_timer_wakeup((uint64_t)calculateSleepDuration(rtc.now()) * 1000000);
        esp_deep_sleep_start();
      };
      break;

    // ################# ADVERTISE_DATA_SLEEP ##################
    case programState::ADVERTISE_DATA_SLEEP:
    debugln("ADVERTISE_DATA_SLEEP");
    delay(1000);
      static bool haveDataAndAdvertising = false; // Static flag
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
        debugln("Setting up to sleep");
        haveDataAndAdvertising = false; // Reset the flag

        debugln("Going to sleep");
        // sets sleep timer to however many seconds there are untill the next quater hour (timeslot offset to be included).
        esp_sleep_enable_timer_wakeup((uint64_t)calculateSleepDuration(rtc.now()) * 1000000); 
        esp_deep_sleep_start();
      };
      debugln("between ifs");
      // If this flag is false then get the data and start the BLE
      // if it is true then we already have the data and started advertising in this cycle
      if (!haveDataAndAdvertising) {
        debugln("about to start advertising");
          pAdvertising->start();
          debugln("Advertising....");

          haveDataAndAdvertising = true; // Set the flag
          // TODO: get data from SD card
          // start the BLE advertising
          // String dataFromFile = readFile("/data.txt");
          // pDHTCharacteristic->setValue(dataFromFile);
          // debug(dataFromFile);
          debug("Characteristic value: ");
          debugln(pDHTCharacteristic->getValue().c_str());  
      }
      debugln("just before datatransmissionsuccess if");

      if(dataTransmissionSuccess){
          clearFile("/data.txt"); 
          dataTransmissionSuccess = false;
          debugln("Data transfered to client. Clearing data and going to sleep");
          timer = MAXADVERTISINGTIME + 1; // make timer larger that MAXADVERTISINGTIME so that the advertising will stop and programe will sleep
      }
    break;

  default:
    debugln("In the default....something terrible has happend");
  }
}





// ################# Some Helpers ##################

int getCurrentCycleNumber(const DateTime& currentTime) {
    int minutesSinceHourStart = currentTime.minute() % 8; // Get the minutes within the current 8-minute window
    return minutesSinceHourStart / 2; // Divide by 2 to get the cycle number (0 to 3)
}

int calculateSleepDuration(const DateTime& currentTime) {
    int minutesSinceHourStart = currentTime.minute() % 8;
    int secondsSinceCycleStart = (minutesSinceHourStart % 2) * 60 + currentTime.second();
    int sleepDuration = (2 * 60) - secondsSinceCycleStart;

    // Add 5 seconds as a buffer
    sleepDuration += 5;

    // Ensure sleep duration does not exceed the cycle length
    if (sleepDuration > (2 * 60)) {
        sleepDuration = (2 * 60);
    }

    return sleepDuration;
}

String formatDateTime(const DateTime& dt) {
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ", 
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  return String(buffer);
}

String getTimestamp(){
  DateTime now = rtc.now();
  String timestamp = formatDateTime(now);
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

void appendFile(const char * path, String data) {
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


String readFile(const char * path) {
    String fileContent = "";
    File dataFile = SD.open(path);

    if (dataFile) {
        debug("Reading file from: ");
        debugln(path);

        while (dataFile.available()) {
            String line = dataFile.readStringUntil('\n');
            fileContent += line;
            if (dataFile.available()) { // Check if there are more lines to read
                fileContent += ','; // Append a comma for each line except the last one
            }
        }
        dataFile.close();
    } else {
        debugln("Error opening file");
    }

    return fileContent;
}


void clearFile(const char * path) {
    File dataFile = SD.open(path, FILE_WRITE);
    
    if (dataFile) {
        debugln("Clearing: " + String(path));
        dataFile.close();
        debugln("Clear file completed.");
    } else {
        debugln("Error opening file for read: " + String(path));
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

