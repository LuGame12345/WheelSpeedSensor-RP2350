# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

```bash
# Kompilieren
pio run

# Kompilieren und auf Board flashen
pio run --target upload

# Seriellen Monitor öffnen (115200 Baud)
pio device monitor

# Kompilieren + flashen + Monitor in einem Schritt
pio run --target upload && pio device monitor
```

## Zielplatform

- **Board:** Seeed Studio XIAO RP2350 (`seeed_xiao_rp2350`) als Basis, aber per `board_build.variant = rpipico2w` + CYW43-Flags in `platformio.ini` auf den Pico 2 W umgebogen (nötig für BLE). Die Seeed-Platform akzeptiert nur ihre eigenen Board-Namen, daher keine eigene Board-JSON möglich. `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` schaltet BTstack ein.
  - Physisch ist es ein Raspberry Pi Pico 2 W (siehe `Schaltungsdokumentation_auto.pdf`), es meldet sich per USB aber als XIAO RP2350. Die Konfiguration `rpipico2w` funktioniert nicht – XIAO-Konfiguration beibehalten. Pin-Nummern im Code sind GPIO-Nummern.
- **Chip:** RP2350 (Cortex-M33, 150 MHz), 2 MB Flash
- **Framework:** Arduino via `framework-arduinopico`
- **Platform-Quelle:** `https://github.com/Seeed-Studio/platform-seeedboards.git`

## Architektur

### WheelSpeedSensor-Bibliothek (`lib/WheelSpeedSensor/`)

Interrupt-gesteuerte Raddrehzahlmessung für zwei Räder (links/rechts).

**Messprinzip:** An jedem Messintervall (Standard: 200 ms) werden die seit dem letzten Intervall gezählten Flanken beider Encoder-Sensoren ausgelesen, Zähler zurückgesetzt und daraus m/s sowie km/h berechnet.

**Singleton-Einschränkung:** Die ISR-Routinen (`isrLeft`, `isrRight`) greifen über den statischen Zeiger `WheelSpeedSensor::instance` auf das Objekt zu. Es kann daher nur **eine Instanz** dieser Klasse gleichzeitig existieren. Ein zweites Objekt würde `instance` überschreiben.

**Interrupt-Sicherheit:** In `update()` werden die `volatile`-Zähler mit `noInterrupts()` / `interrupts()` atomar ausgelesen und zurückgesetzt.

**Konstruktor-Parameter:**
```cpp
WheelSpeedSensor(uint8_t pinLeft, uint8_t pinRight, float wheelDiameterMm, int slots)
// pinLeft=21, pinRight=20, Durchmesser=65 mm, Aussparungen=20
```

Die ISRs entprellen per `micros()` (Pulse < 2000 µs nach dem vorherigen werden verworfen, `setDebounceUs()`). Die ISR-Zähler laufen durch (kein Reset); `update()` bildet Differenzen. `getPulseCounts(l, r)` liefert die Gesamtpulse seit `begin()` (wird vom DriveController genutzt).

### MotorDriver-Bibliothek (`lib/MotorDriver/`)

Ansteuerung des L298N-Motortreibers (Pinbelegung siehe `Schaltungsdokumentation_auto.pdf`).

```cpp
MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena,
            uint8_t in3, uint8_t in4, uint8_t enb,
            uint8_t maxDutyPercent = 80, uint8_t minDutyPercent = 0)
// Links: IN1=0, IN2=1, ENA=2 · Rechts: IN3=4, IN4=5, ENB=3
```

- Geschwindigkeit in Prozent (float): `drive(left, right)`, `setLeft()`, `setRight()` mit -100..+100.
- Beide Motoren sind verpolt angeschlossen → `main.cpp` ruft `motors.setInverted(true, true)` auf.
- `stop()` lässt auslaufen (Enable aus), `brake()` bremst aktiv (IN-Pins beide HIGH).
- Der PWM-Tastgrad wird auf `maxDutyPercent` begrenzt (3–6-V-Motoren an 2S-Akku bis 8,4 V).
- Sollwerte ≠ 0 werden auf `minDutyPercent..maxDutyPercent` abgebildet (Anlauf-PWM; `main.cpp` nutzt 35..80). Unter ~35 % PWM laufen die Motoren nicht an.
- `setInverted(left, right)` kehrt die Richtung falsch herum verbauter Motoren um.
- **PWM global:** `begin()` setzt `analogWriteFreq(1000)` und `analogWriteRange(1000)`; das gilt in arduino-pico für alle `analogWrite`-Pins.

### DriveController-Bibliothek (`lib/DriveController/`)

Gleichlaufregelung über die Lichtschranken, damit der Roboter geradeaus fährt. Nutzt `MotorDriver` und `WheelSpeedSensor` per Referenz.

- Fehler = Pulsdifferenz (= Streckendifferenz) seit dem letzten neuen `drive()`-Sollwert, normiert auf das Sollverhältnis links:rechts.
- Korrektur = `kp * Fehler + Trim`; der Trim ist ein Integral, das die dauerhafte Motorabweichung lernt und über Fahrten hinweg behalten wird (getrennt je Vor-/Rückwärts-Kombination). Gelernt wird nur im RAM, nach einem Reset beginnt er bei 0.
- Es wird immer das schnellere Rad gebremst (max. 30 %).
- Standard: `kp = 0.08`, `ki = 0.05` (zur Laufzeit per `setGains()` änderbar).
- **Kurs-Trim** `setBias()`: verschiebt das Soll-Pulsverhältnis (+0.01 = links 1 % mehr Pulse → zieht nach rechts). Gleicht mechanische Unterschiede (Raddurchmesser, Schlupf) aus, die die Lichtschranken nicht sehen. Nur im RAM – gefundenen Wert in `setup()` fest eintragen.
- Regelung nur, wenn beide Sollwerte ≠ 0 (die Lichtschranken erkennen keine Drehrichtung).
- `update()` muss in jedem `loop()` aufgerufen werden (regelt alle 20 ms).

### UltrasonicSensor-Bibliothek (`lib/UltrasonicSensor/`)

HC-SR04, nicht-blockierend: `update()` triggert (10 µs an TRIG), die Echodauer wird per CHANGE-Interrupt an ECHO mit `micros()` gemessen. Neue Messung frühestens 25 ms nach der letzten, Timeout nach 60 ms → `isValid() == false`. Singleton wie WheelSpeedSensor.

```cpp
UltrasonicSensor(uint8_t trigPin, uint8_t echoPin)   // TRIG=18, ECHO=19
```

**Hardware:** ECHO liefert 5 V, GPIO19 ist nicht 5-V-tolerant → Spannungsteiler nötig (siehe PDF).

### LineSensor-Bibliothek (`lib/LineSensor/`)

3-Kanal-Spurfolgesensor, **LOW = schwarze Linie**, HIGH = keine Linie (am Roboter gemessen; per Konstruktor-Parameter `lineLevel` umstellbar). Pins als `INPUT` (Modul treibt selbst).

```cpp
LineSensor(uint8_t pinLeft, uint8_t pinCenter, uint8_t pinRight, uint8_t lineLevel = LOW)   // 7, 8, 9
```

- `read()` → `Reading{left, center, right}` (true = Linie)
- `getPosition(pos)` → -1.0 (links) .. 0.0 (Mitte) .. +1.0 (rechts); `false`, wenn keine Linie erkannt
- `readBits()` → Bitmuster L/M/R, z. B. `0b010` = mittig

### BleUart-Bibliothek (`lib/BleUart/`)

BLE-„Seriell“ über den Nordic UART Service (NUS) direkt mit BTstack (`att_db_util`, `att_server`). Ist ein `Stream` → `print()`/`read()` wie bei `Serial`. Singleton (BTstack-Callbacks global).

- Service `6E400001-…`, RX (write) `6E400002-…`, TX (notify) `6E400003-…`; Gerätename `Auto-Roboter`.
- Empfang: Write-Callback (Bluetooth-Kontext) → RX-Ringpuffer → `read()` in `loop()`.
- Senden: `write()` legt unter `__lockBluetooth()` in den TX-Ringpuffer und fordert `ATT_EVENT_CAN_SEND_NOW` an; dort wird in MTU-großen Stücken per `att_server_notify` verschickt. Ohne Verbindung/aktiviertes Notify wird verworfen (blockiert nie).
- Gedacht für Apps wie „Serial Bluetooth Terminal“ (Android) bzw. „Bluefruit Connect“/„nRF Connect“ (iOS).

### LineFollower-Bibliothek (`lib/LineFollower/`)

Autonome Linienfolge, bewusst einfach: nur nach der Linienposition lenken. Nutzt `LineSensor` und `DriveController` per Referenz.

- Nur Mitte (C) sieht die Linie → perfekt auf der Linie → geradeaus. Position aus `LineSensor::getPosition()` (−1 … +1).
- lenk = kp·pos + kd·Δpos/Δt; links = basis·(1+lenk), rechts = basis·(1−lenk). Startwerte: basis 40 %, kp 0.6, kd 0 (öffentliche Felder).
- **Lücke** (kein Sensor sieht die Linie, z. B. Mitte der X-Kreuzung): geradeaus mit Basistempo, bis die Linie wieder auftaucht (`inGap()`). Bewusst ohne Streckenmessung, ohne Suche, ohne Zustandsautomat (vom Nutzer so gewünscht).
- Hindernisse: `main.cpp` pausiert per `setPaused()`; `isActive()` zählt dort als „will vorwärts“.

### `src/main.cpp`

Standard Arduino-Sketch: `setup()` initialisiert Serial (115200), alle Sensoren/Motoren und `ble.begin()`. `loop()` wertet Befehle über Serial **und BLE** aus (`handleCommand()`: w/s/a/d = fahren, x = stopp, b = bremsen, r = Trim zurücksetzen, n = Regelung an/aus, h = Hilfe; Linienfolge: `l` an/aus, `k`/`K` Lenk-Kp, `m`/`M` Kd, `+`/`-` Basistempo während Linienfolge; Einstellen: `+`/`-` Tempo, `,`/`.` Kurs-Trim, `p`/`P` Kp, `i`/`I` Ki, `g` Werte anzeigen), ruft `sonar.update()`, `handleObstacle()` und `drive.update()` auf und gibt alle 200 ms Liniensensor (`#` = Linie, `.` = keine), Hindernisabstand (m), PWM, Pulse seit Fahrbefehl L/R, Pulsfehler, Trim und Geschwindigkeiten aus. Alle Ausgaben laufen über `out()` an USB und BLE.

**Modi / Not-Halt / Heartbeat** (Konzept: `docs/App_PS4_Controller_Konzept.pdf`): `robotMode` Manual|Line (`$MODE`), `$START` löst einen Not-Halt (nur Sperre, fährt nicht los) bzw. startet im Modus Line die Linienfolge. `$ESTOP` = `drive.brake()` + `estopActive` sperrt `$J`, Fahrtasten und `l`. `$J` wirkt nur im Modus Manual. Heartbeat: nach dem ersten `$HB` stoppt alles, wenn 1,5 s kein `$`-Befehl kommt. Status-JSON enthält `selected`, `estop`, `gap`, `blocked`, `lineSpeed`, `lineKp`; Protokollversion 2.

**App-Protokoll** (vollständig in `docs/BLE_APP_SCHNITTSTELLE.md`, bei Änderungen dort mitpflegen): Erweiterte Befehle `$NAME,args\n` (`LineParser` → `handleExtended()`): `$J,x,y` Joystick (−100..100, Totmann-Timeout 300 ms, Mischung links = y + x, rechts = y − x, × `joyMaxSpeed`), `$STOP`, `$BRAKE`, `$SPEED,n`, `$FMT,JSON|TEXT`, `$PING`, `$INFO`. Ausgaben laufen über `outEvent(json, text)`: USB immer Text, BLE Text oder (nach `$FMT,JSON`) eine JSON-Zeile pro Nachricht (`status` alle 200 ms, `obstacle`, `timeout`, `msg`, …). Bei BLE-Trennung zurück auf Text.

**Fahren nur bei gehaltener Taste** (`holdKey()`/`checkHold()`): w/a/s/d fahren nur, solange Befehle nachkommen. Erste Wiederholung wird bis 600 ms erwartet (Tastenwiederholung des Betriebssystems), danach Abstand ≤ 200 ms – sonst `releaseKey()` → Stopp. Zusätzlich versteht `PadParser` das Bluefruit-Connect-Control-Pad (`!B<Taste><1|0><CRC>`, 5/6/7/8 = hoch/runter/links/rechts) mit echtem Drücken/Loslassen, dann ohne Timeout. Trennt sich das Handy, werden die Motoren gestoppt.

Hinderniserkennung (`handleObstacle()`), nur bei Vorwärtsfahrt (manuell oder Linienfolge aktiv); nach einem Stopp geht es erst weiter, wenn der Weg über 10 cm frei ist (Linienfolge wird solange pausiert): zwischen 10 cm und dem Bremspunkt stufenlos gedrosselt (`setSpeedScale` 0.5 → 0.2), am Bremspunkt `drive.brake()`. Bremspunkt = `STOP_DISTANCE_M` (5 cm) + Geschwindigkeit × `REACTION_TIME_S` (0,15 s), damit er etwa bei 5 cm steht; Geschwindigkeit = Mittel beider Lichtschranken (alle 200 ms). Rückwärts und Drehen bleiben immer erlaubt und ungedrosselt. **Wichtig:** Der HC-SR04 liefert unter ~3 cm kein gültiges Echo – `obstacleDistance()` wertet ein fehlendes Echo nach einem nahen Messwert (< 10 cm) deshalb als 0 m statt als „frei“. Stoppgrenze nicht unter ~4 cm setzen.
