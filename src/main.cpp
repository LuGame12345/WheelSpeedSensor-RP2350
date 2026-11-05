#include <Arduino.h>
#include "WheelSpeedSensor.h"
#include "MotorDriver.h"
#include "DriveController.h"
#include "UltrasonicSensor.h"
#include "LineSensor.h"

// Lichtschranken: Pin Links = 21, Pin Rechts = 20, Raddurchmesser = 65 mm, Aussparungen = 20
WheelSpeedSensor speedSensors(21, 20, 65.0, 20);

// L298N laut Schaltungsdokumentation:
// Links:  IN1 = GPIO0, IN2 = GPIO1, ENA = GPIO2
// Rechts: IN3 = GPIO4, IN4 = GPIO5, ENB = GPIO3
// PWM-Tastgrad 35..80 %: max. 80 % (3–6-V-Motoren an 2S-Akku),
// min. 35 %, damit die Motoren auch bei kleinen Sollwerten anlaufen
MotorDriver motors(0, 1, 2, 4, 5, 3, 80, 35);

// Gleichlaufregelung über die Lichtschranken
DriveController drive(motors, speedSensors);

// HC-SR04: TRIG = GPIO18, ECHO = GPIO19
UltrasonicSensor sonar(18, 19);

// Spurfolgesensor: links = GPIO7, Mitte = GPIO8, rechts = GPIO9 (LOW = schwarze Linie)
LineSensor lineSensor(7, 8, 9);

static constexpr int   TEST_SPEED      = 50;     // % für die Tastatursteuerung
static constexpr float SLOW_DISTANCE_M = 0.10f;  // ab hier langsamer fahren
static constexpr float STOP_DISTANCE_M = 0.05f;  // ab hier sofort stoppen
                                                 // (HC-SR04 misst unter ~3 cm nicht zuverlässig)
static constexpr float REACTION_TIME_S = 0.15f;  // Bremsweg ≈ Geschwindigkeit × diese Zeit
                                                 // (Messverzögerung + Ausrollen). Hält er zu nah
                                                 // an → erhöhen, hält er zu früh an → senken.
static constexpr float SLOW_FACTOR_MAX = 0.5f;   // Geschwindigkeit bei SLOW_DISTANCE_M
static constexpr float SLOW_FACTOR_MIN = 0.2f;   // Geschwindigkeit kurz vor STOP_DISTANCE_M

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Warten auf Seriellen Monitor (Timeout 3s)

    motors.begin();
    motors.setInverted(true, true);     // beide Motoren sind verpolt angeschlossen
    speedSensors.begin();
    sonar.begin();
    lineSensor.begin();
    Serial.println("Initialisiert. Steuerung: w=vor, s=zurück, a=links, d=rechts, x=stopp, b=bremsen");
    Serial.println("Regelung: r=Trim zurücksetzen, n=Regelung an/aus");
}

void handleSerialCommands() {
    static bool syncOn = true;
    while (Serial.available()) {
        switch (Serial.read()) {
            case 'w': drive.drive( TEST_SPEED,  TEST_SPEED); break;
            case 's': drive.drive(-TEST_SPEED, -TEST_SPEED); break;
            case 'a': drive.drive(-TEST_SPEED,  TEST_SPEED); break;
            case 'd': drive.drive( TEST_SPEED, -TEST_SPEED); break;
            case 'x': drive.stop();  break;
            case 'b': drive.brake(); break;
            case 'r': drive.resetTrim(); Serial.println("Trim zurückgesetzt"); break;
            case 'n':
                syncOn = !syncOn;
                drive.setSyncEnabled(syncOn);
                Serial.println(syncOn ? "Regelung AN" : "Regelung AUS");
                break;
        }
    }
}

// Aktuelle Fahrgeschwindigkeit (Mittel beider Räder), alle 200 ms von den Lichtschranken
float currentSpeedMs = 0.0f;

// Abstand zum Hindernis in Metern (INFINITY = frei).
// Kommt kein Echo, obwohl das Hindernis zuletzt nah war, ist es zu nah zum
// Messen (HC-SR04 unter ~3 cm) → als 0 m werten statt als „frei“.
float obstacleDistance() {
    static float lastValid = INFINITY;
    if (sonar.isValid()) {
        lastValid = sonar.getDistanceM();
        return lastValid;
    }
    return (lastValid < SLOW_DISTANCE_M) ? 0.0f : INFINITY;
}

// Langsamer fahren bzw. stoppen, wenn vorne ein Hindernis ist.
// Wird in jedem loop() geprüft, damit auch ein neuer 'w'-Befehl sofort abgefangen wird.
// Rückwärts und Drehen bleiben immer erlaubt (und ungedrosselt), damit man wegkommt.
void handleObstacle() {
    float d = obstacleDistance();

    if (!drive.isMovingForward()) {
        drive.setSpeedScale(1.0f);
        return;
    }

    // Früher bremsen, je schneller er fährt, damit er etwa bei STOP_DISTANCE_M steht
    float brakeDistance = STOP_DISTANCE_M + currentSpeedMs * REACTION_TIME_S;

    if (d < brakeDistance) {
        drive.brake();
        Serial.print("STOPP: Hindernis bei ");
        Serial.print(d, 3);
        Serial.print(" m (Bremspunkt ");
        Serial.print(brakeDistance, 3);
        Serial.print(" m bei ");
        Serial.print(currentSpeedMs, 2);
        Serial.println(" m/s)");
        return;
    }

    if (d < SLOW_DISTANCE_M) {
        // Stufenlos: bei 10 cm SLOW_FACTOR_MAX, am Bremspunkt SLOW_FACTOR_MIN
        float t = (d - brakeDistance) / (SLOW_DISTANCE_M - brakeDistance);
        t = constrain(t, 0.0f, 1.0f);
        drive.setSpeedScale(SLOW_FACTOR_MIN + t * (SLOW_FACTOR_MAX - SLOW_FACTOR_MIN));
    } else {
        drive.setSpeedScale(1.0f);
    }
}

void loop() {
    handleSerialCommands();
    sonar.update();
    handleObstacle();
    drive.update();

    WheelSpeedSensor::SpeedData data;

    // Prüfen, ob neue Geschwindigkeitswerte vorliegen (alle 200ms)
    if (speedSensors.update(data)) {
        currentSpeedMs = (data.leftSpeedMs + data.rightSpeedMs) / 2.0f;

        // Linie: L/M/R, '#' = schwarze Linie, '.' = keine Linie
        LineSensor::Reading line = lineSensor.read();
        Serial.print("Linie: ");
        Serial.print(line.left   ? '#' : '.');
        Serial.print(line.center ? '#' : '.');
        Serial.print(line.right  ? '#' : '.');
        Serial.print(" | Hindernis: ");
        if (sonar.isValid()) {
            Serial.print(sonar.getDistanceM(), 3);
            Serial.print(" m");
        } else {
            Serial.print("--- ");
        }
        Serial.print(" | PWM L/R: ");
        Serial.print(motors.getLeft(), 1);
        Serial.print("% / ");
        Serial.print(motors.getRight(), 1);
        Serial.print("% | Fehler: ");
        Serial.print(drive.getPulseError(), 1);
        Serial.print(" Pulse | Trim: ");
        Serial.print(drive.getTrim() * 100.0f, 1);
        Serial.print("% || L: ");
        Serial.print(data.leftSpeedMs, 3);
        Serial.print(" m/s | R: ");
        Serial.print(data.rightSpeedMs, 3);
        Serial.println(" m/s");
    }
}
