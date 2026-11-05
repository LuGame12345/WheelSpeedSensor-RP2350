#include "WheelSpeedSensor.h"

WheelSpeedSensor *WheelSpeedSensor::instance = nullptr;

WheelSpeedSensor::WheelSpeedSensor(uint8_t pinLeft, uint8_t pinRight,
                                     float wheelDiameterMm, int slots)
    : _pinLeft(pinLeft), _pinRight(pinRight)
{
    float circumference = (wheelDiameterMm / 1000.0f) * 3.14159265359f;
    _metersPerPulse = circumference / slots;
}

void WheelSpeedSensor::isrLeft() {
    if (instance) instance->_countLeft++;
}

void WheelSpeedSensor::isrRight() {
    if (instance) instance->_countRight++;
}

void WheelSpeedSensor::begin() {
    instance = this;

    pinMode(_pinLeft,  INPUT_PULLUP);
    pinMode(_pinRight, INPUT_PULLUP);

    _previousMillis = millis();
    getPulseCounts(_lastLeft, _lastRight);
    attachInterrupt(digitalPinToInterrupt(_pinLeft),  isrLeft,  RISING);
    attachInterrupt(digitalPinToInterrupt(_pinRight), isrRight, RISING);
}

void WheelSpeedSensor::getPulseCounts(uint32_t &left, uint32_t &right) const {
    // Beide Zähler atomar (zum selben Zeitpunkt) auslesen
    noInterrupts();
    left  = _countLeft;
    right = _countRight;
    interrupts();
}

bool WheelSpeedSensor::update(SpeedData &data) {
    unsigned long now = millis();
    if (now - _previousMillis < _intervalMs) return false;

    float elapsed   = (now - _previousMillis) / 1000.0f;
    _previousMillis = now;

    uint32_t l, r;
    getPulseCounts(l, r);
    uint32_t lPulses = l - _lastLeft;
    uint32_t rPulses = r - _lastRight;
    _lastLeft  = l;
    _lastRight = r;

    data.leftSpeedMs   = (lPulses * _metersPerPulse) / elapsed;
    data.leftSpeedKmh  = data.leftSpeedMs  * 3.6f;
    data.rightSpeedMs  = (rPulses * _metersPerPulse) / elapsed;
    data.rightSpeedKmh = data.rightSpeedMs * 3.6f;
    return true;
}
