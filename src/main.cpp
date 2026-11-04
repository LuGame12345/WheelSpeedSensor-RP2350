
#include <Arduino.h>
#include "WheelSpeedSensor.h"

// Objekt erstellen: 
// Pin Links = 21, Pin Rechts = 20, Raddurchmesser = 65 mm, Aussparungen = 20
WheelSpeedSensor speedSensors(21, 20, 65.0, 20);

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Warten auf Seriellen Monitor (Timeout 3s)

    speedSensors.begin();
    Serial.println("Lichtschranken-Bibliothek erfolgreich initialisiert!");
}

void loop() {
    WheelSpeedSensor::SpeedData data;

    // Prüfen, ob neue Geschwindigkeitswerte vorliegen (alle 200ms)
    if (speedSensors.update(data)) {
        Serial.print("Links -> m/s: ");
        Serial.print(data.leftSpeedMs, 3);
        Serial.print(" | km/h: ");
        Serial.print(data.leftSpeedKmh, 2);
        Serial.print("   ||   Rechts -> m/s: ");
        Serial.print(data.rightSpeedMs, 3);
        Serial.print(" | km/h: ");
        Serial.println(data.rightSpeedKmh, 2);
    }
}