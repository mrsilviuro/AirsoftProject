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

enum RfidExportResult : uint8_t {
    EXPORT_OK,
    EXPORT_NO_TAG,       // niciun card prezent
    EXPORT_NOT_ADMIN,    // cardul nu e admin (gol / puncte / altceva)
    EXPORT_WRITE_FAIL    // eroare de scriere
};
// Scrie blob-ul de stare pe un card admin (sectoarele 2..7). Verifica intai ca e admin tag.
RfidExportResult rfidExportState(const uint8_t* blob, uint16_t len);

enum RfidImportResult : uint8_t {
    IMPORT_OK,
    IMPORT_NO_TAG,
    IMPORT_NOT_ADMIN,
    IMPORT_READ_FAIL
};
// Citeste blob-ul de stare de pe un card admin (sectoarele 2..7) in 'blob'.
RfidImportResult rfidImportState(uint8_t* blob, uint16_t len);
