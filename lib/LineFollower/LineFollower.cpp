#include "LineFollower.h"

LineFollower::LineFollower(LineSensor &line, DriveController &drive, WheelSpeedSensor &wheels)
    : _line(line), _drive(drive), _wheels(wheels) {}

float LineFollower::_travelledMm() const {
    // Lichtschranken zählen auch rückwärts hoch → beim Drehen auf der Stelle
    // ergibt der Mittelwert die Radstrecke, nicht 0
    uint32_t l, r;
    _wheels.getPulseCounts(l, r);
    return (l + r) * 0.5f * _wheels.getMetersPerPulse() * 1000.0f;
}

float LineFollower::getStateDistanceMm() const {
    if (_state != State::Gap && _state != State::Search) return 0.0f;
    return _travelledMm() - _stateStartMm;
}

const char *LineFollower::getStateName() const {
    switch (_state) {
        case State::Idle:    return "AUS";
        case State::Follow:  return "FOLGEN";
        case State::Gap:     return "LUECKE";
        case State::Search:  return "SUCHEN";
        case State::Stopped: return "STOPP";
    }
    return "?";
}

void LineFollower::_enter(State s) {
    _state        = s;
    _stateStartMm = _travelledMm();
}

void LineFollower::start() {
    _paused  = false;
    _lastPos = 0.0f;
    _holdUntilMm = 0.0f;
    _lastMs  = millis();
    _enter(State::Follow);
}

void LineFollower::stop() {
    _state  = State::Idle;
    _paused = false;
    _drive.stop();
}

void LineFollower::update() {
    if (!isActive() || _paused) return;

    unsigned long now = millis();
    if (now - _lastMs < _intervalMs) return;
    float dt = (now - _lastMs) / 1000.0f;
    _lastMs  = now;

    float pos;
    bool  seen = _line.getPosition(pos);   // false = alle Sensoren sehen Weiß
    float mm   = _travelledMm();

    switch (_state) {
        case State::Follow: {
            if (!seen) {
                // Linie weg → erst geradeaus weiter (Kreuzung mit Lücke)
                _enter(State::Gap);
                _drive.drive(baseSpeed, baseSpeed);
                return;
            }

            float steer = kp * pos + kd * (pos - _lastPos) / dt;
            // Direkt nach einer Lücke nicht in einen schrägen Kreuzungsast abbiegen
            if (mm < _holdUntilMm) steer = constrain(steer, -crossSteer, crossSteer);

            float left  = constrain(baseSpeed * (1.0f + steer), -100.0f, 100.0f);
            float right = constrain(baseSpeed * (1.0f - steer), -100.0f, 100.0f);
            _drive.drive(left, right);

            _lastPos = pos;
            if (pos > 0.01f)  _lastSide = 1;
            if (pos < -0.01f) _lastSide = -1;
            break;
        }

        case State::Gap:
            if (seen) {
                _holdUntilMm = mm + crossHoldMm;
                _state = State::Follow;
                return;
            }
            if (mm - _stateStartMm >= gapMaxMm) {
                // Lücke zu lang → Linie verloren, zur zuletzt gesehenen Seite drehen
                _enter(State::Search);
                _drive.drive(searchSpeed * _lastSide, -searchSpeed * _lastSide);
            }
            break;

        case State::Search:
            if (seen) {
                _lastPos = pos;
                _holdUntilMm = 0.0f;
                _state = State::Follow;
                return;
            }
            if (mm - _stateStartMm >= searchMaxMm) {
                _state = State::Stopped;
                _drive.stop();
            }
            break;

        default:
            break;
    }
}
