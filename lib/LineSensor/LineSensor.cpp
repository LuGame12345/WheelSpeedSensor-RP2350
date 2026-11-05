#include "LineSensor.h"

LineSensor::LineSensor(uint8_t pinLeft, uint8_t pinCenter, uint8_t pinRight,
                       uint8_t lineLevel)
    : _pinLeft(pinLeft), _pinCenter(pinCenter), _pinRight(pinRight),
      _lineLevel(lineLevel) {}

void LineSensor::begin() {
    // Das Modul treibt seine Ausgänge selbst → kein Pull-up/Pull-down nötig
    pinMode(_pinLeft,   INPUT);
    pinMode(_pinCenter, INPUT);
    pinMode(_pinRight,  INPUT);
}

LineSensor::Reading LineSensor::read() const {
    Reading r;
    r.left   = digitalRead(_pinLeft)   == _lineLevel;
    r.center = digitalRead(_pinCenter) == _lineLevel;
    r.right  = digitalRead(_pinRight)  == _lineLevel;
    return r;
}

uint8_t LineSensor::readBits() const {
    Reading r = read();
    return (r.left << 2) | (r.center << 1) | r.right;
}

bool LineSensor::getPosition(float &position) const {
    Reading r = read();
    int count = r.left + r.center + r.right;
    if (count == 0) return false;

    // Mittelwert der Sensoren, die die Linie sehen (links = -1, Mitte = 0, rechts = +1)
    position = (float)(r.right - r.left) / count;
    return true;
}
