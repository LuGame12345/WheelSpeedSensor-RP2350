#include "BleUart.h"
#include <btstack.h>
#include <cyw43_wrappers.h>   // __lockBluetooth() / __unlockBluetooth()

BleUart *BleUart::instance = nullptr;

// UUIDs als Bytefolge (Big Endian, wie von att_db_util erwartet)
static const uint8_t NUS_SERVICE[16] = {0x6E,0x40,0x00,0x01,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E};
static const uint8_t NUS_RX[16]      = {0x6E,0x40,0x00,0x02,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E};
static const uint8_t NUS_TX[16]      = {0x6E,0x40,0x00,0x03,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E};

static uint8_t advData[31];
static uint8_t scanData[31];
static btstack_packet_callback_registration_t hciEventRegistration;

void BleUart::begin(const char *deviceName) {
    instance = this;

    // --- GATT-Datenbank ---
    att_db_util_init();

    // GAP-Service mit Gerätenamen
    att_db_util_add_service_uuid16(ORG_BLUETOOTH_SERVICE_GENERIC_ACCESS);
    att_db_util_add_characteristic_uuid16(ORG_BLUETOOTH_CHARACTERISTIC_GAP_DEVICE_NAME,
        ATT_PROPERTY_READ, ATT_SECURITY_NONE, ATT_SECURITY_NONE,
        (uint8_t *)deviceName, strlen(deviceName));

    // Nordic UART Service
    att_db_util_add_service_uuid128(NUS_SERVICE);
    _rxValueHandle = att_db_util_add_characteristic_uuid128(NUS_RX,
        ATT_PROPERTY_WRITE | ATT_PROPERTY_WRITE_WITHOUT_RESPONSE | ATT_PROPERTY_DYNAMIC,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE, nullptr, 0);
    // NOTIFY legt automatisch den Client-Configuration-Deskriptor an (Handle = Wert + 1)
    _txValueHandle = att_db_util_add_characteristic_uuid128(NUS_TX,
        ATT_PROPERTY_NOTIFY | ATT_PROPERTY_DYNAMIC,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE, nullptr, 0);

    // --- Werbedaten: Flags + Name; Service-UUID in der Scan-Antwort ---
    size_t nameLen = min(strlen(deviceName), (size_t)26);
    int pos = 0;
    advData[pos++] = 0x02; advData[pos++] = BLUETOOTH_DATA_TYPE_FLAGS; advData[pos++] = 0x06;
    advData[pos++] = nameLen + 1; advData[pos++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&advData[pos], deviceName, nameLen); pos += nameLen;
    int advLen = pos;

    pos = 0;
    scanData[pos++] = 17; scanData[pos++] = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS;
    reverse_128(NUS_SERVICE, &scanData[pos]); pos += 16;   // in Werbedaten Little Endian
    int scanLen = pos;

    // --- BTstack starten ---
    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

    att_server_init(att_db_util_get_address(), _attRead, _attWrite);
    att_server_register_packet_handler(_packetHandler);

    hciEventRegistration.callback = _packetHandler;
    hci_add_event_handler(&hciEventRegistration);

    bd_addr_t nullAddr = {0};
    gap_advertisements_set_params(0x0030, 0x0030, 0, 0, nullAddr, 0x07, 0x00);
    gap_advertisements_set_data(advLen, advData);
    gap_scan_response_set_data(scanLen, scanData);
    gap_advertisements_enable(1);

    hci_power_control(HCI_POWER_ON);
}

// ---------------- BTstack-Callbacks (laufen im Bluetooth-Kontext) ----------------

uint16_t BleUart::_attRead(uint16_t, uint16_t handle, uint16_t, uint8_t *buffer, uint16_t) {
    BleUart *self = instance;
    // Client-Configuration-Deskriptor von TX: aktuellen Notify-Status melden
    if (self && handle == self->_txValueHandle + 1) {
        if (buffer) little_endian_store_16(buffer, 0, self->_notifyEnabled ? 1 : 0);
        return 2;
    }
    return 0;  // sonst nichts Lesbares (TX ist nur Notify)
}

int BleUart::_attWrite(uint16_t con, uint16_t handle, uint16_t, uint16_t, uint8_t *buffer, uint16_t size) {
    BleUart *self = instance;
    if (!self) return 0;

    if (handle == self->_rxValueHandle) {
        // Empfangene Bytes in den Ringpuffer (überzählige werden verworfen)
        for (uint16_t i = 0; i < size; i++) {
            uint16_t next = (self->_rxHead + 1) % RX_SIZE;
            if (next == self->_rxTail) break;
            self->_rx[self->_rxHead] = buffer[i];
            self->_rxHead = next;
        }
    } else if (handle == self->_txValueHandle + 1 && size >= 2) {
        // Handy hat Benachrichtigungen an-/abgeschaltet
        self->_notifyEnabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
        self->_conHandle     = con;
        if (self->_notifyEnabled) att_server_request_can_send_now_event(con);
    }
    return 0;
}

void BleUart::_packetHandler(uint8_t type, uint16_t, uint8_t *packet, uint16_t) {
    BleUart *self = instance;
    if (!self || type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            self->_ready = btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
            break;
        case ATT_EVENT_CONNECTED:
            self->_conHandle = att_event_connected_get_handle(packet);
            break;
        case ATT_EVENT_DISCONNECTED:
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            self->_conHandle     = 0xFFFF;
            self->_notifyEnabled = false;
            self->_txHead = self->_txTail = 0;   // alte Daten verwerfen
            break;
        case ATT_EVENT_CAN_SEND_NOW:
            self->_sendChunk();
            break;
    }
}

// Ein Paket (max. MTU - 3 Bytes) aus dem Sendepuffer verschicken.
// Läuft im Bluetooth-Kontext; fordert sich selbst erneut an, solange Daten da sind.
void BleUart::_sendChunk() {
    if (_conHandle == 0xFFFF || !_notifyEnabled || _txHead == _txTail) return;

    uint16_t maxLen = att_server_get_mtu(_conHandle) - 3;
    uint8_t  chunk[244];
    if (maxLen > sizeof(chunk)) maxLen = sizeof(chunk);

    uint16_t n = 0;
    uint16_t tail = _txTail;
    while (n < maxLen && tail != _txHead) {
        chunk[n++] = _tx[tail];
        tail = (tail + 1) % TX_SIZE;
    }
    _txTail = tail;

    att_server_notify(_conHandle, _txValueHandle, chunk, n);
    if (_txHead != _txTail) att_server_request_can_send_now_event(_conHandle);
}

String BleUart::address() const {
    bd_addr_t addr;
    gap_local_bd_addr(addr);
    return String(bd_addr_to_str(addr));
}

// ---------------- Stream-Schnittstelle (läuft in loop()) ----------------

int BleUart::available() {
    return (_rxHead + RX_SIZE - _rxTail) % RX_SIZE;
}

int BleUart::peek() {
    if (_rxHead == _rxTail) return -1;
    return _rx[_rxTail];
}

int BleUart::read() {
    if (_rxHead == _rxTail) return -1;
    uint8_t c = _rx[_rxTail];
    _rxTail = (_rxTail + 1) % RX_SIZE;
    return c;
}

size_t BleUart::write(uint8_t c) {
    return write(&c, 1);
}

size_t BleUart::write(const uint8_t *buffer, size_t size) {
    // Ohne verbundenes Handy (oder ohne aktivierte Benachrichtigungen) verwerfen,
    // damit loop() nie blockiert
    if (_conHandle == 0xFFFF || !_notifyEnabled) return size;

    __lockBluetooth();
    bool wasEmpty = (_txHead == _txTail);
    for (size_t i = 0; i < size; i++) {
        uint16_t next = (_txHead + 1) % TX_SIZE;
        if (next == _txTail) break;          // Puffer voll → Rest verwerfen
        _tx[_txHead] = buffer[i];
        _txHead = next;
    }
    if (wasEmpty) att_server_request_can_send_now_event(_conHandle);
    __unlockBluetooth();
    return size;
}
