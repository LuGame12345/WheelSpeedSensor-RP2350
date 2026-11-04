#pragma once
#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/dma.h"

class WheelSpeedSensor {
public:
    struct SpeedData {
        float leftSpeedMs;
        float leftSpeedKmh;
        float rightSpeedMs;
        float rightSpeedKmh;
    };

    // pinLeft/pinRight: GPIO-Pins der Lichtschranken
    // wheelDiameterMm: Raddurchmesser in mm
    // slots: Anzahl Aussparungen pro Umdrehung
    WheelSpeedSensor(uint8_t pinLeft, uint8_t pinRight,
                     float wheelDiameterMm = 65.0f, int slots = 20);

    // PIO-State-Machines und DMA-Kanäle starten
    void begin();

    // Gibt true zurück, wenn 200 ms vergangen sind und neue Daten bereitstehen
    bool update(SpeedData &data);

private:
    uint8_t _pinLeft;
    uint8_t _pinRight;
    float   _metersPerPulse;

    unsigned long _previousMillis = 0;
    static constexpr unsigned long _intervalMs = 200;

    PIO  _pio       = pio0;  // beide Kanäle auf PIO0
    uint _smLeft    = 0;
    uint _smRight   = 0;
    uint _pioOffset = 0;

    int      _dmaLeft   = -1;
    int      _dmaRight  = -1;
    // Letzter DMA-Zählerstand; Differenz = Pulse im Intervall
    uint32_t _lastLeft  = 0xFFFF'FFFFu;
    uint32_t _lastRight = 0xFFFF'FFFFu;

    // DMA-Zieladressen: der geschriebene Wert ist irrelevant
    volatile uint32_t _sinkLeft  = 0;
    volatile uint32_t _sinkRight = 0;

    void     _initSM (uint sm, uint8_t pin);
    void     _initDMA(int &chan, uint sm, volatile uint32_t &sink);
    uint32_t _pullPulses(int chan, uint32_t &last);
};
