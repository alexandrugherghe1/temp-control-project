# Sistem de Monitorizare și Control al Temperaturii (ESP32 + FreeRTOS)

**Proiect Disertație** | Universitatea Transilvania din Brașov  
**Specializare:** Sisteme Electrice și de Comunicații Integrate  
**Student:** Alexandru Gherghe

---

## 📌 Descriere Proiect
Acest proiect implementează un sistem embedded distribuit (IoT) pentru monitorizarea temperaturii ambientale și controlul automat al unui sistem de răcire. Arhitectura hardware se bazează pe microcontrollerul **ESP32**, utilizând capabilitățile **Dual-Core** și sistemul de operare în timp real **FreeRTOS** pentru a asigura un comportament deterministic și o conectivitate robustă.

Sistemul permite vizualizarea datelor și setarea parametrilor de control de la distanță, prin intermediul unei conexiuni **Bluetooth Low Energy (BLE)**.

## 🛠️ Arhitectură Software & Tehnologii

Software-ul este proiectat pe principiul separării planului de control de cel de comunicație, exploatând arhitectura asimetrică a procesorului Xtensa LX6:

### 1. Task Control (Core 0 - Hard Real-Time)
* **Rol:** Gestionează procesul fizic critic.
* **Senzor:** Achiziție date de la **DS18B20** (Protocol 1-Wire).
* **Algoritm:** Implementează un **Controler Proporțional (P)**. Turația ventilatorului este calculată dinamic în funcție de eroarea dintre temperatura măsurată și pragul setat ($Duty = Error \times K_p$).
* **Actuator:** Generare semnal **PWM (Pulse Width Modulation)** pe 8 biți pentru controlul fin al turației ventilatorului.

### 2. Task Comunicație (Core 1 - Soft Real-Time)
* **Rol:** Gestionează interfața cu utilizatorul.
* **BLE Server:** Implementează un profil GATT personalizat (Environmental Sensing).
* **Notify:** Trimite temperatura curentă către telefon în timp real.
* **Write:** Recepționează noul prag de temperatură (Setpoint) de la utilizator.

### 3. Sincronizare (Inter-Process Communication)
Transferul datelor între cele două nuclee se realizează exclusiv prin **FreeRTOS Queues**, asigurând integritatea datelor (thread-safety) și eliminând riscul de *Race Conditions*.

## 🔌 Configurație Hardware (Pinout)

Sistemul este configurat pentru placa de dezvoltare **ESP32 DevKit V1**:

| Componentă | Pin ESP32 | Observații |
| :--- | :--- | :--- |
| **Senzor DS18B20** | GPIO 4 | Necesită rezistor de pull-up (4.7kΩ) între Data și 3.3V |
| **Ventilator (MOSFET)** | GPIO 5 | Ieșire PWM (Timer Hardware LEDC, 5kHz) |
| **UART (Debug)** | TX/RX (USB) | Baud rate: 115200 |

**Necesar Hardware:**
* ESP32 DevKit V1.
* Senzor digital DS18B20.
* Ventilator DC (12V) + Etaj de putere (Tranzistor MOSFET ex: IRLZ44N).

## 📂 Structura Proiectului

```text
├── doc/                  # Documentația proiectului și lucrarea științifică (PDF + LaTeX)
├── src/
│   └── main.cpp          # Codul sursă principal (C++ / Arduino framework)
├── platformio.ini        # Fișier configurare biblioteci și mediu
└── README.md             # Acest fișier
