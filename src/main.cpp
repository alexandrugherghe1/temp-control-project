/**
 * @file main.cpp
 * @brief Sistem Distribuit de Monitorizare și Control (Master Unitbv)
 * @details Implementare Dual-Core ESP32 cu FreeRTOS, BLE și Control Proporțional (PWM)
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
#define SENSOR_PIN  4   // Pin GPIO pentru DS18B20
#define FAN_PIN     5   // Pin GPIO pentru MOSFET Ventilator

// --- 2. Parametri PWM ---
const int pwmFreq = 5000;
const int pwmChannel = 0;
const int pwmResolution = 8;

// --- 3. Variabile Globale ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

float global_threshold = 25.0; // Prag temperatura (Setpoint)
bool deviceConnected = false;

// Constanta Proportionala (Kp)
// Determina cat de agresiv raspunde ventilatorul la eroare
const float Kp = 50.0; 

// --- 4. FreeRTOS Handles ---
QueueHandle_t tempQueue;
QueueHandle_t thresholdQueue;
TaskHandle_t hTaskSenzor;
TaskHandle_t hTaskBLE;

// --- 5. Configurare BLE ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TEMP_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define THRESHOLD_CHAR_UUID "c5b2c86a-529a-4e24-878e-a2b0c03c9c6c"

BLECharacteristic *pTempCharacteristic;
BLECharacteristic *pThresholdCharacteristic;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; };
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false;
        BLEDevice::startAdvertising(); 
    }
};

class ThresholdCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            float newThreshold = (float)atof(value.c_str());
            xQueueSend(thresholdQueue, &newThreshold, 0);
        }
    }
};

// ================================================================
// TASK 1: CONTROL LOOP (Core 0)
// ================================================================
void taskSenzorControl(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1000); // 1 secunda
    float currentTemp;
    float receivedThreshold;

    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(FAN_PIN, pwmChannel);

    for (;;) {
        // 1. Verifica daca s-a schimbat pragul din telefon
        if (xQueueReceive(thresholdQueue, &receivedThreshold, 0) == pdPASS) {
            global_threshold = receivedThreshold;
            // Confirmare vizuala inapoi in BLE
            char threshStr[8];
            dtostrf(global_threshold, 6, 2, threshStr);
            pThresholdCharacteristic->setValue(threshStr);
        }

        // 2. Achizitie Temperatura
        sensors.requestTemperatures(); 
        currentTemp = sensors.getTempCByIndex(0);

        if(currentTemp == DEVICE_DISCONNECTED_C) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; 
        }

        // 3. Algoritm de Control Proportional (P din PID)
        int pwmDuty = 0;
        float error = currentTemp - global_threshold;

        if (error > 0) {
            // Calculam turatia proportional cu depasirea temperaturii
            // Ex: Depasire 1 grad * Kp(50) = Duty 50
            // Ex: Depasire 3 grade * Kp(50) = Duty 150
            float dutyCalc = error * Kp;

            // Limite minime de pornire ventilator (start-up voltage)
            if (dutyCalc < 60) dutyCalc = 60; 
            
            // Saturatie maxima (8 biti = 255)
            if (dutyCalc > 255) dutyCalc = 255;

            pwmDuty = (int)dutyCalc;
        } else {
            pwmDuty = 0; // Oprit
        }

        ledcWrite(pwmChannel, pwmDuty);

        // 4. Trimite date spre BLE
        xQueueSend(tempQueue, &currentTemp, 0);

        // 5. Asigura periodicitatea stricta (Jitter minim)
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

// ================================================================
// TASK 2: BLE COMM (Core 1)
// ================================================================
void taskBLE(void *pvParameters) {
    float tempToNotify;

    for (;;) {
        // Asteapta date din coada
        if (xQueueReceive(tempQueue, &tempToNotify, portMAX_DELAY) == pdPASS) {
            if (deviceConnected) {
                char tempStr[8];
                dtostrf(tempToNotify, 6, 2, tempStr);
                pTempCharacteristic->setValue(tempStr);
                pTempCharacteristic->notify();
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    sensors.begin();

    tempQueue = xQueueCreate(5, sizeof(float));
    thresholdQueue = xQueueCreate(2, sizeof(float));

    BLEDevice::init("ESP32-Master-Project");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTempCharacteristic = pService->createCharacteristic(
        TEMP_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pTempCharacteristic->addDescriptor(new BLE2902());

    pThresholdCharacteristic = pService->createCharacteristic(
        THRESHOLD_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    pThresholdCharacteristic->setCallbacks(new ThresholdCallbacks());

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    // Lansare Task-uri pe nuclee diferite
    xTaskCreatePinnedToCore(taskSenzorControl, "SenzorCtrl", 4096, NULL, 1, &hTaskSenzor, 0);
    xTaskCreatePinnedToCore(taskBLE, "BLECom", 4096, NULL, 1, &hTaskBLE, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}
