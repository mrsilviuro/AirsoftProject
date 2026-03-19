#include "rfid.h"

MFRC522 mfrc522(PIN_RFID_SDA, PIN_RFID_RST);

static MFRC522::MIFARE_Key defaultKey = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
static MFRC522::MIFARE_Key customKey = {{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6}};

RfidReadData rfidReadTag() {
    RfidReadData data = {RFID_READ_NONE, 0};

    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return data;

    const byte blockAddr = RFID_BLOCK_ADDR;
    MFRC522::StatusCode status;

    // Autentificare cu cheia custom
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &customKey, &mfrc522.uid);

    if (status != MFRC522::STATUS_OK) {
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();
        data.result = RFID_READ_INVALID;
        return data;
    }

    // Citim blocul 4
    byte buf[18];
    byte size = sizeof(buf);
    status = mfrc522.MIFARE_Read(blockAddr, buf, &size);
    if (status != MFRC522::STATUS_OK) {
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();
        data.result = RFID_READ_INVALID;
        return data;
    }

    // Verificam magic byte
    if (buf[0] != RFID_MAGIC_BYTE) {
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();
        data.result = RFID_READ_INVALID;
        return data;
    }

    // Verificam checksum cu UID
    byte cs = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
    for (byte i = 0; i < mfrc522.uid.size; i++) cs ^= mfrc522.uid.uidByte[i];
    if (cs != buf[4]) {
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();
        data.result = RFID_READ_INVALID;
        return data;
    }

    uint8_t tagType = buf[1];
    uint16_t points = (buf[2] << 8) | buf[3];

    if (tagType == 2) {
        // Card Admin — nu il ardem
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();
        data.result = RFID_READ_ADMIN;
        return data;
    }

    if (tagType == 1) {
        if (points == 0) {
            // Card deja ars
            mfrc522.PCD_StopCrypto1();
            mfrc522.PICC_HaltA();
            data.result = RFID_READ_INVALID;
            return data;
        }

        mfrc522.PCD_StopCrypto1(); mfrc522.PICC_HaltA();

        data.result = RFID_READ_POINTS;
        data.points = points;
        return data;
    }

    mfrc522.PCD_StopCrypto1();
    mfrc522.PICC_HaltA();
    data.result = RFID_READ_INVALID;
    return data;
}

void rfidInit() {
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SDA);
    mfrc522.PCD_Init();
    Serial.println("[RFID] Initializat.");
}

RfidWriteResult rfidWriteTag(uint8_t tagTypeToWrite, uint16_t points) {
    const byte blockAddr = RFID_BLOCK_ADDR;

    // 1. Cautam card
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        return RFID_TIMEOUT;
    }

    MFRC522::StatusCode status;
    bool isNewCard = false;
    uint8_t currentType = 0;

    // 2. Autentificare cu cheia custom
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &customKey, &mfrc522.uid);

    if (status != MFRC522::STATUS_OK) {
        // Esuat — incercam cu cheia de fabrica (card nou)
        mfrc522.PCD_StopCrypto1();
        mfrc522.PICC_HaltA();

        byte wakeBuf[2];
        byte wakeSize = sizeof(wakeBuf);
        mfrc522.PICC_WakeupA(wakeBuf, &wakeSize);
        if (!mfrc522.PICC_ReadCardSerial()) return RFID_ERROR;

        status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &defaultKey, &mfrc522.uid);
        if (status != MFRC522::STATUS_OK) {
            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
            return RFID_ERROR;
        }
        isNewCard = true;
    }

    // 3. Citim tipul curent al cardului (daca nu e nou)
    if (!isNewCard) {
        byte buf[18];
        byte size = sizeof(buf);
        status = mfrc522.MIFARE_Read(blockAddr, buf, &size);
        if (status == MFRC522::STATUS_OK && buf[0] == RFID_MAGIC_BYTE) {
            currentType = buf[1];
        }
    }

    // 4. Verificare permisiuni
    if (tagTypeToWrite == 1 && currentType == 2) {
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return RFID_DENIED;  // Nu putem suprascrie Admin cu Points
    }

    bool revoking = (tagTypeToWrite == 2 && currentType == 2);

    // 5. Construim blocul de date
    byte dataBlock[16] = {0};

    if (!revoking) {
        dataBlock[0] = RFID_MAGIC_BYTE;
        dataBlock[1] = tagTypeToWrite;
        dataBlock[2] = (points >> 8) & 0xFF;
        dataBlock[3] = points & 0xFF;

        // Checksum: bytes 0-3 XOR cu toti bytes din UID
        byte cs = dataBlock[0] ^ dataBlock[1] ^ dataBlock[2] ^ dataBlock[3];
        for (byte i = 0; i < mfrc522.uid.size; i++) cs ^= mfrc522.uid.uidByte[i];
        dataBlock[4] = cs;
    }
    // daca revoking, dataBlock ramane plin de zerouri = stergere

    // 6. Scriem blocul de date
    status = mfrc522.MIFARE_Write(blockAddr, dataBlock, 16);
    if (status != MFRC522::STATUS_OK) {
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return RFID_ERROR;
    }

    // 7. Daca e card nou sau scriem Admin, schimbam cheia sectorului
    if (isNewCard || tagTypeToWrite == 2) {
        byte trailerBlock = 7;  // Trailer pentru sectorul 1 (block 4-7)
        byte sectorTrailer[16] = {customKey.keyByte[0], customKey.keyByte[1], customKey.keyByte[2], customKey.keyByte[3], customKey.keyByte[4], customKey.keyByte[5], 0xFF, 0x07, 0x80, 0x69, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        mfrc522.MIFARE_Write(trailerBlock, sectorTrailer, 16);
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    if (revoking) return RFID_OK_REVOKED;
    if (tagTypeToWrite == 2) return RFID_OK_ADMIN;
    if (isNewCard || currentType == 0) return RFID_OK_NEW;
    return RFID_OK_OVERWRITE;
}

bool rfidBurnTag() {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
        return false;

    MFRC522::StatusCode status;
    status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, RFID_BLOCK_ADDR, &customKey, &mfrc522.uid);

    if (status != MFRC522::STATUS_OK) {
        mfrc522.PCD_StopCrypto1(); mfrc522.PICC_HaltA();
        return false;
    }

    byte zeroBlock[16] = {0};
    status = mfrc522.MIFARE_Write(RFID_BLOCK_ADDR, zeroBlock, 16);
    mfrc522.PCD_StopCrypto1(); mfrc522.PICC_HaltA();
    return (status == MFRC522::STATUS_OK);
}
