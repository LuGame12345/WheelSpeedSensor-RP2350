#include <Arduino.h>
#include "WheelSpeedSensor.h"
#include "MotorDriver.h"

// Lichtschranken: Pin Links = 21, Pin Rechts = 20, Raddurchmesser = 65 mm, Aussparungen = 20
WheelSpeedSensor speedSensors(21, 20, 65.0, 20);

// L298N laut Schaltungsdokumentation:
// Links:  IN1 = GPIO0, IN2 = GPIO1, ENA = GPIO2
// Rechts: IN3 = GPIO4, IN4 = GPIO5, ENB = GPIO3
// PWM-Tastgrad auf 80 % begrenzt (3–6-V-Motoren an 2S-Akku)
MotorDriver motors(0, 1, 2, 4, 5, 3, 80);

static constexpr int TEST_SPEED = 50;  // % für die Tastatursteuerung

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Warten auf Seriellen Monitor (Timeout 3s)

    motors.begin();
    speedSensors.begin();
    Serial.println("Initialisiert. Steuerung: w=vor, s=zurück, a=links, d=rechts, x=stopp, b=bremsen");
}

void handleSerialCommands() {
    while (Serial.available()) {
        switch (Serial.read()) {
            case 'w': motors.drive( TEST_SPEED,  TEST_SPEED); break;
            case 's': motors.drive(-TEST_SPEED, -TEST_SPEED); break;
            case 'a': motors.drive(-TEST_SPEED,  TEST_SPEED); break;
            case 'd': motors.drive( TEST_SPEED, -TEST_SPEED); break;
            case 'x': motors.stop();  break;
            case 'b': motors.brake(); break;
        }
    }
}

void loop() {
    handleSerialCommands();

    WheelSpeedSensor::SpeedData data;

    // Prüfen, ob neue Geschwindigkeitswerte vorliegen (alle 200ms)
    if (speedSensors.update(data)) {
        Serial.print("Motor L/R: ");
        Serial.print(motors.getLeft());
        Serial.print("% / ");
        Serial.print(motors.getRight());
        Serial.print("%   ||   Links -> m/s: ");
        Serial.print(data.leftSpeedMs, 3);
        Serial.print(" | km/h: ");
        Serial.print(data.leftSpeedKmh, 2);
        Serial.print("   ||   Rechts -> m/s: ");
        Serial.print(data.rightSpeedMs, 3);
        Serial.print(" | km/h: ");
        Serial.println(data.rightSpeedKmh, 2);
    }
}
