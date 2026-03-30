#pragma once
#include <Adafruit_PN532.h>
#include <SPI.h>

#include "config.h"

void rfidInit();

enum RfidWriteResult : uint8_t {
    RFID_OK_NEW,
    RFID_OK_OVERWRITE,
    RFID_OK_ADMIN,
    RFID_OK_REVOKED,
    RFID_DENIED,
    RFID_TIMEOUT,
    RFID_ERROR
};

enum RfidReadResult : uint8_t {
    RFID_READ_NONE,
    RFID_READ_ADMIN,
    RFID_READ_POINTS,
    RFID_READ_INVALID
};

struct RfidReadData {
    RfidReadResult result;
    uint16_t points;
};

RfidReadData rfidReadTag();
RfidWriteResult rfidWriteTag(uint8_t tagTypeToWrite, uint16_t points);
bool rfidBurnTag();
