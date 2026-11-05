#include "MotorDriver.h"
#include <initializer_list>

MotorDriver::MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena, uint8_t in3, uint8_t in4, uint8_t enb,   uint8_t maxDutyPercent)
    : _maxDutyPercent(constrain(maxDutyPercent, 0, 100))
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

void MotorDriver::_apply(Channel &ch, int percent) {
    percent    = constrain(percent, -100, 100);
    ch.percent = percent;

    int dir = ch.inverted ? -percent : percent;

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

    // 0..100 % Sollwert → 0..maxDuty % Tastgrad
    uint32_t duty = (uint32_t)abs(percent) * _maxDutyPercent * _pwmRange / 10000;
    analogWrite(ch.en, duty);
}

void MotorDriver::drive(int leftPercent, int rightPercent) {
    _apply(_left,  leftPercent);
    _apply(_right, rightPercent);
}

void MotorDriver::setLeft(int percent)  { _apply(_left,  percent); }
void MotorDriver::setRight(int percent) { _apply(_right, percent); }

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
