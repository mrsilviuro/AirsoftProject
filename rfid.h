#pragma once
#include <MFRC522.h>
#include <SPI.h>

#include "config.h"

extern MFRC522 mfrc522;

// Apelata o singura data in setup()
void rfidInit();

// Rezultatul unei operatii de scriere
enum RfidWriteResult : uint8_t {
    RFID_OK_NEW,        // Card nou scris cu succes
    RFID_OK_OVERWRITE,  // Card de puncte suprascris
    RFID_OK_ADMIN,      // Admin tag scris
    RFID_OK_REVOKED,    // Admin tag revocat (sters)
    RFID_DENIED,        // Incercare de a suprascrie Admin cu Points
    RFID_TIMEOUT,       // Niciun card detectat
    RFID_ERROR          // Eroare citire/scriere
};

// Incearca sa scrie pe cardul prezent.
// tagTypeToWrite: 1=Puncte, 2=Admin
// points: valoarea de puncte (ignorata daca e Admin)
// Returneaza rezultatul operatiei.
RfidWriteResult rfidWriteTag(uint8_t tagTypeToWrite, uint16_t points);

// Rezultatul unei operatii de citire
enum RfidReadResult : uint8_t {
    RFID_READ_NONE,    // Niciun card prezent
    RFID_READ_ADMIN,   // Card Admin valid
    RFID_READ_POINTS,  // Card de puncte valid
    RFID_READ_INVALID  // Card invalid / checksum gresit
};

struct RfidReadData {
    RfidReadResult result;
    uint16_t points;  // Valabil doar daca result == RFID_READ_POINTS
};

// Citeste cardul prezent si returneaza datele.
// Daca e card de puncte valid, il ARD inainte de a returna (anti-fraud).
RfidReadData rfidReadTag();

// Arde cardul (scrie zerouri) — apelata DOAR dupa ce stim ca avem proprietar valid
bool rfidBurnTag();
