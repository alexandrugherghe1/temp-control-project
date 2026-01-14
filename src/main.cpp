/**
 * @file main.cpp
 * @brief Sistem Distribuit de Monitorizare și Control (Master Unitbv)
 * @details Implementare Dual-Core ESP32 cu FreeRTOS, BLE și Control Proporțional (PWM)
 * @author Alexandru Gherghe - Versiune Actualizata ESP32 v3.0
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
const int pwmResolution = 8;
// Nota: In versiunea noua nu mai avem nevoie de "Channel" manual

// --- 3. Variabile Globale ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

float global_threshold = 25.0; // Prag temperatura (Setpoint)
bool deviceConnected = false;

// Constanta Proportionala (Kp)
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
        // MODIFICARE v3.0: Folosim String Arduino in loc de std::string
        String value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            // MODIFICARE: Conversie directa din String in float
            float newThreshold = value.toFloat();
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

    // MODIFICARE v3.0: Noua metoda de atasare PWM
    ledcAttach(FAN_PIN, pwmFreq, pwmResolution);

    for (;;) {
        // 1. Verifica daca s-a schimbat pragul din telefon
        if (xQueueReceive(thresholdQueue, &receivedThreshold, 0) == pdPASS) {
            global_threshold = receivedThreshold;
            
            // Confirmare vizuala inapoi in BLE (Folosim String pentru simplitate)
            String threshStr = String(global_threshold, 2);
            pThresholdCharacteristic->setValue(threshStr.c_str());
        }

        // 2. Achizitie Temperatura
        sensors.requestTemperatures(); 
        currentTemp = sensors.getTempCByIndex(0);

        // Verificare eroare citire (-127 grade inseamna senzor deconectat)
        if(currentTemp == DEVICE_DISCONNECTED_C) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; 
        }

        // 3. Algoritm de Control Proportional (P din PID)
        int pwmDuty = 0;
        float error = currentTemp - global_threshold;

        if (error > 0) {
            float dutyCalc = error * Kp;

            // Limite minime si maxime
            if (dutyCalc < 60) dutyCalc = 60; 
            if (dutyCalc > 255) dutyCalc = 255;

            pwmDuty = (int)dutyCalc;
        } else {
            pwmDuty = 0; // Oprit
        }

        // MODIFICARE v3.0: Scriem direct pe PIN, nu pe canal
        ledcWrite(FAN_PIN, pwmDuty);

        // 4. Trimite date spre BLE
        xQueueSend(tempQueue, &currentTemp, 0);

        // 5. Asigura periodicitatea
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

// ================================================================
// TASK 2: BLE COMM (Core 1)
// ================================================================
void taskBLE(void *pvParameters) {
    float tempToNotify;

    for (;;) {
        if (xQueueReceive(tempQueue, &tempToNotify, portMAX_DELAY) == pdPASS) {
            if (deviceConnected) {
                // Conversie simpla float -> String -> char array
                String tempStr = String(tempToNotify, 2);
                
                pTempCharacteristic->setValue(tempStr.c_str());
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

    // Lansare Task-uri
    xTaskCreatePinnedToCore(taskSenzorControl, "SenzorCtrl", 4096, NULL, 1, &hTaskSenzor, 0);
    xTaskCreatePinnedToCore(taskBLE, "BLECom", 4096, NULL, 1, &hTaskBLE, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}
