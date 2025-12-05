# Sistem de Monitorizare și Control al Temperaturii (ESP32 + FreeRTOS)

Proiect de Disertație - Universitatea Transilvania din Brașov
**Program Master:** Sisteme Electrice și de Comunicații Integrate

## Descriere
Acest proiect implementează un sistem distribuit IoT pentru controlul temperaturii ambientale. Utilizează un microcontroller ESP32 într-o configurație **Dual-Core**, separând logica de control critică de stiva de comunicație Bluetooth Low Energy (BLE).

## Arhitectură Software
Sistemul este bazat pe **FreeRTOS** și utilizează două task-uri paralele sincronizate prin cozi de mesaje (Queues):

1.  **Task Control (Core 0):**
    * Citește senzorul DS18B20 (Protocol 1-Wire).
    * Implementează logica de control a ventilatorului.
    * Generează semnal **PWM** (folosind timere hardware LEDC) pentru acționare.
2.  **Task Comunicație (Core 1):**
    * Gestionează serverul GATT (Generic Attribute Profile).
    * Trimite notificări asincrone către aplicația mobilă.
    * Recepționează comenzi pentru setarea pragului de temperatură.

## Componente Hardware
* Placă de dezvoltare: **ESP32 DevKit V1**
* Senzor: **DS18B20**
* Actuator: Ventilator DC (comandat prin tranzistor/MOSFET)

## Instrucțiuni de Utilizare
1.  Deschideți proiectul folosind **VS Code** cu extensia **PlatformIO**.
2.  Compilați și încărcați codul pe placa ESP32.
3.  Folosiți aplicația mobilă **nRF Connect** pentru a vă conecta la dispozitivul `ESP32-Master-Project`.
4.  Abonați-vă la caracteristica de temperatură pentru date în timp real.

## Autor
**Alexandru Gherghe**
