#pragma once
#include <Arduino.h>

// Ansteuerung des L298N-Doppel-H-Brücken-Motortreibers (zwei DC-Motoren).
//
// Kanal A = linker Motor  (IN1, IN2, ENA)
// Kanal B = rechter Motor (IN3, IN4, ENB)
//
// Geschwindigkeit wird in Prozent angegeben: -100 (voll rückwärts) .. 0 .. +100 (voll vorwärts).
// Der tatsächliche PWM-Tastgrad wird auf maxDutyPercent begrenzt, damit die
// 3–6-V-Motoren bei vollem 2S-Akku (bis 8,4 V) nicht überlastet werden.
// Jeder Sollwert ≠ 0 wird auf mindestens minDutyPercent abgebildet, damit die
// Motoren auch bei kleinen Werten sicher anlaufen (1 % → minDuty, 100 % → maxDuty).
class MotorDriver {
public:
    MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena,
                uint8_t in3, uint8_t in4, uint8_t enb,
                uint8_t maxDutyPercent = 80, uint8_t minDutyPercent = 0);

    // Pins als Ausgänge konfigurieren, PWM einrichten, Motoren stoppen
    void begin();

    // Beide Motoren setzen (-100 .. +100 %)
    void drive(float leftPercent, float rightPercent);
    void setLeft(float percent);
    void setRight(float percent);

    // Auslaufen lassen (Enable aus, Motor dreht frei aus)
    void stop();

    // Aktiv bremsen (beide Richtungseingänge HIGH → Motorklemmen kurzgeschlossen)
    void brake();

    // Drehrichtung eines Motors umkehren, falls er falsch herum verbaut ist
    void setInverted(bool left, bool right);

    float getLeft()  const { return _left.percent; }
    float getRight() const { return _right.percent; }

private:
    struct Channel {
        uint8_t inA;
        uint8_t inB;
        uint8_t en;
        bool    inverted = false;
        float   percent  = 0;
    };

    Channel _left;
    Channel _right;
    uint8_t _maxDutyPercent;
    uint8_t _minDutyPercent;  // Anlauf-PWM: darunter drehen die Motoren nicht

    static constexpr uint32_t _pwmFreqHz = 1000;  // L298N schaltet langsam → ~1 kHz
    static constexpr uint32_t _pwmRange  = 1000;  // Auflösung 0..1000

    void _initChannel(const Channel &ch);
    void _apply(Channel &ch, float percent);
};
