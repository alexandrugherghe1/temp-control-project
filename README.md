# Sistem de Monitorizare și Control al Temperaturii (ESP32 + FreeRTOS)

**Proiect Disertație** | Universitatea Transilvania din Brașov
**Specializare:** Sisteme Electrice și de Comunicații Integrate
**Student:** Alexandru Gherghe

---

## 📌 Descriere Proiect
Proiectul constă într-un sistem embedded distribuit pentru monitorizarea temperaturii în timp real și acționarea automată a unui sistem de răcire (ventilator). Soluția hardware se bazează pe microcontrollerul **ESP32**, exploatând arhitectura **Dual-Core** pentru a rula logica de control și stiva Bluetooth (BLE) în paralel, fără blocaje.

## 🛠️ Arhitectură Software & Tehnologii
Sistemul rulează pe **FreeRTOS**, având sarcini (tasks) distribuite pe nuclee:

* **Core 0 (Task Control):**
    * Achiziție date de la senzorul **DS18B20** (via protocol 1-Wire).
    * Algoritm de control ON/OFF cu histerezis (sau PID).
    * Generare semnal **PWM** pe canalul 0 pentru controlul turației ventilatorului.
* **Core 1 (Task Comunicație):**
    * Server **BLE GATT** pentru telemetrie.
    * Notificare asincronă ("Notify") a temperaturii către client (telefon).
    * Recepționare comenzi ("Write") pentru modificarea pragului de declanșare.

Sincronizarea datelor între cele două nuclee se realizează prin **Cozi de Mesaje (FreeRTOS Queues)** pentru a asigura thread-safety.

## 🔌 Configurație Hardware (Pinout)

| Componentă | Pin ESP32 | Observații |
| :--- | :--- | :--- |
| **Senzor DS18B20** | GPIO 4 | Necesită rezistor pull-up 4.7kΩ |
| **Ventilator (MOSFET)** | GPIO 5 | Ieșire PWM (Timer Hardware) |
| **UART (Debug)** | TX/RX (USB) | Baud rate: 115200 |

**Hardware necesar:**
* Placă dezvoltare ESP32 DevKit V1
* Senzor temperatură DS18B20
* Ventilator DC + Tranzistor/MOSFET (ex: IRLZ44N) pentru etajul de putere.

## 🚀 Cum se rulează proiectul

1.  **Mediu de dezvoltare:** Proiectul este configurat pentru **VS Code + PlatformIO**.
    * Bibliotecile necesare (`OneWire`, `DallasTemperature`) sunt definite în `platformio.ini` și se instalează automat la compilare.
2.  **Upload:** Conectați placa și folosiți comanda *PlatformIO: Upload*.
3.  **Testare:**
    * Deschideți Serial Monitor (115200 baud) pentru log-uri de sistem.
    * Folosiți aplicația **nRF Connect** (Android/iOS) pentru a scana după dispozitivul `ESP32-Master-Project`.

