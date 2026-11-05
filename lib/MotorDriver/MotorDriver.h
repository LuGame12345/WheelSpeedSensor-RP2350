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
class MotorDriver {
public:
    MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena,
                uint8_t in3, uint8_t in4, uint8_t enb,
                uint8_t maxDutyPercent = 80);

    // Pins als Ausgänge konfigurieren, PWM einrichten, Motoren stoppen
    void begin();

    // Beide Motoren setzen (-100 .. +100 %)
    void drive(int leftPercent, int rightPercent);
    void setLeft(int percent);
    void setRight(int percent);

    // Auslaufen lassen (Enable aus, Motor dreht frei aus)
    void stop();

    // Aktiv bremsen (beide Richtungseingänge HIGH → Motorklemmen kurzgeschlossen)
    void brake();

    // Drehrichtung eines Motors umkehren, falls er falsch herum verbaut ist
    void setInverted(bool left, bool right);

    int getLeft()  const { return _left.percent; }
    int getRight() const { return _right.percent; }

private:
    struct Channel {
        uint8_t inA;
        uint8_t inB;
        uint8_t en;
        bool    inverted = false;
        int     percent  = 0;
    };

    Channel _left;
    Channel _right;
    uint8_t _maxDutyPercent;

    static constexpr uint32_t _pwmFreqHz = 1000;  // L298N schaltet langsam → ~1 kHz
    static constexpr uint32_t _pwmRange  = 1000;  // Auflösung 0..1000

    void _initChannel(const Channel &ch);
    void _apply(Channel &ch, int percent);
};
