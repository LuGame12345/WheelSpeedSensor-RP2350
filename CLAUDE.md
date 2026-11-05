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

- **Board:** Seeed Studio XIAO RP2350 (`seeed_xiao_rp2350`)
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

### MotorDriver-Bibliothek (`lib/MotorDriver/`)

Ansteuerung des L298N-Motortreibers (Pinbelegung siehe `Schaltungsdokumentation.pdf`).

```cpp
MotorDriver(uint8_t in1, uint8_t in2, uint8_t ena,
            uint8_t in3, uint8_t in4, uint8_t enb, uint8_t maxDutyPercent = 80)
// Links: IN1=0, IN2=1, ENA=2 · Rechts: IN3=4, IN4=5, ENB=3
```

- Geschwindigkeit in Prozent: `drive(left, right)`, `setLeft()`, `setRight()` mit -100..+100.
- `stop()` lässt auslaufen (Enable aus), `brake()` bremst aktiv (IN-Pins beide HIGH).
- Der PWM-Tastgrad wird auf `maxDutyPercent` begrenzt (3–6-V-Motoren an 2S-Akku bis 8,4 V).
- `setInverted(left, right)` kehrt die Richtung falsch herum verbauter Motoren um.
- **PWM global:** `begin()` setzt `analogWriteFreq(1000)` und `analogWriteRange(1000)`; das gilt in arduino-pico für alle `analogWrite`-Pins.

### `src/main.cpp`

Standard Arduino-Sketch: `setup()` initialisiert Serial (115200), `motors.begin()` und `speedSensors.begin()`. `loop()` wertet Tastaturbefehle über Serial aus (w/s/a/d = fahren, x = stopp, b = bremsen) und gibt alle 200 ms Motor-Sollwerte und gemessene Geschwindigkeiten aus.
