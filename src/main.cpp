#include <Arduino.h>
#include "WheelSpeedSensor.h"
#include "MotorDriver.h"
#include "DriveController.h"
#include "UltrasonicSensor.h"
#include "LineSensor.h"
#include "BleUart.h"
#include "LineFollower.h"

// Lichtschranken: Pin Links = 21, Pin Rechts = 20, Raddurchmesser = 65 mm, Aussparungen = 20
WheelSpeedSensor speedSensors(21, 20, 65.0, 20);

// L298N laut Schaltungsdokumentation:
// Links:  IN1 = GPIO0, IN2 = GPIO1, ENA = GPIO2
// Rechts: IN3 = GPIO4, IN4 = GPIO5, ENB = GPIO3
// PWM-Tastgrad 35..100 %: volle Leistung (am Roboter geprüft, die Motoren vertragen 100 %),
// min. 35 %, damit die Motoren auch bei kleinen Sollwerten anlaufen
MotorDriver motors(0, 1, 2, 4, 5, 3, 100, 35);

// Gleichlaufregelung über die Lichtschranken
DriveController drive(motors, speedSensors);

// HC-SR04: TRIG = GPIO18, ECHO = GPIO19
UltrasonicSensor sonar(18, 19);

// Spurfolgesensor: links = GPIO7, Mitte = GPIO8, rechts = GPIO9 (LOW = schwarze Linie)
LineSensor lineSensor(7, 8, 9);

// BLE-„Seriell“ (Nordic UART Service): gleiche Befehle und Ausgaben wie über USB
BleUart ble;
static constexpr const char *BLE_NAME = "Auto-Roboter";

// Autonome Linienfolge (Taste 'l')
LineFollower lineFollower(lineSensor, drive);

int testSpeed = 50;                              // % für die Tastatursteuerung (+/- ändert)
static constexpr float SLOW_DISTANCE_M = 0.10f;  // ab hier langsamer fahren
static constexpr float STOP_DISTANCE_M = 0.05f;  // ab hier sofort stoppen
                                                 // (HC-SR04 misst unter ~3 cm nicht zuverlässig)
static constexpr float REACTION_TIME_S = 0.15f;  // Bremsweg ≈ Geschwindigkeit × diese Zeit
                                                 // (Messverzögerung + Ausrollen). Hält er zu nah
                                                 // an → erhöhen, hält er zu früh an → senken.
static constexpr float SLOW_FACTOR_MAX = 0.5f;   // Geschwindigkeit bei SLOW_DISTANCE_M
static constexpr float SLOW_FACTOR_MIN = 0.2f;   // Geschwindigkeit kurz vor STOP_DISTANCE_M

// ---------------- Ausgabe an USB und BLE ----------------
//
// USB bekommt immer lesbaren Text. BLE bekommt standardmäßig denselben Text;
// eine App kann mit "$FMT,JSON" auf maschinenlesbare JSON-Zeilen umschalten
// (eine Zeile pro Nachricht, '\n' am Ende). Siehe docs/BLE_APP_SCHNITTSTELLE.md.
bool bleJson = false;

// Text für einen JSON-String maskieren (Anführungszeichen, Backslash, Zeilenenden)
String jsonEscape(const String &text) {
    String r;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '"' || c == '\\') { r += '\\'; r += c; }
        else if (c == '\n' || c == '\r') { /* weglassen */ }
        else r += c;
    }
    return r;
}

// Ein Ereignis ausgeben: USB als Text, BLE als Text oder JSON-Zeile
void outEvent(const String &json, const String &text) {
    Serial.print(text);
    if (bleJson) ble.print(json + "\n");
    else         ble.print(text);
}

// Freier Text (Hilfe, Bestätigungen) → in JSON als {"t":"msg","text":"..."}
void out(const String &text) {
    outEvent("{\"t\":\"msg\",\"text\":\"" + jsonEscape(text) + "\"}", text);
}

void printSettings() {
    out("Tempo: " + String(testSpeed) + " % | Kurs: " + String(drive.getBias() * 100.0f, 1) +
        " % | Kp: " + String(drive.getKp(), 2) + " | Ki: " + String(drive.getKi(), 2) + "\n");
    out("Linie: Basis " + String(lineFollower.baseSpeed, 0) + " % | Lenk-Kp " + String(lineFollower.kp, 2) +
        " | Kd " + String(lineFollower.kd, 2) + "\n");
}

void printHelp() {
    out("Initialisiert. Steuerung (gedrückt halten): w=vor, s=zurück, a=links, d=rechts, x=stopp, b=bremsen\n");
    out("Regelung: r=Trim zurücksetzen, n=Regelung an/aus, h=Hilfe\n");
    out("Einstellen: +/- Tempo, ,/. Kurs links/rechts, p/P Kp, i/I Ki, g=Werte anzeigen\n");
    out("Linienfolge: l=start/stopp, k/K Lenk-Kp, m/M Kd, +/- Basistempo (während Linienfolge)\n");
    out("App: $J,x,y (Joystick), $STOP, $BRAKE, $SPEED,n, $FMT,JSON|TEXT, $PING, $INFO\n");
    out("App: $ESTOP, $START, $MODE,MANUAL|LINE, $LINESPEED,n, $LINEKP,v, $BIAS,v, $TRIMRESET, $HB\n");
    out("Kalibrierung: $CAL,AUTO | $CAL,MANUAL,<wert %> | $CAL,TEST[,tempo,sek] | $CAL,STOP | $CAL,GET\n");
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

// ---------------- Fahren nur solange die Taste gehalten wird ----------------
//
// Tastatur / Terminal-Apps: Beim Gedrückthalten schickt das Betriebssystem die Taste
// wiederholt (erst nach ~0,5 s, dann ~30×/s). Bleiben die Wiederholungen aus, ist
// die Taste losgelassen → stoppen.
// Bluefruit-Connect-Control-Pad: meldet „gedrückt“/„losgelassen“ direkt.
static constexpr unsigned long HOLD_FIRST_MS  = 600;  // bis die Tastenwiederholung einsetzt
static constexpr unsigned long HOLD_REPEAT_MS = 200;  // Abstand zwischen Wiederholungen + Reserve

// ---------------- Modus und Not-Halt (Konzept „iOS-App mit PS4-Controller“) ----------------
//
// Modus MANUELL: Fahren per Joystick ($J) bzw. Tasten.  Modus AUTONOM (LINE): $START / X
// startet die Linienfolge.  Not-Halt ($ESTOP / Dreieck): aktiv bremsen und ALLE Fahrbefehle
// sperren, bis $START kommt. Danach steht er im gewählten Modus und fährt nicht von selbst los.
enum class RobotMode { Manual, Line };
RobotMode robotMode       = RobotMode::Manual;
bool      estopActive     = false;
bool      obstacleBlocked = false;   // steht gerade wegen eines Hindernisses

const char *modeName() { return robotMode == RobotMode::Line ? "line" : "manual"; }

// ---------------- Kalibrierung ----------------
//
// Standard: AUTO – der Pico lernt den Motor-Ausgleich bei jeder Fahrt selbst.
// "$CAL,MANUAL,<wert>" – fester Ausgleich aus der App (gilt bis Neustart oder
//   BLE-Trennung, dann wieder AUTO). Nichts wird im Pico gespeichert.
// "$CAL,TEST[,<tempo>,<sekunden>]" – Testfahrt geradeaus (Standard 30 %, 5 s),
//   währenddessen kann der Wert live mit $CAL,MANUAL geändert werden.
bool          calTestActive = false;
unsigned long calTestStart  = 0;
unsigned long calTestMs     = 5000;
int           calTestSpeed  = 30;
unsigned long calLastReport = 0;

String calInfoJson(const char *event) {
    return String("{\"t\":\"cal\",\"event\":\"") + event + "\"" +
           ",\"mode\":\"" + (drive.isManualCalibration() ? "manual" : "auto") + "\"" +
           ",\"value\":" + String(drive.getManualValue(), 1) +
           ",\"learned\":" + String(drive.getLearnedValue(), 1) + "}";
}

void calAbort(const char *reason) {
    if (!calTestActive) return;
    calTestActive = false;
    drive.stop();
    outEvent(String("{\"t\":\"cal\",\"event\":\"test\",\"state\":\"aborted\",\"reason\":\"") + reason + "\"}",
             String("Testfahrt abgebrochen: ") + reason + "\n");
}

bool          joyActive = false;  // Joystick steuert gerade (siehe unten)
char          activeKey = 0;      // aktuell gehaltene Fahrtaste (0 = keine)
bool          repeating = false;  // Wiederholung schon angekommen?
bool          padHeld   = false;  // vom Control Pad gehalten (kein Timeout nötig)
unsigned long lastKeyMs = 0;

void applyMove(char key) {
    switch (key) {
        case 'w': drive.drive( testSpeed,  testSpeed); break;
        case 's': drive.drive(-testSpeed, -testSpeed); break;
        case 'a': drive.drive(-testSpeed,  testSpeed); break;
        case 'd': drive.drive( testSpeed, -testSpeed); break;
    }
}

void holdKey(char key, bool fromPad = false) {
    if (estopActive) return;              // Not-Halt: keine Fahrbefehle
    calAbort("manuell");
    if (key == activeKey) {
        repeating = true;                 // Wiederholung → weiterfahren
    } else {
        if (lineFollower.isActive()) lineFollower.stop();   // manuelle Übernahme
        robotMode = RobotMode::Manual;
        activeKey = key;
        repeating = false;
        joyActive = false;
        applyMove(key);
    }
    padHeld   = fromPad;
    lastKeyMs = millis();
}

void releaseKey() {
    calAbort("stopp");
    activeKey = 0;
    padHeld   = false;
    joyActive = false;
    if (lineFollower.isActive()) lineFollower.stop();
    drive.stop();
}

// In jedem loop(): Taste nicht mehr gehalten → stoppen
void checkHold() {
    if (!activeKey || padHeld) return;
    unsigned long timeout = repeating ? HOLD_REPEAT_MS : HOLD_FIRST_MS;
    if (millis() - lastKeyMs > timeout) releaseKey();
}

 // ---------------- Joystick (App) ----------------
//
// "$J,<x>,<y>" mit x, y = -100..100 (x: rechts positiv, y: vor positiv).
// Die App muss den Wert wiederholen, solange der Joystick ausgelenkt ist
// (empfohlen alle 50–100 ms). Kommt JOY_TIMEOUT_MS lang nichts → Stopp.
static constexpr unsigned long JOY_TIMEOUT_MS = 300;
static constexpr int           JOY_DEADZONE   = 8;    // kleine Auslenkungen ignorieren

int           joyMaxSpeed = 70;    // % bei voller Auslenkung ("$SPEED,<n>")
unsigned long lastJoyMs   = 0;

void joystick(int x, int y) {
    x = constrain(x, -100, 100);
    y = constrain(y, -100, 100);
    if (abs(x) < JOY_DEADZONE) x = 0;
    if (abs(y) < JOY_DEADZONE) y = 0;

    // Not-Halt oder Modus Autonom: Joystick fährt nicht (R2/Stick nur im Modus Manuell)
    if (estopActive || robotMode != RobotMode::Manual) return;

    // Während einer Testfahrt: Joystick in Ruhe ignorieren, Bewegung bricht den Test ab
    if (calTestActive) {
        if (x == 0 && y == 0) return;
        calAbort("joystick");
    }

    // Fahrtasten-Halten beenden, Joystick übernimmt
    activeKey = 0;
    padHeld   = false;

    if (x == 0 && y == 0) {           // losgelassen
        joyActive = false;
        drive.stop();
        return;
    }

    // Differentialantrieb mischen: vor/zurück + lenken
    float l = y + x;
    float r = y - x;
    float m = max(max(fabsf(l), fabsf(r)), 100.0f);   // auf ±100 normieren
    l = l / m * joyMaxSpeed;
    r = r / m * joyMaxSpeed;

    // Auf 2-%-Schritte runden, damit nicht jedes Zittern einen neuen Sollwert gibt
    l = roundf(l / 2.0f) * 2.0f;
    r = roundf(r / 2.0f) * 2.0f;

    drive.drive(l, r);
    joyActive = true;
    lastJoyMs = millis();
}

void checkJoystick() {
    if (joyActive && millis() - lastJoyMs > JOY_TIMEOUT_MS) {
        joyActive = false;
        drive.stop();
        outEvent("{\"t\":\"timeout\"}", "Joystick-Timeout → Stopp\n");
    }
}

// ---------------- Not-Halt / Start / Modus ----------------
void emergencyStop() {
    calAbort("nothalt");
    activeKey = 0;
    padHeld   = false;
    joyActive = false;
    if (lineFollower.isActive()) lineFollower.stop();
    drive.brake();                         // Motorklemmen kurzschließen → aktiv bremsen
    estopActive = true;
    outEvent("{\"t\":\"estop\",\"active\":true}", "NOT-HALT – gesperrt bis X ($START)\n");
}

void startCommand() {
    if (estopActive) {                     // 1. X nach Not-Halt: nur Sperre lösen
        estopActive = false;
        drive.stop();
        outEvent("{\"t\":\"estop\",\"active\":false}", "Not-Halt gelöst – bereit\n");
        return;
    }
    if (robotMode == RobotMode::Line) {
        calAbort("linienfolge");
        if (!lineFollower.isActive()) lineFollower.start();
        outEvent("{\"t\":\"start\",\"mode\":\"line\"}", "Start: Linienfolge\n");
    } else {
        outEvent("{\"t\":\"start\",\"mode\":\"manual\"}", "Start: Manuell bereit\n");
    }
}

void setMode(RobotMode m) {
    if (!estopActive) releaseKey();        // beim Wechsel anhalten (ausrollen)
    robotMode = m;
    outEvent("{\"t\":\"mode\",\"value\":\"" + String(modeName()) + "\"}",
             String("Modus: ") + (m == RobotMode::Line ? "Autonom (Linie)" : "Manuell") + "\n");
}

// ---------------- Kalibrier-Testfahrt ----------------
void calStartTest(int speed, unsigned long ms) {
    if (estopActive) {
        outEvent("{\"t\":\"error\",\"msg\":\"Not-Halt aktiv\",\"cmd\":\"CAL\"}", "Testfahrt: erst Not-Halt lösen\n");
        return;
    }
    if (robotMode != RobotMode::Manual) {
        outEvent("{\"t\":\"error\",\"msg\":\"nur im Modus Manuell\",\"cmd\":\"CAL\"}", "Testfahrt nur im Modus Manuell\n");
        return;
    }
    releaseKey();                          // laufende Fahrt beenden
    calTestSpeed  = constrain(speed, 10, 100);
    calTestMs     = constrain(ms, 1000UL, 10000UL);
    calTestStart  = millis();
    calLastReport = calTestStart;
    calTestActive = true;
    drive.drive(calTestSpeed, calTestSpeed);
    outEvent("{\"t\":\"cal\",\"event\":\"test\",\"state\":\"running\",\"speed\":" + String(calTestSpeed) +
             ",\"ms\":" + String(calTestMs) + ",\"left\":" + String(calTestMs) + "}",
             "Testfahrt: " + String(calTestSpeed) + " % für " + String(calTestMs / 1000.0f, 1) + " s\n");
}

void calTestUpdate() {
    if (!calTestActive) return;
    if (obstacleBlocked) { calAbort("hindernis"); return; }

    unsigned long elapsed = millis() - calTestStart;
    uint32_t pl, pr;
    drive.getTripPulses(pl, pr);

    if (elapsed >= calTestMs) {
        calTestActive = false;
        drive.stop();
        outEvent("{\"t\":\"cal\",\"event\":\"test\",\"state\":\"done\"" +
                 String(",\"pulses\":[") + String(pl) + "," + String(pr) + "]" +
                 ",\"mode\":\"" + (drive.isManualCalibration() ? "manual" : "auto") + "\"" +
                 ",\"value\":" + String(drive.getManualValue(), 1) +
                 ",\"learned\":" + String(drive.getLearnedValue(), 1) + "}",
                 "Testfahrt fertig – Pulse L/R: " + String(pl) + "/" + String(pr) + "\n");
        return;
    }
    if (millis() - calLastReport >= 500) {  // Fortschritt 2× pro Sekunde
        calLastReport = millis();
        outEvent("{\"t\":\"cal\",\"event\":\"test\",\"state\":\"running\",\"speed\":" + String(calTestSpeed) +
                 ",\"ms\":" + String(calTestMs) + ",\"left\":" + String(calTestMs - elapsed) +
                 ",\"pulses\":[" + String(pl) + "," + String(pr) + "]}",
                 "Testfahrt: noch " + String((calTestMs - elapsed) / 1000.0f, 1) + " s, Pulse L/R " + String(pl) + "/" + String(pr) + "\n");
    }
}

// ---------------- Heartbeat ----------------
// Die App schickt "$HB" alle 500 ms. Hat sie das einmal getan und bleibt danach jedes
// $-Kommando HB_TIMEOUT_MS lang aus, wird alles angehalten – auch die Linienfolge.
static constexpr unsigned long HB_TIMEOUT_MS = 1500;
bool          hbSeen   = false;
unsigned long lastHbMs = 0;

void checkHeartbeat() {
    if (hbSeen && millis() - lastHbMs > HB_TIMEOUT_MS) {
        hbSeen = false;
        if (!estopActive) releaseKey();
        outEvent("{\"t\":\"hbtimeout\"}", "Heartbeat fehlt → Stopp\n");
    }
}

// Bluefruit Control Pad: "!B" + Taste + '1'/'0' + Prüfsumme
//   5 = hoch, 6 = runter, 7 = links, 8 = rechts
struct PadParser {
    uint8_t buf[5];
    uint8_t len = 0;

    // true = Zeichen gehört zu einem Pad-Paket (nicht als Einzelbefehl auswerten)
    bool feed(char c) {
        if (len == 0 && c != '!') return false;
        buf[len++] = c;
        if (len == 2 && buf[1] != 'B') { len = 0; return true; }  // anderes Paket → ignorieren
        if (len < 5) return true;
        len = 0;

        uint8_t sum = buf[0] + buf[1] + buf[2] + buf[3];
        if ((uint8_t)~sum != buf[4]) return true;               // Prüfsumme falsch

        char key = 0;
        switch (buf[2]) {
            case '5': key = 'w'; break;
            case '6': key = 's'; break;
            case '7': key = 'a'; break;
            case '8': key = 'd'; break;
        }
        if (!key) return true;
        if (buf[3] == '1')            holdKey(key, true);
        else if (key == activeKey)    releaseKey();
        return true;
    }
};
PadParser serialPad, blePad;

// ---------------- Erweiterte Befehle für Apps: "$NAME,arg,arg\n" ----------------
void handleExtended(char *cmd) {
    // Name und Argumente trennen
    char *args = strchr(cmd, ',');
    if (args) *args++ = '\0';

    lastHbMs = millis();                   // jedes $-Kommando zählt als Lebenszeichen

    if (strcmp(cmd, "J") == 0) {
        int x = 0, y = 0;
        if (args) {
            x = atoi(args);
            char *second = strchr(args, ',');
            if (second) y = atoi(second + 1);
        }
        joystick(x, y);
    } else if (strcmp(cmd, "HB") == 0) {
        hbSeen = true;
    } else if (strcmp(cmd, "ESTOP") == 0) {
        emergencyStop();
    } else if (strcmp(cmd, "START") == 0) {
        startCommand();
    } else if (strcmp(cmd, "MODE") == 0 && args) {
        if      (strcmp(args, "LINE") == 0)   setMode(RobotMode::Line);
        else if (strcmp(args, "MANUAL") == 0) setMode(RobotMode::Manual);
    } else if (strcmp(cmd, "STOP") == 0) {
        if (!estopActive) releaseKey();    // Aus: ausrollen (beendet auch die Linienfolge)
    } else if (strcmp(cmd, "BRAKE") == 0) {
        if (!estopActive) releaseKey();
        drive.brake();
    } else if (strcmp(cmd, "LINEKP") == 0 && args) {
        lineFollower.kp = constrain(atof(args), 0.0f, 5.0f);
        outEvent("{\"t\":\"linekp\",\"value\":" + String(lineFollower.kp, 2) + "}",
                 "Lenk-Kp: " + String(lineFollower.kp, 2) + "\n");
    } else if (strcmp(cmd, "LINESPEED") == 0 && args) {
        lineFollower.baseSpeed = constrain(atof(args), 10.0f, 100.0f);
        outEvent("{\"t\":\"linespeed\",\"value\":" + String(lineFollower.baseSpeed, 0) + "}",
                 "Linien-Tempo: " + String(lineFollower.baseSpeed, 0) + " %\n");
    } else if (strcmp(cmd, "BIAS") == 0 && args) {
        drive.setBias(atof(args) / 100.0f);
        outEvent("{\"t\":\"bias\",\"value\":" + String(drive.getBias() * 100.0f, 1) + "}",
                 "Kurs-Trim: " + String(drive.getBias() * 100.0f, 1) + " %\n");
    } else if (strcmp(cmd, "CAL") == 0 && args) {
        // Unterbefehl und weitere Argumente trennen
        char *a2 = strchr(args, ',');
        if (a2) *a2++ = '\0';
        if (strcmp(args, "MANUAL") == 0 && a2) {
            drive.setManualCalibration(atof(a2));
            outEvent(calInfoJson("set"), "Kalibrierung: manuell " + String(drive.getManualValue(), 1) + " %\n");
        } else if (strcmp(args, "AUTO") == 0) {
            drive.setAutoCalibration();
            outEvent(calInfoJson("set"), "Kalibrierung: automatisch\n");
        } else if (strcmp(args, "TEST") == 0) {
            int speed = 30;
            float sec = 5.0f;
            if (a2) {
                speed = atoi(a2);
                char *a3 = strchr(a2, ',');
                if (a3) sec = atof(a3 + 1);
            }
            calStartTest(speed, (unsigned long)(sec * 1000.0f));
        } else if (strcmp(args, "STOP") == 0) {
            calAbort("app");
        } else if (strcmp(args, "GET") == 0) {
            outEvent(calInfoJson("info"),
                     String("Kalibrierung: ") + (drive.isManualCalibration() ? "manuell " : "automatisch ") +
                     String(drive.getManualValue(), 1) + " % (gelernt " + String(drive.getLearnedValue(), 1) + " %)\n");
        } else {
            outEvent("{\"t\":\"error\",\"msg\":\"unbekannter Befehl\",\"cmd\":\"CAL\"}", "Unbekannter $CAL-Befehl\n");
        }
    } else if (strcmp(cmd, "TRIMRESET") == 0) {
        if (joyActive || activeKey || lineFollower.isActive()) {
            outEvent("{\"t\":\"error\",\"msg\":\"nur im Stand\",\"cmd\":\"TRIMRESET\"}",
                     "Trim-Reset nur im Stand\n");
        } else {
            drive.resetTrim();
            out("Trim zurückgesetzt\n");
        }
    } else if (strcmp(cmd, "SPEED") == 0 && args) {
        joyMaxSpeed = constrain(atoi(args), 10, 100);
        outEvent("{\"t\":\"speed\",\"max\":" + String(joyMaxSpeed) + "}",
                 "Joystick-Tempo: " + String(joyMaxSpeed) + " %\n");
    } else if (strcmp(cmd, "FMT") == 0 && args) {
        bleJson = (strcmp(args, "JSON") == 0);
        outEvent("{\"t\":\"fmt\",\"value\":\"" + String(bleJson ? "JSON" : "TEXT") + "\"}",
                 String("BLE-Format: ") + (bleJson ? "JSON" : "TEXT") + "\n");
    } else if (strcmp(cmd, "PING") == 0) {
        outEvent("{\"t\":\"pong\"}", "PONG\n");
    } else if (strcmp(cmd, "INFO") == 0) {
        outEvent("{\"t\":\"info\",\"name\":\"" + String(BLE_NAME) + "\",\"proto\":3"
                 ",\"maxSpeed\":" + String(joyMaxSpeed) +
                 ",\"joyTimeoutMs\":" + String(JOY_TIMEOUT_MS) +
                 ",\"hbTimeoutMs\":" + String(HB_TIMEOUT_MS) +
                 ",\"selected\":\"" + modeName() + "\"" +
                 ",\"estop\":" + (estopActive ? "true" : "false") +
                 ",\"lineSpeed\":" + String(lineFollower.baseSpeed, 0) +
                 ",\"lineKp\":" + String(lineFollower.kp, 2) + "}",
                 "Info: " + String(BLE_NAME) + ", Protokoll 3, Joystick-Tempo " + String(joyMaxSpeed) + " %\n");
    } else {
        outEvent("{\"t\":\"error\",\"msg\":\"unbekannter Befehl\",\"cmd\":\"" + jsonEscape(cmd) + "\"}",
                 "Unbekannter Befehl: $" + String(cmd) + "\n");
    }
}

// Sammelt "$...\n"-Zeilen. true = Zeichen gehört zu einer solchen Zeile.
struct LineParser {
    char    buf[48];
    uint8_t len    = 0;
    bool    active = false;

    bool feed(char c) {
        if (!active) {
            if (c != '$') return false;
            active = true;
            len    = 0;
            return true;
        }
        if (c == '\n' || c == '\r' || c == ';') {   // Zeilenende (';' für Apps ohne Zeilenumbruch)
            buf[len] = '\0';
            active = false;
            if (len > 0) handleExtended(buf);
            return true;
        }
        if (len < sizeof(buf) - 1) buf[len++] = c;
        else active = false;                         // zu lang → verwerfen
        return true;
    }
};
LineParser serialLine, bleLine;

// Einen Steuerbefehl ausführen – egal ob er per USB oder BLE kam
void handleCommand(char c) {
    static bool syncOn = true;
    switch (c) {
        case 'w': case 's': case 'a': case 'd': holdKey(c); break;
        case 'x': releaseKey(); break;
        case 'b': releaseKey(); drive.brake(); break;
        case 'r': drive.resetTrim(); out("Trim zurückgesetzt\n"); break;
        case 'n':
            syncOn = !syncOn;
            drive.setSyncEnabled(syncOn);
            out(syncOn ? "Regelung AN\n" : "Regelung AUS\n");
            break;
        case 'h': printHelp(); break;

        // --- Linienfolge ---
        case 'l':
            if (lineFollower.isActive()) {
                lineFollower.stop();
                out("Linienfolge AUS\n");
            } else if (estopActive) {
                out("Not-Halt aktiv – erst X / $START\n");
            } else {
                activeKey = 0;
                padHeld   = false;
                joyActive = false;
                robotMode = RobotMode::Line;
                lineFollower.start();
                out("Linienfolge AN\n");
            }
            break;
        case 'k': lineFollower.kp = max(lineFollower.kp - 0.1f, 0.0f);  printSettings(); break;
        case 'K': lineFollower.kp += 0.1f;                               printSettings(); break;
        case 'm': lineFollower.kd = max(lineFollower.kd - 0.05f, 0.0f); printSettings(); break;
        case 'M': lineFollower.kd += 0.05f;                              printSettings(); break;

        // --- Einstellen fürs Geradeausfahren (wirkt sofort, auch während der Fahrt) ---
        // +/- : während der Linienfolge deren Basistempo, sonst das Tastentempo
        case '+':
            if (lineFollower.isActive()) { lineFollower.baseSpeed = min(lineFollower.baseSpeed + 10.0f, 100.0f); printSettings(); }
            else { testSpeed = min(testSpeed + 10, 100); out("Tempo: " + String(testSpeed) + " %\n"); }
            break;
        case '-':
            if (lineFollower.isActive()) { lineFollower.baseSpeed = max(lineFollower.baseSpeed - 10.0f, 10.0f); printSettings(); }
            else { testSpeed = max(testSpeed - 10, 10); out("Tempo: " + String(testSpeed) + " %\n"); }
            break;
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
    while (Serial.available()) {
        char c = Serial.read();
        if (!serialLine.feed(c) && !serialPad.feed(c)) handleCommand(c);
    }
    while (ble.available()) {
        char c = ble.read();
        if (!bleLine.feed(c) && !blePad.feed(c)) handleCommand(c);
    }
    checkHold();
    checkJoystick();
    checkHeartbeat();

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
        if (!estopActive) releaseKey();  // bei Not-Halt bleibt die Bremse an
        bleJson = false;                 // nächste Verbindung startet wieder im Textformat
        hbSeen  = false;
        drive.setAutoCalibration();      // manuelle Kalibrierung gilt nur pro Verbindung
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

    // Vorwärts gewollt? Manuell (Sollwert) oder Linienfolge (auch wenn gerade gebremst)
    bool forward = drive.isMovingForward() || lineFollower.isActive();
    if (!forward) {
        drive.setSpeedScale(1.0f);
        obstacleBlocked = false;
        lineFollower.setPaused(false);
        return;
    }

    // Früher bremsen, je schneller er fährt, damit er etwa bei STOP_DISTANCE_M steht
    float brakeDistance = STOP_DISTANCE_M + currentSpeedMs * REACTION_TIME_S;

    // Einmal gestoppt: erst weiter, wenn der Weg wieder über SLOW_DISTANCE_M frei ist
    // (sonst kriecht er in Ruckeln an das Hindernis heran)
    if (d < brakeDistance || (obstacleBlocked && d < SLOW_DISTANCE_M)) {
        drive.brake();
        lineFollower.setPaused(true);
        if (!obstacleBlocked) {
            outEvent("{\"t\":\"obstacle\",\"dist\":" + String(d, 3) + ",\"brakeAt\":" + String(brakeDistance, 3) +
                     ",\"speed\":" + String(currentSpeedMs, 2) + "}",
                     "STOPP: Hindernis bei " + String(d, 3) + " m (Bremspunkt " +
                     String(brakeDistance, 3) + " m bei " + String(currentSpeedMs, 2) + " m/s)\n");
        }
        obstacleBlocked = true;
        return;
    }
    if (obstacleBlocked) {
        obstacleBlocked = false;
        lineFollower.setPaused(false);
        if (lineFollower.isActive()) out("Weg frei → Linienfolge geht weiter\n");
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
    calTestUpdate();
    lineFollower.update();
    drive.update();

    WheelSpeedSensor::SpeedData data;

    // Prüfen, ob neue Geschwindigkeitswerte vorliegen (alle 200ms)
    if (speedSensors.update(data)) {
        currentSpeedMs = (data.leftSpeedMs + data.rightSpeedMs) / 2.0f;

        // Linie: L/M/R, '#' = schwarze Linie, '.' = keine Linie
        LineSensor::Reading line = lineSensor.read();
        String msg = estopActive ? "NOT-HALT | Modus: " : "Modus: ";
        if (lineFollower.isActive()) {
            msg += lineFollower.isPaused() ? "Linie (Hindernis)" : (lineFollower.inGap() ? "Linie (Lücke)" : "Linie");
        } else {
            msg += activeKey ? "manuell" : "steht";
        }
        msg += " | Linie: ";
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

        const char *mode = lineFollower.isActive() ? "line" : (joyActive ? "joystick" : (activeKey ? "keys" : "idle"));
        String json = "{\"t\":\"status\",\"ms\":" + String(millis()) +
            ",\"mode\":\"" + mode + "\"" +
            ",\"line\":[" + String(line.left) + "," + String(line.center) + "," + String(line.right) + "]" +
            ",\"dist\":" + (sonar.isValid() ? String(sonar.getDistanceM(), 3) : String("null")) +
            ",\"pwm\":[" + String(motors.getLeft(), 1) + "," + String(motors.getRight(), 1) + "]" +
            ",\"speed\":[" + String(data.leftSpeedMs, 3) + "," + String(data.rightSpeedMs, 3) + "]" +
            ",\"pulses\":[" + String(tripL) + "," + String(tripR) + "]" +
            ",\"err\":" + String(drive.getPulseError(), 1) +
            ",\"trim\":" + String(drive.getTrim() * 100.0f, 1) +
            ",\"maxSpeed\":" + String(joyMaxSpeed) +
            ",\"selected\":\"" + modeName() + "\"" +
            ",\"estop\":" + (estopActive ? "true" : "false") +
            ",\"gap\":" + (lineFollower.isActive() && lineFollower.inGap() ? "true" : "false") +
            ",\"blocked\":" + (obstacleBlocked ? "true" : "false") +
            ",\"lineSpeed\":" + String(lineFollower.baseSpeed, 0) +
            ",\"lineKp\":" + String(lineFollower.kp, 2) +
            ",\"cal\":\"" + (drive.isManualCalibration() ? "manual" : "auto") + "\"" +
            ",\"calValue\":" + String(drive.getManualValue(), 1) +
            ",\"calLearned\":" + String(drive.getLearnedValue(), 1) +
            ",\"calTest\":" + (calTestActive ? "true" : "false") + "}";
        outEvent(json, msg);
    }
}
