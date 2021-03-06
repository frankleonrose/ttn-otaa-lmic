/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network.
 *
 * This uses OTAA (Over-the-air activation), where where a DevEUI and
 * application key is configured, which are used in an over-the-air
 * activation procedure where a DevAddr and session keys are
 * assigned/generated for use with all further communication.
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
 * g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
 * violated by this sketch when left running for longer)!

 * To use this sketch, first register your application and device with
 * the things network, to set or generate an AppEUI, DevEUI and AppKey.
 * Multiple devices can use the same AppEUI, but each device has its own
 * DevEUI and AppKey.
 *
 * Do not forget to define the radio type correctly in config.h.
 *
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#define _SERIAL(op) if (Serial) { Serial.op; }

void do_send(osjob_t* j);

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
// lsb copied from https://staging.thethingsnetwork.org/applications/70B3D57ED00001B0/getting-data
static const u1_t PROGMEM APPEUI[8]= { 0xB0, 0x01, 0x00, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]= { 0x78, 0x56, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t PROGMEM APPKEY[16] = { 0x4A, 0x7F, 0xF9, 0xF3, 0xC8, 0x53, 0x63, 0xBC, 0x51, 0x70, 0x51, 0x7E, 0x5A, 0x6C, 0x04, 0x5C };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

static uint8_t mydata[] = "Hello, world!";
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = 19,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 18,
  .dio = {16, 5, 6}, // Moved dio0 from 17 because of overlapping ExtInt4 (pin6)
};

unsigned long convertSec(long time) {
    return (time * US_PER_OSTICK) / 1000;
}

void onEvent (ev_t ev) {
    _SERIAL(print(os_getTime()));
    _SERIAL(print(": "));
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            _SERIAL(println(F("EV_SCAN_TIMEOUT")));
            break;
        case EV_BEACON_FOUND:
            _SERIAL(println(F("EV_BEACON_FOUND")));
            break;
        case EV_BEACON_MISSED:
            _SERIAL(println(F("EV_BEACON_MISSED")));
            break;
        case EV_BEACON_TRACKED:
            _SERIAL(println(F("EV_BEACON_TRACKED")));
            break;
        case EV_JOINING:
            _SERIAL(println(F("EV_JOINING")));
            break;
        case EV_JOINED:
            _SERIAL(println(F("EV_JOINED")));

            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            _SERIAL(println(F("EV_RFU1")));
            break;
        case EV_JOIN_FAILED:
            _SERIAL(println(F("EV_JOIN_FAILED")));
            break;
        case EV_REJOIN_FAILED:
            _SERIAL(println(F("EV_REJOIN_FAILED")));
            break;
            break;
        case EV_TXCOMPLETE:
            _SERIAL(println(F("EV_TXCOMPLETE (includes waiting for RX windows)")));
            if (LMIC.txrxFlags & TXRX_ACK)
              _SERIAL(println(F("Received ack")));
            if (LMIC.dataLen) {
              _SERIAL(println(F("Received ")));
              _SERIAL(println(LMIC.dataLen));
              _SERIAL(println(F(" bytes of payload")));
            }
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            _SERIAL(println(F("EV_LOST_TSYNC")));
            break;
        case EV_RESET:
            _SERIAL(println(F("EV_RESET")));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            _SERIAL(println(F("EV_RXCOMPLETE")));
            break;
        case EV_LINK_DEAD:
            _SERIAL(println(F("EV_LINK_DEAD")));
            break;
        case EV_LINK_ALIVE:
            _SERIAL(println(F("EV_LINK_ALIVE")));
            break;
         default:
            _SERIAL(println(F("Unknown event")));
            break;
    }
}

void do_send(osjob_t* j){
  _SERIAL(print(convertSec(os_getTime())));
  _SERIAL(print(": Do Send "));
  _SERIAL(println(LMIC_getSeqnoUp()));

    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        _SERIAL(println(F("OP_TXRXPEND, not sending")));
    } else {
        // Prepare upstream data transmission at the next possible time.
        //LMIC_setTxData2(1, mydata, sizeof(mydata)-1, 0);
        size_t nData = sizeof(mydata)-1;
        LMIC_setTxData2(1, mydata, nData, 0);
        _SERIAL(println(F("Packet queued")));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
    Serial.begin(9600);
    delay(100);     // per sample code on RF_95 test
    _SERIAL(println(F("Starting")));

    #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
    #endif

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    // Set data rate and transmit power (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Select SubBand
    LMIC_selectSubBand(1); // must align with subband on gateway. Zero origin

    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}
