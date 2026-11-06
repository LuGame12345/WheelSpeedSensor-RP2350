#pragma once
#include <Arduino.h>
#include "MotorDriver.h"
#include "WheelSpeedSensor.h"

// Gleichlaufregelung: hält beide Räder über die Lichtschranken synchron,
// damit der Roboter geradeaus fährt (bzw. die gewünschte Kurve exakt einhält).
//
// Prinzip: Seit dem letzten drive()-Befehl werden die Pulse beider Räder gezählt.
// Sollen beide gleich schnell laufen, müssen auch gleich viele Pulse anfallen.
// Die Differenz (in Pulsen = zurückgelegte Strecke) ist der Fehler:
//   - P-Anteil:   holt einen Vorsprung eines Rades sofort wieder auf
//   - Trim (I):   lernt die dauerhafte Abweichung der Motoren und merkt sie sich
//                 für die nächste Fahrt → selbstkalibrierend
// Korrigiert wird immer, indem das schnellere Rad gebremst wird (so bleibt
// auch bei 100 % noch Regelspielraum).
class DriveController {
public:
    DriveController(MotorDriver &motors, WheelSpeedSensor &sensors);

    // Sollwerte setzen (-100 .. +100 %). Gleiche Werte erneut setzen ist unkritisch.
    void drive(float leftPercent, float rightPercent);
    void stop();
    void brake();

    // So oft wie möglich in loop() aufrufen
    void update();

    void setSyncEnabled(bool enabled) { _syncEnabled = enabled; }

    // Geschwindigkeit global skalieren (0..1), z. B. 0.5 bei nahem Hindernis.
    // Die Regelung bleibt aktiv, das Verhältnis links:rechts bleibt erhalten.
    void setSpeedScale(float scale) { _speedScale = constrain(scale, 0.0f, 1.0f); }

    // true, wenn der Roboter insgesamt nach vorne fährt
    bool isMovingForward() const { return (_targetLeft + _targetRight) > 0.5f; }
    void setGains(float kp, float ki) { _kp = kp; _ki = ki; }
    float getKp() const { return _kp; }
    float getKi() const { return _ki; }

    // Kurs-Trim für mechanische Unterschiede (z. B. Raddurchmesser), die die
    // Lichtschranken nicht sehen: +0.01 = links soll 1 % mehr Pulse machen als rechts
    // (Roboter zieht dann nach rechts). Einstellen, bis er wirklich geradeaus fährt.
    void  setBias(float bias) { _bias = constrain(bias, -0.2f, 0.2f); }
    float getBias() const { return _bias; }

    // Pulse beider Räder seit dem letzten neuen Fahrbefehl (Diagnose)
    void getTripPulses(uint32_t &left, uint32_t &right) const;

    // Gelernte Korrektur verwerfen (alle Fahrtrichtungen)
    void resetTrim();

    // ---- Manuelle Kalibrierung ----
    // Fester Motor-Ausgleich statt Selbstlernen. value in Prozent, -30 … +30:
    //   > 0 → rechter Motor wird um value % gebremst  → Roboter zieht mehr nach rechts
    //   < 0 → linker Motor wird um |value| % gebremst → Roboter zieht mehr nach links
    // Faustregel: zieht er nach links → Wert erhöhen, zieht er nach rechts → Wert senken.
    // Im manuellen Modus ist die Gleichlaufregelung (Selbstlernen) aus.
    void  setManualCalibration(float valuePercent);
    void  setAutoCalibration();                 // zurück zum Selbstlernen (Standard)
    bool  isManualCalibration() const { return _manual; }
    float getManualValue() const { return _manualValue; }

    // Gelernter Wert (vorwärts) in derselben Einheit wie der manuelle Wert (%),
    // z. B. als Startwert für den Drehregler in der App
    float getLearnedValue() const { return -_trim[0] * 100.0f; }

    // Diagnose
    float getTrim()       const { return _trim[_trimIndex()]; }  // gelernte Korrektur (Anteil, z. B. 0.05 = 5 %)
    float getCorrection() const { return _correction; }          // aktuell angewandte Korrektur
    float getPulseError() const { return _error; }               // >0: links voraus, <0: rechts voraus

private:
    MotorDriver      &_motors;
    WheelSpeedSensor &_sensors;

    float _targetLeft  = 0;
    float _targetRight = 0;

    uint32_t _startLeft  = 0;
    uint32_t _startRight = 0;

    // Getrennte Trims je Fahrtrichtung: [vor/vor, vor/rück, rück/vor, rück/rück]
    float _trim[4] = {0, 0, 0, 0};

    float _error      = 0;
    float _correction = 0;
    bool  _syncEnabled = true;
    float _speedScale  = 1.0f;
    bool  _manual      = false;
    float _manualValue = 0.0f;   // %

    float _kp   = 0.08f;  // Korrektur pro Puls Vorsprung (0.08 = 8 %)
    float _ki   = 0.05f;  // Lernrate des Trims pro Puls und Sekunde
    float _bias = 0.0f;   // Kurs-Trim (siehe setBias)
    static constexpr float _maxCorrection = 0.30f;  // max. 30 % abbremsen

    unsigned long _lastMs = 0;
    static constexpr unsigned long _intervalMs = 20;

    int _trimIndex() const { return (_targetLeft < 0 ? 2 : 0) + (_targetRight < 0 ? 1 : 0); }
};
