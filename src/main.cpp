/**
 * @file main.cpp
 * @brief Sistem Distribuit de Monitorizare și Control (Master Unitbv)
 * @details Implementare Dual-Core ESP32 cu FreeRTOS, BLE și Control PWM
 * @author Alexandru Gherghe
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- 1. Configurare Hardware ---
#define SENSOR_PIN  4   // Pin GPIO pentru DS18B20 (Data)
#define FAN_PIN     5   // Pin GPIO pentru control ventilator (MOSFET/Baza Tranzistor)

// --- 2. Parametri PWM (Best Practice: Folosire Timer Hardware) ---
// Folosim perifericul LEDC al ESP32 pentru a nu ocupa procesorul cu generarea semnalului
const int pwmFreq = 5000;       // Frecvență 5 KHz (standard pentru ventilatoare)
const int pwmChannel = 0;       // Canalul 0 (ESP32 are 16 canale)
const int pwmResolution = 8;    // Rezoluție 8 biți (valori 0-255)

// --- 3. Variabile Globale și Obiecte ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

float global_threshold = 25.0; // Pragul implicit de temperatură
bool deviceConnected = false;  // Flag atomic pentru starea conexiunii

// --- 4. FreeRTOS Handles (Tehnică: Sincronizare Thread-Safe) ---
QueueHandle_t tempQueue;       // Coadă: Senzor -> BLE (evită variabilele globale volatile)
QueueHandle_t thresholdQueue;  // Coadă: BLE -> Senzor
TaskHandle_t hTaskSenzor;
TaskHandle_t hTaskBLE;

// --- 5. Configurare BLE UUIDs ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TEMP_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define THRESHOLD_CHAR_UUID "c5b2c86a-529a-4e24-878e-a2b0c03c9c6c"

BLECharacteristic *pTempCharacteristic;
BLECharacteristic *pThresholdCharacteristic;

// --- Callbacks pentru Starea Conexiunii ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      // Best Practice: Repornim reclama imediat pentru a permite reconectarea automată
      BLEDevice::startAdvertising(); 
    }
};

// --- Callbacks pentru Scrierea Datelor (Din Telefon -> ESP32) ---
class ThresholdCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            // Conversie sigură din string în float
            float newThreshold = (float)atof(value.c_str());
            
            // Tehnică PRO: Nu blocăm callback-ul BLE cu logică lentă.
            // Trimitem doar valoarea în coadă și lăsăm task-ul de control să o proceseze.
            xQueueSend(thresholdQueue, &newThreshold, 0);
        }
    }
};

// ================================================================
// TASK 1: SENZOR ȘI CONTROL (Rulează pe Core 0 - App Core)
// Acest task este "Hard Real-Time" - nu trebuie întrerupt de WiFi/BLE
// ================================================================
void taskSenzorControl(void *pvParameters) {
    float currentTemp;
    float receivedThreshold;

    // Configurare Timer PWM (Hardware)
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(FAN_PIN, pwmChannel);

    for (;;) {
        // A. Verificăm asincron dacă am primit un nou prag via BLE
        if (xQueueReceive(thresholdQueue, &receivedThreshold, 0) == pdPASS) {
            global_threshold = receivedThreshold;
            
            // Actualizăm valoarea vizibilă în BLE pentru confirmare vizuală pe telefon
            char threshStr[8];
            dtostrf(global_threshold, 6, 2, threshStr);
            pThresholdCharacteristic->setValue(threshStr);
        }

        // B. Citire Senzor (Operațiune lentă ~750ms)
        sensors.requestTemperatures(); 
        currentTemp = sensors.getTempCByIndex(0);

        // Validare citire (evităm valori eronate gen -127)
        if(currentTemp == DEVICE_DISCONNECTED_C) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; 
        }

        // C. Logica Control Ventilator (PWM)
        // Implementare Histerezis Simplu cu ieșire PWM
        if (currentTemp > global_threshold) {
            // Pornire Ventilator la putere maximă (255)
            // Aici se poate implementa ușor un PID variind duty cycle-ul
            ledcWrite(pwmChannel, 255); 
        } else {
            // Oprire ventilator
            ledcWrite(pwmChannel, 0);
        }

        // D. Trimite temperatura spre BLE (Fără să blocheze dacă coada e plină)
        xQueueSend(tempQueue, &currentTemp, pdMS_TO_TICKS(100));

        // E. Pauză între citiri (Eliberează procesorul pentru alte task-uri IDLE)
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ================================================================
// TASK 2: COMUNICAȚIE BLE (Rulează pe Core 1 - Pro Core)
// Acest task gestionează stiva radio și evenimentele asincrone
// ================================================================
void taskBLE(void *pvParameters) {
    float tempToNotify;

    for (;;) {
        // Așteaptă blocant până primește date în coadă (Eficient energetic)
        if (xQueueReceive(tempQueue, &tempToNotify, portMAX_DELAY) == pdPASS) {
            
            // Trimitem notificare doar dacă avem un client conectat
            if (deviceConnected) {
                char tempStr[8];
                dtostrf(tempToNotify, 6, 2, tempStr); // Formatare: 2 zecimale
                
                pTempCharacteristic->setValue(tempStr);
                pTempCharacteristic->notify(); // Push notification către telefon
            }
        }
    }
}

// ================================================================
// SETUP - Inițializare Sistem
// ================================================================
void setup() {
    Serial.begin(115200);
    
    // 1. Init Senzor
    sensors.begin();

    // 2. Init Cozi FreeRTOS
    tempQueue = xQueueCreate(5, sizeof(float));
    thresholdQueue = xQueueCreate(2, sizeof(float));

    // 3. Init BLE
    BLEDevice::init("ESP32-Master-Project");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Configurare Caracteristici
    pTempCharacteristic = pService->createCharacteristic(
        TEMP_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pTempCharacteristic->addDescriptor(new BLE2902());

    pThresholdCharacteristic = pService->createCharacteristic(
        THRESHOLD_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    pThresholdCharacteristic->setCallbacks(new ThresholdCallbacks());

    // Start Serviciu & Advertising
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // Compatibilitate iPhone
    BLEDevice::startAdvertising();

    // 4. Init Task-uri (Pinned to Core - Arhitectură Dual Core)
    // Task Senzor -> Core 0
    xTaskCreatePinnedToCore(taskSenzorControl, "SenzorCtrl", 4096, NULL, 1, &hTaskSenzor, 0);
    // Task BLE -> Core 1
    xTaskCreatePinnedToCore(taskBLE, "BLECom", 4096, NULL, 1, &hTaskBLE, 1);
}

void loop() {
    // Loop rămâne gol - totul este gestionat de Task-urile FreeRTOS
    // Putem pune microcontrolerul în Light Sleep aici pentru economie
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}
