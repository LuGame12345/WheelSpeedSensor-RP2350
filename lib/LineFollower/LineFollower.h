#pragma once
#include <Arduino.h>
#include "LineSensor.h"
#include "DriveController.h"
#include "WheelSpeedSensor.h"

// Autonome Linienfolge (Konzept: „Linienverfolgung – Programmierkonzept“).
//
// Zustände:
//   FOLGEN  – Linie unter dem Sensor, Lenkung per P(D)-Regler auf die Position
//   LUECKE  – keine Linie: geradeaus weiter und Strecke zählen (Kreuzung mit Lücke)
//   SUCHEN  – Lücke länger als gapMaxMm: auf der Stelle zur zuletzt gesehenen Seite drehen
//   STOPP   – auch nach searchMaxMm Drehweg nichts gefunden → Motoren aus
//
// Lenkung: lenk = kp·pos + kd·Δpos/Δt;  links = basis·(1+lenk), rechts = basis·(1−lenk)
// pos: −1 (Linie links) … 0 (mittig) … +1 (Linie rechts), aus LineSensor::getPosition().
//
// Hindernisse behandelt main.cpp; dafür gibt es setPaused() und wantsForward().
class LineFollower {
public:
    enum class State { Idle, Follow, Gap, Search, Stopped };

    LineFollower(LineSensor &line, DriveController &drive, WheelSpeedSensor &wheels);

    void start();
    void stop();                       // → Idle, Motoren aus
    bool isActive() const { return _state == State::Follow || _state == State::Gap || _state == State::Search; }

    // Pausiert (z. B. Hindernis): update() setzt keine Fahrbefehle
    void setPaused(bool paused) { _paused = paused; }
    bool isPaused() const { return _paused; }

    // true, wenn der Roboter gerade vorwärts fahren will (für die Hinderniserkennung)
    bool wantsForward() const { return _state == State::Follow || _state == State::Gap; }

    // In jedem loop() aufrufen
    void update();

    State       getState() const { return _state; }
    const char *getStateName() const;
    float       getPosition() const { return _lastPos; }
    float       getStateDistanceMm() const;   // Strecke seit Beginn von LUECKE/SUCHEN

    // ---- Parameter (Startwerte aus dem Konzept) ----
    float baseSpeed   = 40.0f;   // % Grundtempo auf der Linie
    float kp          = 0.6f;    // Lenkstärke
    float kd          = 0.0f;    // Dämpfung (pro Sekunde)
    float gapMaxMm    = 100.0f;  // so weit ohne Linie geradeaus (Kreuzung)
    float crossHoldMm = 50.0f;   // nach einer Lücke nur sanft lenken
    float crossSteer  = 0.15f;   // max. Lenkung während crossHoldMm
    float searchSpeed = 40.0f;   // % Drehtempo beim Suchen
    float searchMaxMm = 400.0f;  // max. Radstrecke beim Suchen, dann STOPP

private:
    LineSensor       &_line;
    DriveController  &_drive;
    WheelSpeedSensor &_wheels;

    State _state  = State::Idle;
    bool  _paused = false;

    float _lastPos  = 0.0f;   // letzte Linienposition
    int   _lastSide = 1;      // +1 = Linie zuletzt rechts, −1 = links (Suchrichtung)

    float _stateStartMm = 0.0f;   // Strecke beim Eintritt in LUECKE/SUCHEN
    float _holdUntilMm  = 0.0f;   // bis hierhin nur sanft lenken (nach Lücke)

    unsigned long _lastMs = 0;
    static constexpr unsigned long _intervalMs = 10;

    float _travelledMm() const;   // mittlere Radstrecke seit Start (beide Räder)
    void  _enter(State s);
};
