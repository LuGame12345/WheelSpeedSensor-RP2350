#include "MotorDriver.h"
#include <initializer_list>

MotorDriver::MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena, uint8_t in3, uint8_t in4, uint8_t enb,   uint8_t maxDutyPercent, uint8_t minDutyPercent)
    : _maxDutyPercent(constrain(maxDutyPercent, 0, 100)),
      _minDutyPercent(constrain(minDutyPercent, 0, _maxDutyPercent))
{
    _left.inA  = in1;  _left.inB  = in2;  _left.en  = ena;
    _right.inA = in3;  _right.inB = in4;  _right.en = enb;
}

void MotorDriver::_initChannel(const Channel &ch) {
    pinMode(ch.inA, OUTPUT);
    pinMode(ch.inB, OUTPUT);
    pinMode(ch.en,  OUTPUT);
    digitalWrite(ch.inA, LOW);
    digitalWrite(ch.inB, LOW);
    analogWrite(ch.en, 0);
}

void MotorDriver::begin() {
    // Hinweis: analogWriteFreq/-Range gelten in arduino-pico global für alle PWM-Pins
    analogWriteFreq(_pwmFreqHz);
    analogWriteRange(_pwmRange);

    _initChannel(_left);
    _initChannel(_right);
    stop();
}

void MotorDriver::_apply(Channel &ch, float percent) {
    percent    = constrain(percent, -100.0f, 100.0f);
    ch.percent = percent;

    float dir = ch.inverted ? -percent : percent;

    if (dir > 0) {
        digitalWrite(ch.inA, HIGH);
        digitalWrite(ch.inB, LOW);
    } else if (dir < 0) {
        digitalWrite(ch.inA, LOW);
        digitalWrite(ch.inB, HIGH);
    } else {
        digitalWrite(ch.inA, LOW);
        digitalWrite(ch.inB, LOW);
    }

    // 0 % → aus, sonst 0..100 % Sollwert → minDuty..maxDuty % Tastgrad
    float mag = fabsf(percent);
    float dutyPercent = (mag < 0.01f) ? 0.0f
        : _minDutyPercent + mag * (_maxDutyPercent - _minDutyPercent) / 100.0f;
    uint32_t duty = (uint32_t)(dutyPercent * _pwmRange / 100.0f);
    analogWrite(ch.en, duty);
}

void MotorDriver::drive(float leftPercent, float rightPercent) {
    _apply(_left,  leftPercent);
    _apply(_right, rightPercent);
}

void MotorDriver::setLeft(float percent)  { _apply(_left,  percent); }
void MotorDriver::setRight(float percent) { _apply(_right, percent); }

void MotorDriver::stop() {
    drive(0, 0);
}

void MotorDriver::brake() {
    for (Channel *ch : {&_left, &_right}) {
        ch->percent = 0;
        digitalWrite(ch->inA, HIGH);
        digitalWrite(ch->inB, HIGH);
        analogWrite(ch->en, _pwmRange);  // Enable voll an → Bremswirkung
    }
}

void MotorDriver::setInverted(bool left, bool right) {
    _left.inverted  = left;
    _right.inverted = right;
    // Wirkt ab dem nächsten drive()/setLeft()/setRight()
}
