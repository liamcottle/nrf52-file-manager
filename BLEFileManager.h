#include <bluefruit.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <ArduinoJson.h>

using namespace Adafruit_LittleFS_Namespace;

class BLEFileManager {
public:
    BLEFileManager();
    void begin();

private:
    static BLEService fileManagerService;
    static BLECharacteristic listDirectoryCharacteristic;
    static BLECharacteristic deleteFileCharacteristic;
    static BLECharacteristic readFileCharacteristic;
    static void listDirectoryWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void deleteFileWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void readFileWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
};

BLEService BLEFileManager::fileManagerService = BLEService(BLEUuid("6c69616d-636f-7474-6c65-66696c657300"));
BLECharacteristic BLEFileManager::listDirectoryCharacteristic = BLECharacteristic(BLEUuid("6c69616d-636f-7474-6c65-66696c657301"));
BLECharacteristic BLEFileManager::deleteFileCharacteristic = BLECharacteristic(BLEUuid("6c69616d-636f-7474-6c65-66696c657302"));
BLECharacteristic BLEFileManager::readFileCharacteristic = BLECharacteristic(BLEUuid("6c69616d-636f-7474-6c65-66696c657303"));

BLEFileManager::BLEFileManager() {}

void BLEFileManager::begin() {

    Serial.println("BLEFileManager::begin");

    // all config functions must be called before begin()
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX); // config the peripheral connection with maximum bandwidth
    Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16); // more SRAM required by SoftDevice

    // init ble
    Bluefruit.begin();
    Serial.println("BLE Initialized");

    // set ble device name
    Bluefruit.setName("nRF52 File Manager");

    // set max power. accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
    Bluefruit.setTxPower(4);

    // start ble file manager service
    fileManagerService.begin();
    Serial.println("BLE Service Started");

    // list directory characteristic
    listDirectoryCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    listDirectoryCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    listDirectoryCharacteristic.setMaxLen(512);
    listDirectoryCharacteristic.setWriteCallback(listDirectoryWriteCallback);
    listDirectoryCharacteristic.begin();

    // delete file characteristic
    deleteFileCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    deleteFileCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    deleteFileCharacteristic.setMaxLen(512);
    deleteFileCharacteristic.setWriteCallback(deleteFileWriteCallback);
    deleteFileCharacteristic.begin();

    // read file characteristic
    readFileCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    readFileCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    readFileCharacteristic.setMaxLen(512);
    readFileCharacteristic.setWriteCallback(readFileWriteCallback);
    readFileCharacteristic.begin();

    // setup advertising
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(fileManagerService);
    Bluefruit.ScanResponse.addName();
    Bluefruit.ScanResponse.addTxPower();

    /* Start Advertising
     * - Enable auto advertising if disconnected
     * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     * - Timeout for fast mode is 30 seconds
     * - Start(timeout) with timeout = 0 will advertise forever (until connected)
     *
     * For recommended advertising interval
     * https://developer.apple.com/library/content/qa/qa1931/_index.html
     */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30); // number of seconds in fast mode
    Bluefruit.Advertising.start(0); // 0 = Don't stop advertising after n seconds
    Serial.println("BLE Advertising Started");

}

void BLEFileManager::listDirectoryWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {

    // get path from provided data
    String path = String((char*)data).substring(0, len);
    Serial.println("listDirectoryWriteCallback: " + path);

    // get files in provided path
    File root = InternalFS.open(path.c_str());
    File file = root.openNextFile();
    while(file){

        Serial.println(file.name());

        // create json object
        StaticJsonDocument<256> doc;
        doc["name"] = String(file.name());
        doc["size"] = file.size();
        doc["is_directory"] = file.isDirectory();

        // convert json to string
        char json_string[256];
        serializeJson(doc, json_string);

        // send json over ble characteristic notification
        listDirectoryCharacteristic.notify(json_string);

        // move to next file
        file = root.openNextFile();

    }

    // notify that we are done
    listDirectoryCharacteristic.notify("done");

}

void BLEFileManager::deleteFileWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {

    // get path from provided data
    String path = String((char*)data).substring(0, len);
    Serial.println("deleteFileWriteCallback: " + path);

    File file = InternalFS.open(path.c_str());
    if(file.isDirectory()){
        InternalFS.rmdir_r(path.c_str());
    } else {
        InternalFS.remove(path.c_str());
    }

    // notify that we are done
    listDirectoryCharacteristic.notify("done");

}

void BLEFileManager::readFileWriteCallback(uint16_t handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {

    // get path from provided data
    String path = String((char*)data).substring(0, len);
    Serial.println("readFileWriteCallback: " + path);

    // open file for reading
    File file(InternalFS);
    if(!file.open(path.c_str(), FILE_O_READ)){

        // notify that we failed to open file
        readFileCharacteristic.notify32(-1);
        return;

    }

    // notify how many bytes will be sent
    readFileCharacteristic.notify32(file.size());

    // send all file bytes to user
    while(file.available()){

        // send in chunks of up to 256 bytes
        char buffer[256] = {};
        int readLength = file.readBytes(buffer, sizeof(buffer));

        // send bytes to user
        readFileCharacteristic.notify(buffer, readLength);

    }

    // close file
    file.close();

}
