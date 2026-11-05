#pragma once
#include <Arduino.h>
#include "LineSensor.h"
#include "DriveController.h"

// Autonome Linienfolge mit dem 3-Kanal-Spurfolgesensor.
//
// Nur Mitte (C) sieht die Linie → perfekt auf der Linie → geradeaus.
// Linie unter links/rechts → dagegen lenken:
//
//   L C R   Position   Fahrt
//   . # .     0        geradeaus
//   # # .   −0,5       leicht links
//   # . .   −1         stark links
//   . # #   +0,5       leicht rechts
//   . . #   +1         stark rechts
//
//   lenk   = kp · pos + kd · Δpos/Δt
//   links  = basis · (1 + lenk)
//   rechts = basis · (1 − lenk)
//
// Lücke: Sieht kein Sensor die Linie (z. B. in der Mitte der Kreuzung), fährt er
// geradeaus weiter, bis die Linie wieder auftaucht.
// Hindernisse behandelt main.cpp über setPaused().
class LineFollower {
public:
    LineFollower(LineSensor &line, DriveController &drive);

    void start();
    void stop();                       // Linienfolge aus, Motoren aus
    bool isActive() const { return _active; }

    // Pausiert (z. B. Hindernis): update() setzt keine Fahrbefehle
    void setPaused(bool paused) { _paused = paused; }
    bool isPaused() const { return _paused; }

    // In jedem loop() aufrufen
    void update();

    float getPosition() const { return _lastPos; }
    bool  inGap() const { return _inGap; }   // gerade keine Linie → geradeaus

    // ---- Parameter ----
    float baseSpeed = 40.0f;   // % Grundtempo auf der Linie
    float kp        = 0.6f;    // Lenkstärke
    float kd        = 0.0f;    // Dämpfung (pro Sekunde)

private:
    LineSensor      &_line;
    DriveController &_drive;

    bool  _active  = false;
    bool  _paused  = false;
    float _lastPos = 0.0f;
    bool  _inGap   = false;

    unsigned long _lastMs = 0;
    static constexpr unsigned long _intervalMs = 10;
};
