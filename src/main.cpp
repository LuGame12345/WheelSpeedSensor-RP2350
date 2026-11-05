#include <Arduino.h>
#include "WheelSpeedSensor.h"
#include "MotorDriver.h"
#include "DriveController.h"
#include "UltrasonicSensor.h"
#include "LineSensor.h"
#include "BleUart.h"

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

// BLE-„Seriell“ (Nordic UART Service): gleiche Befehle und Ausgaben wie über USB
BleUart ble;
static constexpr const char *BLE_NAME = "Auto-Roboter";

int testSpeed = 50;                              // % für die Tastatursteuerung (+/- ändert)
static constexpr float SLOW_DISTANCE_M = 0.10f;  // ab hier langsamer fahren
static constexpr float STOP_DISTANCE_M = 0.05f;  // ab hier sofort stoppen
                                                 // (HC-SR04 misst unter ~3 cm nicht zuverlässig)
static constexpr float REACTION_TIME_S = 0.15f;  // Bremsweg ≈ Geschwindigkeit × diese Zeit
                                                 // (Messverzögerung + Ausrollen). Hält er zu nah
                                                 // an → erhöhen, hält er zu früh an → senken.
static constexpr float SLOW_FACTOR_MAX = 0.5f;   // Geschwindigkeit bei SLOW_DISTANCE_M
static constexpr float SLOW_FACTOR_MIN = 0.2f;   // Geschwindigkeit kurz vor STOP_DISTANCE_M

// Text gleichzeitig an USB-Seriell und BLE schicken
void out(const String &text) {
    Serial.print(text);
    ble.print(text);
}

void printSettings() {
    out("Tempo: " + String(testSpeed) + " % | Kurs: " + String(drive.getBias() * 100.0f, 1) +
        " % | Kp: " + String(drive.getKp(), 2) + " | Ki: " + String(drive.getKi(), 2) + "\n");
}

void printHelp() {
    out("Initialisiert. Steuerung: w=vor, s=zurück, a=links, d=rechts, x=stopp, b=bremsen\n");
    out("Regelung: r=Trim zurücksetzen, n=Regelung an/aus, h=Hilfe\n");
    out("Einstellen: +/- Tempo, ,/. Kurs links/rechts, p/P Kp, i/I Ki, g=Werte anzeigen\n");
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Warten auf Seriellen Monitor (Timeout 3s)

    motors.begin();
    motors.setInverted(true, true);     // beide Motoren sind verpolt angeschlossen
    speedSensors.begin();
    sonar.begin();
    lineSensor.begin();
    ble.begin(BLE_NAME);
    printHelp();
}

// Einen Steuerbefehl ausführen – egal ob er per USB oder BLE kam
void handleCommand(char c) {
    static bool syncOn = true;
    switch (c) {
        case 'w': drive.drive( testSpeed,  testSpeed); break;
        case 's': drive.drive(-testSpeed, -testSpeed); break;
        case 'a': drive.drive(-testSpeed,  testSpeed); break;
        case 'd': drive.drive( testSpeed, -testSpeed); break;
        case 'x': drive.stop();  break;
        case 'b': drive.brake(); break;
        case 'r': drive.resetTrim(); out("Trim zurückgesetzt\n"); break;
        case 'n':
            syncOn = !syncOn;
            drive.setSyncEnabled(syncOn);
            out(syncOn ? "Regelung AN\n" : "Regelung AUS\n");
            break;
        case 'h': printHelp(); break;

        // --- Einstellen fürs Geradeausfahren (wirkt sofort, auch während der Fahrt) ---
        case '+': testSpeed = min(testSpeed + 10, 100); out("Tempo: " + String(testSpeed) + " %\n"); break;
        case '-': testSpeed = max(testSpeed - 10, 10);  out("Tempo: " + String(testSpeed) + " %\n"); break;
        // Kurs-Trim: zieht er nach links → '.' (mehr nach rechts), zieht er nach rechts → ','
        case ',': drive.setBias(drive.getBias() - 0.005f); printSettings(); break;
        case '.': drive.setBias(drive.getBias() + 0.005f); printSettings(); break;
        case 'p': drive.setGains(max(drive.getKp() - 0.01f, 0.0f), drive.getKi()); printSettings(); break;
        case 'P': drive.setGains(drive.getKp() + 0.01f, drive.getKi());            printSettings(); break;
        case 'i': drive.setGains(drive.getKp(), max(drive.getKi() - 0.01f, 0.0f)); printSettings(); break;
        case 'I': drive.setGains(drive.getKp(), drive.getKi() + 0.01f);            printSettings(); break;
        case 'g': printSettings(); break;
        // Zeilenenden und Unbekanntes ignorieren (Apps senden oft "w\r\n")
    }
}

void handleCommands() {
    while (Serial.available()) handleCommand(Serial.read());
    while (ble.available())    handleCommand(ble.read());

    // Einmalig melden, sobald Bluetooth läuft (bzw. nach 5 s, falls nicht)
    static bool readyReported = false;
    if (!readyReported && ble.isReady()) {
        Serial.println("BLE bereit: sichtbar als \"" + String(BLE_NAME) + "\" (" + ble.address() + ")");
        readyReported = true;
    } else if (!readyReported && millis() > 5000) {
        Serial.println("FEHLER: Bluetooth ist nach 5 s nicht gestartet");
        readyReported = true;
    }

    // Sicherheit: Verbindung zum Handy weg → anhalten
    static bool wasConnected = false;
    bool connected = ble.isConnected();
    if (connected && !wasConnected) {
        Serial.println("BLE verbunden");
    } else if (!connected && wasConnected) {
        drive.stop();
        Serial.println("BLE getrennt → Motoren gestoppt");
    }
    wasConnected = connected;
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
        out("STOPP: Hindernis bei " + String(d, 3) + " m (Bremspunkt " +
            String(brakeDistance, 3) + " m bei " + String(currentSpeedMs, 2) + " m/s)\n");
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
    handleCommands();
    sonar.update();
    handleObstacle();
    drive.update();

    WheelSpeedSensor::SpeedData data;

    // Prüfen, ob neue Geschwindigkeitswerte vorliegen (alle 200ms)
    if (speedSensors.update(data)) {
        currentSpeedMs = (data.leftSpeedMs + data.rightSpeedMs) / 2.0f;

        // Linie: L/M/R, '#' = schwarze Linie, '.' = keine Linie
        LineSensor::Reading line = lineSensor.read();
        String msg = "Linie: ";
        msg += line.left   ? '#' : '.';
        msg += line.center ? '#' : '.';
        msg += line.right  ? '#' : '.';
        msg += " | Hindernis: ";
        msg += sonar.isValid() ? String(sonar.getDistanceM(), 3) + " m" : String("---");
        msg += " | PWM L/R: " + String(motors.getLeft(), 1) + "% / " + String(motors.getRight(), 1) + "%";
        uint32_t tripL, tripR;
        drive.getTripPulses(tripL, tripR);
        msg += " | Pulse L/R: " + String(tripL) + "/" + String(tripR);
        msg += " | Fehler: " + String(drive.getPulseError(), 1) + " Pulse";
        msg += " | Trim: " + String(drive.getTrim() * 100.0f, 1) + "%";
        msg += " || L: " + String(data.leftSpeedMs, 3) + " m/s | R: " + String(data.rightSpeedMs, 3) + " m/s\n";
        out(msg);
    }
}
