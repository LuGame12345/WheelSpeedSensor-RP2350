#include "UltrasonicSensor.h"

UltrasonicSensor *UltrasonicSensor::instance = nullptr;

// Schallgeschwindigkeit ~343 m/s bei 20 °C, Hin- und Rückweg → /2
static constexpr float METERS_PER_US = 343.0f / 1e6f / 2.0f;

UltrasonicSensor::UltrasonicSensor(uint8_t trigPin, uint8_t echoPin)
    : _trigPin(trigPin), _echoPin(echoPin) {}

void UltrasonicSensor::isrEcho() {
    if (!instance) return;
    if (digitalRead(instance->_echoPin)) {
        instance->_echoStartUs = micros();                    // steigende Flanke
    } else {
        instance->_echoDuration = micros() - instance->_echoStartUs;  // fallende Flanke
        instance->_echoReady    = true;
    }
}

void UltrasonicSensor::begin() {
    instance = this;
    pinMode(_trigPin, OUTPUT);
    digitalWrite(_trigPin, LOW);
    pinMode(_echoPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(_echoPin), isrEcho, CHANGE);
}

void UltrasonicSensor::_trigger() {
    _echoReady = false;
    digitalWrite(_trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trigPin, LOW);
    _lastTriggerMs = millis();
    _waiting       = true;
}

bool UltrasonicSensor::update() {
    unsigned long now = millis();

    if (_waiting) {
        if (_echoReady) {
            noInterrupts();
            uint32_t duration = _echoDuration;
            _echoReady = false;
            interrupts();

            float d  = duration * METERS_PER_US;
            _valid   = (d <= _maxRangeM);
            if (_valid) _distanceM = d;
            _waiting = false;
            return true;
        }
        if (now - _lastTriggerMs >= _timeoutMs) {
            _valid   = false;   // kein Echo
            _waiting = false;
            return true;
        }
        return false;
    }

    if (now - _lastTriggerMs >= _minIntervalMs) _trigger();
    return false;
}
