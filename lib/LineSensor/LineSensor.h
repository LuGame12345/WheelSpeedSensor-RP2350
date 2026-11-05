#pragma once
#include <Arduino.h>

// 3-Kanal-Spurfolgesensor (links / Mitte / rechts).
// Pegel (am Roboter gemessen): LOW = schwarze Linie erkannt, HIGH = keine Linie.
// Über lineLevel im Konstruktor umstellbar, falls ein anderes Modul verbaut wird.
class LineSensor {
public:
    struct Reading {
        bool left;    // true = Linie unter dem linken Sensor
        bool center;
        bool right;
    };

    LineSensor(uint8_t pinLeft, uint8_t pinCenter, uint8_t pinRight,
               uint8_t lineLevel = LOW);

    void begin();

    // Alle drei Kanäle einlesen
    Reading read() const;

    // Lage der Linie relativ zur Mitte:
    //   -1.0 = ganz links, 0.0 = mittig, +1.0 = ganz rechts
    // Gibt false zurück, wenn keine Linie erkannt wird (position bleibt unverändert).
    bool getPosition(float &position) const;

    // Als Bitmuster (links = Bit 2, Mitte = Bit 1, rechts = Bit 0), z. B. 0b010 = mittig
    uint8_t readBits() const;

private:
    uint8_t _pinLeft;
    uint8_t _pinCenter;
    uint8_t _pinRight;
    uint8_t _lineLevel;  // Pegel, der „schwarze Linie“ bedeutet
};
