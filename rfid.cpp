#include "rfid.h"

static Adafruit_PN532 nfc(PIN_RFID_SDA);  // CS/SS pin

static uint8_t defaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t customKey[6]  = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};

// ============================================================
// rfidInit()
// ============================================================
void rfidInit() {
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI);
    nfc.begin();

    uint32_t ver = nfc.getFirmwareVersion();
    if (!ver) {
        Serial.println("[RFID] PN532 negasit!");
        return;
    }

    Serial.print("[RFID] PN532 v");
    Serial.print((ver >> 16) & 0xFF);
    Serial.print('.');
    Serial.println((ver >> 8) & 0xFF);

    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(0x03);
    Serial.println("[RFID] Initializat.");
}

// ============================================================
// rfidReadTag()
// ============================================================
RfidReadData rfidReadTag() {
    RfidReadData data = {RFID_READ_NONE, 0};

    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50)) {
        return data;
    }

    const uint8_t blockAddr = RFID_BLOCK_ADDR;

    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockAddr, 0, customKey)) {
        Serial.println("[RFID] Auth esuat");
        data.result = RFID_READ_INVALID;
        return data;
    }

    uint8_t buf[16];
    if (!nfc.mifareclassic_ReadDataBlock(blockAddr, buf)) {
        Serial.println("[RFID] Read esuat");
        data.result = RFID_READ_INVALID;
        return data;
    }

    if (buf[0] != RFID_MAGIC_BYTE) {
        Serial.print("[RFID] Magic byte gresit: 0x");
        Serial.println(buf[0], HEX);
        data.result = RFID_READ_INVALID;
        return data;
    }

    uint8_t cs = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
    for (uint8_t i = 0; i < uidLength; i++) cs ^= uid[i];
    if (cs != buf[4]) {
        Serial.print("[RFID] Checksum gresit: calc=0x");
        Serial.print(cs, HEX);
        Serial.print(" citit=0x");
        Serial.println(buf[4], HEX);
        data.result = RFID_READ_INVALID;
        return data;
    }

    uint8_t tagType = buf[1];
    uint16_t points = (buf[2] << 8) | buf[3];

    if (tagType == 2) {
        data.result = RFID_READ_ADMIN;
        return data;
    }

    if (tagType == 1) {
        if (points == 0) {
            data.result = RFID_READ_INVALID;
            return data;
        }
        data.result = RFID_READ_POINTS;
        data.points = points;
        return data;
    }

    data.result = RFID_READ_INVALID;
    return data;
}

// ============================================================
// rfidBurnTag()
// ============================================================
bool rfidBurnTag() {
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50)) {
        return false;
    }

    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, RFID_BLOCK_ADDR, 0, customKey)) {
        return false;
    }

    uint8_t zeroBlock[16] = {0};
    return nfc.mifareclassic_WriteDataBlock(RFID_BLOCK_ADDR, zeroBlock);
}

// ============================================================
// EXPORT/IMPORT stare joc — sectoarele 2..7 (blocuri de date, sarim trailerele).
// Sectorul 1 (bloc 4) ramane identitatea de admin, neatins.
// ============================================================
static const uint8_t EXP_BLOCKS[] = {
    8, 9, 10,  12, 13, 14,  16, 17, 18,  20, 21, 22,  24, 25, 26,  28, 29, 30,  32, 33, 34
};
static const uint8_t EXP_BLOCK_COUNT = sizeof(EXP_BLOCKS);   // 21 blocuri = 336 octeti

RfidExportResult rfidExportState(const uint8_t* blob, uint16_t len) {
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50))
        return EXPORT_NO_TAG;

    // 1) Verificam ca e admin tag: bloc 4, cheia custom, magic + type==2
    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, RFID_BLOCK_ADDR, 0, customKey))
        return EXPORT_NOT_ADMIN;
    uint8_t buf[16];
    if (!nfc.mifareclassic_ReadDataBlock(RFID_BLOCK_ADDR, buf))
        return EXPORT_NOT_ADMIN;
    if (buf[0] != RFID_MAGIC_BYTE || buf[1] != 2)
        return EXPORT_NOT_ADMIN;

    // 2) Scriem blob-ul in sectoarele 2..7
    if ((uint16_t)EXP_BLOCK_COUNT * 16 < len) return EXPORT_WRITE_FAIL;   // nu incape
    uint16_t off = 0;
    int16_t lastSector = 1;   // sectorul 1 e deja autentificat
    for (uint8_t bi = 0; bi < EXP_BLOCK_COUNT && off < len; bi++) {
        uint8_t blk = EXP_BLOCKS[bi];
        int16_t sector = blk / 4;
        if (sector != lastSector) {
            if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blk, 0, defaultKey))
                return EXPORT_WRITE_FAIL;
            lastSector = sector;
        }
        uint8_t b[16] = {0};
        uint8_t n = (len - off >= 16) ? 16 : (uint8_t)(len - off);
        memcpy(b, blob + off, n);
        if (!nfc.mifareclassic_WriteDataBlock(blk, b))
            return EXPORT_WRITE_FAIL;
        off += 16;
    }
    return EXPORT_OK;
}

RfidImportResult rfidImportState(uint8_t* blob, uint16_t len) {
    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50))
        return IMPORT_NO_TAG;

    // 1) Verificam ca e admin tag
    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, RFID_BLOCK_ADDR, 0, customKey))
        return IMPORT_NOT_ADMIN;
    uint8_t buf[16];
    if (!nfc.mifareclassic_ReadDataBlock(RFID_BLOCK_ADDR, buf))
        return IMPORT_NOT_ADMIN;
    if (buf[0] != RFID_MAGIC_BYTE || buf[1] != 2)
        return IMPORT_NOT_ADMIN;

    // 2) Citim blob-ul din sectoarele 2..7
    if ((uint16_t)EXP_BLOCK_COUNT * 16 < len) return IMPORT_READ_FAIL;
    uint16_t off = 0;
    int16_t lastSector = 1;
    for (uint8_t bi = 0; bi < EXP_BLOCK_COUNT && off < len; bi++) {
        uint8_t blk = EXP_BLOCKS[bi];
        int16_t sector = blk / 4;
        if (sector != lastSector) {
            if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blk, 0, defaultKey))
                return IMPORT_READ_FAIL;
            lastSector = sector;
        }
        uint8_t b[16];
        if (!nfc.mifareclassic_ReadDataBlock(blk, b))
            return IMPORT_READ_FAIL;
        uint8_t n = (len - off >= 16) ? 16 : (uint8_t)(len - off);
        memcpy(blob + off, b, n);
        off += 16;
    }
    return IMPORT_OK;
}

// ============================================================
// rfidWriteTag()
// ============================================================
RfidWriteResult rfidWriteTag(uint8_t tagTypeToWrite, uint16_t points) {
    const uint8_t blockAddr = RFID_BLOCK_ADDR;

    uint8_t uid[7] = {0};
    uint8_t uidLength = 0;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50)) {
        return RFID_TIMEOUT;
    }

    bool isNewCard = false;
    uint8_t currentType = 0;

    // Incercam cheia custom
    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockAddr, 0, customKey)) {
        // Esuat — incercam cheia de fabrica (card nou)
        if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50)) {
            return RFID_ERROR;
        }
        if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockAddr, 0, defaultKey)) {
            return RFID_ERROR;
        }
        isNewCard = true;
    }

    // Citim tipul curent (daca nu e card nou)
    if (!isNewCard) {
        uint8_t buf[16];
        if (nfc.mifareclassic_ReadDataBlock(blockAddr, buf)) {
            if (buf[0] == RFID_MAGIC_BYTE) {
                currentType = buf[1];
            }
        }
    }

    // Verificare permisiuni
    if (tagTypeToWrite == 1 && currentType == 2) {
        return RFID_DENIED;
    }

    bool revoking = (tagTypeToWrite == 2 && currentType == 2);

    // Construim blocul de date
    uint8_t dataBlock[16] = {0};
    if (!revoking) {
        dataBlock[0] = RFID_MAGIC_BYTE;
        dataBlock[1] = tagTypeToWrite;
        dataBlock[2] = (points >> 8) & 0xFF;
        dataBlock[3] = points & 0xFF;

        uint8_t cs = dataBlock[0] ^ dataBlock[1] ^ dataBlock[2] ^ dataBlock[3];
        for (uint8_t i = 0; i < uidLength; i++) cs ^= uid[i];
        dataBlock[4] = cs;
    }

    if (!nfc.mifareclassic_WriteDataBlock(blockAddr, dataBlock)) {
        return RFID_ERROR;
    }

    // Schimbam cheia daca e card nou sau scriem Admin
    if (isNewCard || tagTypeToWrite == 2) {
        uint8_t sectorTrailer[16] = {
            customKey[0], customKey[1], customKey[2],
            customKey[3], customKey[4], customKey[5],
            0xFF, 0x07, 0x80, 0x69,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };
        nfc.mifareclassic_WriteDataBlock(7, sectorTrailer);
    }

    if (revoking) return RFID_OK_REVOKED;
    if (tagTypeToWrite == 2) return RFID_OK_ADMIN;
    if (isNewCard || currentType == 0) return RFID_OK_NEW;
    return RFID_OK_OVERWRITE;
}
