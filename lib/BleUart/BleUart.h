#pragma once
#include <Arduino.h>

// BLE-„Seriell“ über den Nordic UART Service (NUS) mit dem Funkchip des Pico 2 W.
//
// Verhält sich wie Serial (ist ein Stream): print()/println() senden ans Handy,
// available()/read() liefern, was das Handy schickt.
// Kompatibel mit Apps wie „Serial Bluetooth Terminal“ (Android),
// „Bluefruit Connect“ / „nRF Connect“ / „LightBlue“ (iOS + Android).
//
//   Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
//   RX (Handy → Roboter, write):  6E400002-...
//   TX (Roboter → Handy, notify): 6E400003-...
//
// Singleton-Einschränkung: nur eine Instanz möglich (BTstack-Callbacks sind global).
class BleUart : public Stream {
public:
    // Name, unter dem der Roboter in der App erscheint
    void begin(const char *deviceName);

    bool isConnected() const { return _conHandle != 0xFFFF; }

    // true, sobald der Funkchip läuft und der Roboter sichtbar ist (Advertising)
    bool isReady() const { return _ready; }

    // Bluetooth-Adresse als Text (z. B. "2C:CF:67:12:34:56"), erst gültig wenn isReady()
    String address() const;

    // Stream-Schnittstelle
    int    available() override;
    int    read() override;
    int    peek() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void   flush() override {}
    using Print::write;

private:
    static constexpr size_t RX_SIZE = 128;
    static constexpr size_t TX_SIZE = 1024;

    // Empfangspuffer (Producer: BTstack-Callback, Consumer: loop)
    uint8_t           _rx[RX_SIZE];
    volatile uint16_t _rxHead = 0;
    volatile uint16_t _rxTail = 0;

    // Sendepuffer (unter Bluetooth-Lock benutzt)
    uint8_t  _tx[TX_SIZE];
    uint16_t _txHead = 0;
    uint16_t _txTail = 0;

    volatile uint16_t _conHandle     = 0xFFFF;
    volatile bool     _notifyEnabled = false;
    volatile bool     _ready         = false;
    uint16_t          _rxValueHandle = 0;
    uint16_t          _txValueHandle = 0;

    void _sendChunk();

    static BleUart *instance;
    static uint16_t _attRead(uint16_t con, uint16_t handle, uint16_t offset, uint8_t *buffer, uint16_t size);
    static int      _attWrite(uint16_t con, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buffer, uint16_t size);
    static void     _packetHandler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);
};
