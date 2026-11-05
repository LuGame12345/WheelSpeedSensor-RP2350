#pragma once
#include <Arduino.h>

// HC-SR04 Ultraschallsensor, nicht-blockierend.
//
// Ablauf: update() sendet einen 10-µs-Triggerimpuls an TRIG. Die Dauer des
// Echo-Impulses wird per Interrupt (CHANGE) an ECHO mit micros() gemessen.
// Dadurch blockiert die Messung loop() nicht (pulseIn() würde bis zu 38 ms warten).
//
// Singleton-Einschränkung wie bei WheelSpeedSensor: nur eine Instanz möglich.
class UltrasonicSensor {
public:
    UltrasonicSensor(uint8_t trigPin, uint8_t echoPin);

    void begin();

    // So oft wie möglich in loop() aufrufen.
    // Gibt true zurück, wenn eine neue Messung (oder ein Timeout) vorliegt.
    bool update();

    // Letzte gültige Entfernung in Metern
    float getDistanceM() const { return _distanceM; }

    // false, wenn kein Echo kam bzw. außerhalb des Messbereichs (~2 cm .. 4 m)
    bool isValid() const { return _valid; }

private:
    uint8_t _trigPin;
    uint8_t _echoPin;

    float _distanceM = 0;
    bool  _valid     = false;

    bool          _waiting       = false;  // Trigger gesendet, Ergebnis ausstehend
    unsigned long _lastTriggerMs = 0;

    static constexpr unsigned long _minIntervalMs = 25;  // frühestens nächste Messung
    static constexpr unsigned long _timeoutMs     = 60;  // kein Echo → ungültig
    static constexpr float         _maxRangeM     = 4.0f;

    volatile uint32_t _echoStartUs  = 0;
    volatile uint32_t _echoDuration = 0;
    volatile bool     _echoReady    = false;

    void _trigger();

    static UltrasonicSensor *instance;
    static void isrEcho();
};
