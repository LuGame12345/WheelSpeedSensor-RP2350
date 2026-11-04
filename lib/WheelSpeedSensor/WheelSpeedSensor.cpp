#include "WheelSpeedSensor.h"
#include "pulse_counter.pio.h"

WheelSpeedSensor::WheelSpeedSensor(uint8_t pinLeft, uint8_t pinRight,
                                     float wheelDiameterMm, int slots)
    : _pinLeft(pinLeft), _pinRight(pinRight)
{
    float circumference = (wheelDiameterMm / 1000.0f) * 3.14159265359f;
    _metersPerPulse = circumference / slots;
}

void WheelSpeedSensor::_initSM(uint sm, uint8_t pin) {
    pio_sm_config cfg = pulse_counter_program_get_default_config(_pioOffset);
    // IN-Basis setzen: "wait ... pin 0" bezieht sich auf GPIO[pin + 0]
    sm_config_set_in_pins(&cfg, pin);
    sm_config_set_clkdiv(&cfg, 1.0f);  // 125 MHz; Flanken-Mindestbreite ~16 ns

    gpio_pull_up(pin);                 // Pull-up für Lichtschranken-Ausgang
    pio_sm_init(_pio, sm, _pioOffset, &cfg);
    pio_sm_set_enabled(_pio, sm, true);
}

void WheelSpeedSensor::_initDMA(int &chan, uint sm, volatile uint32_t &sink) {
    chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    // DREQ: DMA transferiert nur, wenn FIFO-Daten vorliegen (= pro Puls)
    channel_config_set_dreq(&c, pio_get_dreq(_pio, sm, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);

    dma_channel_configure(chan, &c,
        (void*)&sink,                  // Schreibziel (Wert wird ignoriert)
        (const void*)&_pio->rxf[sm],   // Quelle: PIO RX-FIFO
        0xFFFF'FFFFu,                  // ~4 Mrd. Transfers ≈ nie ausgeschöpft
        true);                         // sofort starten
}

void WheelSpeedSensor::begin() {
    _pioOffset = pio_add_program(_pio, &pulse_counter_program);
    _smLeft  = pio_claim_unused_sm(_pio, true);
    _smRight = pio_claim_unused_sm(_pio, true);

    _initSM(_smLeft,  _pinLeft);
    _initSM(_smRight, _pinRight);
    _initDMA(_dmaLeft,  _smLeft,  _sinkLeft);
    _initDMA(_dmaRight, _smRight, _sinkRight);
}

uint32_t WheelSpeedSensor::_pullPulses(int chan, uint32_t &last) {
    // transfer_count zählt rückwärts: Differenz = Anzahl Pulse seit letztem Aufruf
    // uint32-Subtraktion ist Wraparound-sicher
    uint32_t current = dma_hw->ch[chan].transfer_count;
    uint32_t pulses  = last - current;
    last = current;
    return pulses;
}

bool WheelSpeedSensor::update(SpeedData &data) {
    unsigned long now = millis();
    if (now - _previousMillis < _intervalMs) return false;

    float elapsed     = (now - _previousMillis) / 1000.0f;
    _previousMillis   = now;

    uint32_t lPulses = _pullPulses(_dmaLeft,  _lastLeft);
    uint32_t rPulses = _pullPulses(_dmaRight, _lastRight);

    data.leftSpeedMs   = (lPulses * _metersPerPulse) / elapsed;
    data.leftSpeedKmh  = data.leftSpeedMs  * 3.6f;
    data.rightSpeedMs  = (rPulses * _metersPerPulse) / elapsed;
    data.rightSpeedKmh = data.rightSpeedMs * 3.6f;
    return true;
}
