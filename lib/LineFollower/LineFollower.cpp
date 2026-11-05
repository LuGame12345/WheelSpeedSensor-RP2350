#include "LineFollower.h"

LineFollower::LineFollower(LineSensor &line, DriveController &drive)
    : _line(line), _drive(drive) {}

void LineFollower::start() {
    _active  = true;
    _paused  = false;
    _lastPos = 0.0f;
    _inGap   = false;
    _lastMs  = millis();
    _drive.drive(baseSpeed, baseSpeed);
}

void LineFollower::stop() {
    _active = false;
    _paused = false;
    _drive.stop();
}

void LineFollower::update() {
    if (!_active || _paused) return;

    unsigned long now = millis();
    if (now - _lastMs < _intervalMs) return;
    float dt = (now - _lastMs) / 1000.0f;
    _lastMs  = now;

    float pos;
    if (!_line.getPosition(pos)) {
        // Lücke → geradeaus, bis die Linie wieder da ist
        _inGap = true;
        _drive.drive(baseSpeed, baseSpeed);
        return;
    }
    _inGap = false;

    float steer = kp * pos + kd * (pos - _lastPos) / dt;
    float left  = constrain(baseSpeed * (1.0f + steer), -100.0f, 100.0f);
    float right = constrain(baseSpeed * (1.0f - steer), -100.0f, 100.0f);
    _drive.drive(left, right);

    _lastPos = pos;
}
