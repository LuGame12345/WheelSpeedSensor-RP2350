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
    attachInterrupt(digitalPinToInterrupt(_pinLeft),  isrLeft,  RISING);
    attachInterrupt(digitalPinToInterrupt(_pinRight), isrRight, RISING);
}

bool WheelSpeedSensor::update(SpeedData &data) {
    unsigned long now = millis();
    if (now - _previousMillis < _intervalMs) return false;

    float elapsed   = (now - _previousMillis) / 1000.0f;
    _previousMillis = now;

    // Zähler atomar auslesen und zurücksetzen
    noInterrupts();
    uint32_t lPulses = _countLeft;
    uint32_t rPulses = _countRight;
    _countLeft  = 0;
    _countRight = 0;
    interrupts();

    data.leftSpeedMs   = (lPulses * _metersPerPulse) / elapsed;
    data.leftSpeedKmh  = data.leftSpeedMs  * 3.6f;
    data.rightSpeedMs  = (rPulses * _metersPerPulse) / elapsed;
    data.rightSpeedKmh = data.rightSpeedMs * 3.6f;
    return true;
}
