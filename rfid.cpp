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
