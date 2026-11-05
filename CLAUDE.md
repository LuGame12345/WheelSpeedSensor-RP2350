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

- **Board:** Raspberry Pi Pico 2 W (`rpipico2w`)
- **Chip:** RP2350 (Cortex-M33, 150 MHz), 4 MB Flash, WLAN/Bluetooth (CYW43439)
- **Framework:** Arduino via `framework-arduinopico` (Earle Philhower Core)
- **Platform-Quelle:** `https://github.com/maxgerhardt/platform-raspberrypi.git`

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

### `src/main.cpp`

Standard Arduino-Sketch: `setup()` initialisiert Serial (115200) und ruft `speedSensors.begin()` auf; `loop()` ruft `speedSensors.update(data)` auf und gibt bei neuen Daten die Werte über Serial aus.
