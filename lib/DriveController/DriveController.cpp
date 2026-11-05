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
    _error = (pL * b - pR * a) / max(a, b);

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
