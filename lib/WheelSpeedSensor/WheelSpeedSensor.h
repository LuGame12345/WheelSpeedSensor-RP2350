#pragma once
#include <Arduino.h>

class WheelSpeedSensor {
public:
    struct SpeedData {
        float leftSpeedMs;
        float leftSpeedKmh;
        float rightSpeedMs;
        float rightSpeedKmh;
    };

    // pinLeft/pinRight: GPIO-Pins der Lichtschranken
    // wheelDiameterMm: Raddurchmesser in mm
    // slots: Anzahl Aussparungen pro Umdrehung
    WheelSpeedSensor(uint8_t pinLeft, uint8_t pinRight,
                     float wheelDiameterMm = 65.0f, int slots = 20);

    // Pins konfigurieren und Interrupts aktivieren
    void begin();

    // Gibt true zurück, wenn 200 ms vergangen sind und neue Daten bereitstehen
    bool update(SpeedData &data);

    // Gesamtzahl der Pulse seit begin() (läuft durch, uint32-Überlauf ist harmlos
    // solange nur Differenzen gebildet werden)
    void getPulseCounts(uint32_t &left, uint32_t &right) const;

    // Entprellung: Pulse, die schneller als minUs nach dem vorherigen kommen,
    // werden als Störung verworfen. 2000 µs reichen bis ~1 m/s (≈100 Pulse/s).
    void setDebounceUs(uint32_t minUs) { _debounceUs = minUs; }

private:
    uint8_t _pinLeft;
    uint8_t _pinRight;
    float   _metersPerPulse;

    unsigned long _previousMillis = 0;
    static constexpr unsigned long _intervalMs = 200;

    // Von den ISRs hochgezählte Flanken (werden nie zurückgesetzt)
    volatile uint32_t _countLeft  = 0;
    volatile uint32_t _countRight = 0;

    // Zeitpunkt des letzten gezählten Pulses (für die Entprellung)
    volatile uint32_t _lastPulseUsLeft  = 0;
    volatile uint32_t _lastPulseUsRight = 0;
    volatile uint32_t _debounceUs       = 2000;

    // Zählerstände beim letzten update()
    uint32_t _lastLeft  = 0;
    uint32_t _lastRight = 0;

    // Singleton-Zeiger für die ISRs (nur eine Instanz möglich)
    static WheelSpeedSensor *instance;
    static void isrLeft();
    static void isrRight();
};
