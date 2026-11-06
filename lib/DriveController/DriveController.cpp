#include "DriveController.h"

DriveController::DriveController(MotorDriver &motors, WheelSpeedSensor &sensors)
    : _motors(motors), _sensors(sensors) {}

void DriveController::drive(float leftPercent, float rightPercent) {
    leftPercent  = constrain(leftPercent,  -100.0f, 100.0f);
    rightPercent = constrain(rightPercent, -100.0f, 100.0f);

    // Nur bei neuem Sollwert die Streckenzählung neu starten
    if (leftPercent == _targetLeft && rightPercent == _targetRight) return;

    _targetLeft  = leftPercent;
    _targetRight = rightPercent;
    _sensors.getPulseCounts(_startLeft, _startRight);
    _error      = 0;
    _correction = 0;

    if (fabsf(_targetLeft) < 0.5f && fabsf(_targetRight) < 0.5f) {
        _motors.stop();
        return;
    }

    // Sofort mit der bereits gelernten Korrektur losfahren
    _lastMs = millis() - _intervalMs;
    update();
}

void DriveController::stop() {
    _targetLeft = _targetRight = 0;
    _correction = 0;
    _motors.stop();
}

void DriveController::brake() {
    _targetLeft = _targetRight = 0;
    _correction = 0;
    _motors.brake();
}

void DriveController::getTripPulses(uint32_t &left, uint32_t &right) const {
    uint32_t l, r;
    _sensors.getPulseCounts(l, r);
    left  = l - _startLeft;
    right = r - _startRight;
}

void DriveController::setManualCalibration(float valuePercent) {
    _manualValue = constrain(valuePercent, -30.0f, 30.0f);
    _manual      = true;
}

void DriveController::setAutoCalibration() {
    _manual = false;
}

void DriveController::resetTrim() {
    for (float &t : _trim) t = 0;
}

void DriveController::update() {
    unsigned long now = millis();
    if (now - _lastMs < _intervalMs) return;
    float dt = (now - _lastMs) / 1000.0f;
    _lastMs  = now;

    float a = fabsf(_targetLeft);
    float b = fabsf(_targetRight);

    // Stillstand → nichts zu regeln (stop()/brake() haben die Motoren bereits gesetzt)
    if (a < 0.5f && b < 0.5f) return;

    // Manuelle Kalibrierung: fester Ausgleich, kein Selbstlernen
    if (_manual) {
        _correction = 0;
        float outL = a, outR = b;
        float v = _manualValue / 100.0f;
        if (v > 0) outR *= (1.0f - v);   // rechten Motor bremsen → zieht nach rechts
        else       outL *= (1.0f + v);   // linken Motor bremsen  → zieht nach links
        _motors.drive(copysignf(outL * _speedScale, _targetLeft),
                      copysignf(outR * _speedScale, _targetRight));
        return;
    }

    // Steht ein Rad bewusst still, gibt es kein Verhältnis zum Regeln
    if (!_syncEnabled || a < 0.5f || b < 0.5f) {
        _correction = 0;
        _motors.drive(_targetLeft * _speedScale, _targetRight * _speedScale);
        return;
    }

    uint32_t l, r;
    _sensors.getPulseCounts(l, r);
    float pL = (float)(l - _startLeft);
    float pR = (float)(r - _startRight);

    // Strecken-Fehler, normiert auf das gewünschte Verhältnis a:b.
    // Geradeaus (a == b) ist das einfach pL - pR.
    // Kurs-Trim: Sollverhältnis leicht verschieben (nur für die Fehlerberechnung)
    float aT = a * (1.0f + _bias);
    float bT = b * (1.0f - _bias);
    _error = (pL * bT - pR * aT) / max(aT, bT);

    // Trim lernen (Integral), begrenzt gegen Aufschaukeln
    float &trim = _trim[_trimIndex()];
    trim = constrain(trim + _ki * _error * dt, -_maxCorrection, _maxCorrection);

    _correction = constrain(_kp * _error + trim, -_maxCorrection, _maxCorrection);

    // Immer das schnellere Rad bremsen
    float outL = a, outR = b;
    if (_correction > 0) outL *= (1.0f - _correction);
    else                 outR *= (1.0f + _correction);

    _motors.drive(copysignf(outL * _speedScale, _targetLeft),
                  copysignf(outR * _speedScale, _targetRight));
}
