#include "lora.h"

// ============================================================
// Obiect Serial LoRa
// ============================================================
HardwareSerial LoRaSerial(1);

// ============================================================
// Variabile de stare — exportate prin lora.h
// ============================================================
char currentSyncID[4] = "---";
bool isNetworkSynced = false;
bool isMasterNode = false;

bool    loraSyncJustReceived = false;
uint8_t loraSyncFromUnit     = 1; // ← 1 in loc de 0, evitam index -1

uint32_t syncEpochSeconds = 0;
uint32_t lastEpochTick = 0;
uint32_t syncReceivedTime = 0;
bool hasTransmittedThisMinute = false;
bool finalHeartbeatSent = false;

int32_t  loraRxScores[4] = {0};
uint16_t loraRxKills[4]  = {0};
uint8_t globalUnitMode[MAX_UNITS] = {0};
Team globalUnitStatus[MAX_UNITS];
uint32_t lastSeenTime[MAX_UNITS] = {0};
uint32_t globalEventTime[MAX_UNITS] = {0};
uint8_t globalBattery[MAX_UNITS] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};

uint8_t pendingEventType = EVT_NONE;
uint8_t pendingEventTeam = 0;

// ============================================================
// Masina de stari pentru transmisie (non-blocking)
// ============================================================
enum TxState : uint8_t { TX_IDLE, TX_WAIT_AIR_FREE, TX_SENDING, TX_WAIT_DONE, TX_POST_ACTION };

static TxState txState = TX_IDLE;
static uint32_t txStateStart = 0;
static uint32_t txTimeout = 3000;

static byte txBuf[62] = {0};
static uint8_t txLen = 0;
static uint8_t txPktType = 0;

// Jitter
static bool jitterPending = false;
static uint32_t jitterStart = 0;
static uint32_t jitterDelay = 0;
static uint8_t jitterPktType = 0;

// EpochSync timer
static uint32_t epochSyncTimer = 0;

// Retransmisie
static uint8_t retxCount = 0;
static uint8_t retxMax = 0;
static uint32_t retxDelay = 0;
static uint32_t retxStart = 0;

// Date salvate pentru transmisii programate
static uint8_t s_gsTimeLimit, s_gsBonus, s_gsWinCond;
static uint8_t s_bsTimerIdx, s_bsCooldownIdx, s_bsExpPtsIdx, s_bsDefPtsIdx;
static uint8_t s_rsTimeIdx, s_rsPenaltyIdx, s_rsLimitIdx[4];
static bool s_isRunning, s_isTimeOut;
static uint32_t s_gameTimeLeft;
static int32_t s_scores[4];
static uint16_t s_kills[4];
static uint8_t s_urgentEvent, s_urgentTeam;
static uint8_t s_conquestWinner = 0;
static int32_t s_penalties[4] = {0};
static uint32_t s_lastTimerTick = 0;

bool    loraSettingsReceived = false;
uint8_t rx_gsTimeLimit = 0, rx_gsBonus = 0, rx_gsWinCond = 0;
uint8_t rx_bsTimerIdx = 0, rx_bsCooldownIdx = 0;
uint8_t rx_bsExpPtsIdx = 0, rx_bsDefPtsIdx = 0;
uint8_t rx_rsTimeIdx = 0, rx_rsPenaltyIdx = 0;
uint8_t rx_rsLimitIdx[4] = {0};

bool loraStartJustSent = false;
bool loraPauseJustSent = false;
bool loraResumeJustSent = false;
int32_t loraRxPenalties[4] = {0};
bool loraSyncTimerReset = false;
uint32_t loraStartGameTimeLeft = 0;
uint32_t loraRxTimerTick = 0;
uint32_t loraMasterTimerTick = 0;

// ============================================================
// Helper — CRC
// ============================================================
static byte calcCRC(byte* buf, uint8_t len, bool useSyncID) {
    byte crc = 0;
    for (uint8_t i = 0; i < len; i++) crc ^= buf[i];
    if (useSyncID) crc ^= currentSyncID[0] ^ currentSyncID[1] ^ currentSyncID[2];
    return crc;
}

// ============================================================
// Helper — fereastra sigura pentru alerte
// ============================================================
static bool inSafeWindow() {
    if (!isNetworkSynced) return true;
    if (millis() < lastEpochTick) return false;  // In grace period

    uint32_t now = millis();
    uint32_t msInCycle = (syncEpochSeconds * 1000) + (now - lastEpochTick);
    uint32_t second = (msInCycle / 1000) % 60;
    uint32_t msInSec = msInCycle % 1000;

    for (uint8_t u = 0; u < MAX_UNITS; u++) {
        uint32_t ws = u * 5;
        // Ocupat: suntem in secunda de heartbeat
        if (second == ws % 60) return false;
        // Ocupat: suntem in ultimele 500ms inainte (pachetul nostru ar ajunge in fereastra)
        if (second == (ws == 0 ? 59 : ws - 1) % 60 && msInSec >= 500) return false;
    }
    return true;
}

// ============================================================
// Helper — jitter inteligent
// ============================================================
static uint32_t smartJitter(uint32_t pktDurationMs = 500) {
    uint32_t now = millis();  // ← PRIMA linie
    if (!isNetworkSynced) return random(5, 50);
    if (now < lastEpochTick) return random(1000, 2000);
    uint32_t msInCycle = (syncEpochSeconds * 1000) + (now - lastEpochTick);
    uint32_t second = (msInCycle / 1000) % 60;
    uint32_t msInSec = msInCycle % 1000;

    for (uint8_t offset = 0; offset < 60; offset++) {
        uint32_t testSec = (second + offset) % 60;
        bool occupied = false;

        // Verificam ca toata durata pachetului incape fara sa atinga o fereastra
        uint32_t pktEndMs = (testSec * 1000) + (offset == 0 ? msInSec : 0) + pktDurationMs;
        uint32_t pktEndSec = (pktEndMs / 1000) % 60;

        for (uint8_t u = 0; u < MAX_UNITS; u++) {
            uint32_t ws = u * 5;

            // Fereastra de start e ocupata?
            if (testSec == ws % 60) {
                occupied = true;
                break;
            }
            // Ultimele 500ms inainte de fereastra?
            if (testSec == (ws == 0 ? 59 : ws - 1) % 60 && offset == 0 && msInSec >= 500) {
                occupied = true;
                break;
            }
            // Pachetul se termina in fereastra altcuiva?
            if (pktEndSec == ws % 60) {
                occupied = true;
                break;
            }
        }

        if (!occupied) {
            uint32_t msToWait = (offset * 1000);
            if (msToWait >= msInSec)
                msToWait -= msInSec;
            else
                msToWait = 0;
            return msToWait + random(10, 100);
        }
    }

    return random(500, 1000);
}

// ============================================================
// Constructori pachete
// ============================================================
static void buildHeartbeat(
    int32_t liveScore[4], uint16_t teamKills[4],
    int32_t appliedPenalties[4],
    uint8_t selectedMode, Team sectorOwner,
    bool isBombArmed, Team respawnTeam,
    uint8_t batteryPercent) {

    memset(txBuf, 0, 32);
    txBuf[0] = NETWORK_ID[0]; txBuf[1] = NETWORK_ID[1]; txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_HEARTBEAT;
    txBuf[4] = UNIT_ID;

    // Scoruri int32_t x4
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 5 + i*4;
        txBuf[b]   = (liveScore[i] >> 24) & 0xFF;
        txBuf[b+1] = (liveScore[i] >> 16) & 0xFF;
        txBuf[b+2] = (liveScore[i] >> 8)  & 0xFF;
        txBuf[b+3] =  liveScore[i]        & 0xFF;
    }

    // Killuri uint16_t x4
    for (uint8_t i = 0; i < 4; i++) {
        txBuf[21 + i*2]     = (teamKills[i] >> 8) & 0xFF;
        txBuf[21 + i*2 + 1] =  teamKills[i]       & 0xFF;
    }

    // Penalizari int32_t x4
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 29 + i*4;
        txBuf[b]   = (appliedPenalties[i] >> 24) & 0xFF;
        txBuf[b+1] = (appliedPenalties[i] >> 16) & 0xFF;
        txBuf[b+2] = (appliedPenalties[i] >> 8)  & 0xFF;
        txBuf[b+3] =  appliedPenalties[i]        & 0xFF;
    }

    // Mode si status
    uint8_t mode = 0;
    if      (selectedMode == 0) mode = 1;
    else if (selectedMode == 1) mode = 2;
    else if (selectedMode == 2) mode = 3;

    uint8_t status = 0;
    if      (mode == 1) status = (uint8_t)sectorOwner;
    else if (mode == 2) status = isBombArmed ? 9 : 0;
    else if (mode == 3) status = (uint8_t)respawnTeam;

    uint8_t batLvl = 0;
    if      (batteryPercent >= 80) batLvl = 4;
    else if (batteryPercent >= 60) batLvl = 3;
    else if (batteryPercent >= 40) batLvl = 2;
    else if (batteryPercent >= 20) batLvl = 1;

    txBuf[45] = ((mode   & 0x0F) << 4) | (status & 0x0F);
    txBuf[46] = ((batLvl & 0x0F) << 4) | (pendingEventType & 0x0F);
    txBuf[47] = calcCRC(txBuf, 47, true);
    txLen     = 48;
    txPktType = PKT_HEARTBEAT;
    }

static void printTimingSuffix() {
    if (!isNetworkSynced || millis() < lastEpochTick) {
        Serial.println();
        return;
    }

    uint32_t now       = millis();
    uint32_t msInCycle = (syncEpochSeconds * 1000) + (now - lastEpochTick);
    uint32_t cycleS    = (msInCycle / 1000) % 60;
    uint32_t cycleMs   = msInCycle % 1000;

    uint32_t elapsed   = now - syncReceivedTime;
    uint32_t elM       = elapsed / 60000;
    uint32_t elS       = (elapsed % 60000) / 1000;
    uint32_t elMs      = elapsed % 1000;

    Serial.print(" / Epoch: ");
    Serial.print(cycleS); Serial.print("s ");
    Serial.print(cycleMs); Serial.print("ms / Sync: ");
    if (elM > 0) { Serial.print(elM); Serial.print("min "); }
    Serial.print(elS); Serial.print("sec ");
    Serial.print(elMs); Serial.println("ms");
}

static void buildSync() {
    memset(txBuf, 0, 52);
    txBuf[0] = NETWORK_ID[0];
    txBuf[1] = NETWORK_ID[1];
    txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_SYNC;
    txBuf[4] = UNIT_ID;
    txBuf[5] = currentSyncID[0];
    txBuf[6] = currentSyncID[1];
    txBuf[7] = currentSyncID[2];

    txBuf[8] = ((s_gsTimeLimit & 0x0F) << 4) | (s_gsBonus & 0x0F);
    txBuf[9] = ((s_bsTimerIdx & 0x0F) << 4) | (s_bsCooldownIdx & 0x0F);
    txBuf[10] = ((s_bsExpPtsIdx & 0x0F) << 4) | (s_bsDefPtsIdx & 0x0F);
    txBuf[11] = ((s_rsTimeIdx & 0x0F) << 4) | (s_rsPenaltyIdx & 0x0F);
    txBuf[12] = ((s_rsLimitIdx[0] & 0x0F) << 4) | (s_rsLimitIdx[1] & 0x0F);
    txBuf[13] = ((s_rsLimitIdx[2] & 0x0F) << 4) | (s_rsLimitIdx[3] & 0x0F);

    uint8_t flags = 0;
    if (s_isRunning) flags |= 0x80;
    if (s_isTimeOut) flags |= 0x40;
    flags |= (s_gsWinCond & 0x03) << 4;
    txBuf[14] = flags;

     txBuf[15] = (s_gameTimeLeft >> 24) & 0xFF;
    txBuf[16] = (s_gameTimeLeft >> 16) & 0xFF;
    txBuf[17] = (s_gameTimeLeft >> 8)  & 0xFF;
    txBuf[18] =  s_gameTimeLeft        & 0xFF;
    uint16_t msUntilNext = (uint16_t)(1000 - (millis() - s_lastTimerTick));  // ← millis() acum, la momentul transmisiei!
    txBuf[19] = (msUntilNext >> 8) & 0xFF;
    txBuf[20] =  msUntilNext       & 0xFF;

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 21 + i * 4;
        txBuf[b] = (s_scores[i] >> 24) & 0xFF;
        txBuf[b + 1] = (s_scores[i] >> 16) & 0xFF;
        txBuf[b + 2] = (s_scores[i] >> 8) & 0xFF;
        txBuf[b + 3] = s_scores[i] & 0xFF;
    }

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 37 + i * 2;
        txBuf[b] = (s_kills[i] >> 8) & 0xFF;
        txBuf[b + 1] = s_kills[i] & 0xFF;
    }

    // Penalizari aplicate int32_t x4
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t b = 45 + i*4;
        txBuf[b]   = (s_penalties[i] >> 24) & 0xFF;
        txBuf[b+1] = (s_penalties[i] >> 16) & 0xFF;
        txBuf[b+2] = (s_penalties[i] >> 8)  & 0xFF;
        txBuf[b+3] =  s_penalties[i]        & 0xFF;
    }
    txBuf[61] = calcCRC(txBuf, 61, true);
    txLen     = 62;
    txPktType = PKT_SYNC;
}

static void buildUrgent(uint8_t eventType, uint8_t teamId) {
    memset(txBuf, 0, 7);
    txBuf[0] = NETWORK_ID[0];
    txBuf[1] = NETWORK_ID[1];
    txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_URGENT;
    txBuf[4] = UNIT_ID;
    txBuf[5] = ((eventType & 0x0F) << 4) | (teamId & 0x0F);
    txBuf[6] = calcCRC(txBuf, 6, true);
    txLen = 7;
    txPktType = PKT_URGENT;
}

static void buildEpochSync(uint32_t timeLeft) {
    memset(txBuf, 0, 10);
    txBuf[0] = NETWORK_ID[0]; txBuf[1] = NETWORK_ID[1]; txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_EPOCH_SYNC;
    txBuf[4] = UNIT_ID;
    txBuf[5] = (timeLeft >> 24) & 0xFF;
    txBuf[6] = (timeLeft >> 16) & 0xFF;
    txBuf[7] = (timeLeft >> 8)  & 0xFF;
    txBuf[8] =  timeLeft        & 0xFF;
    txBuf[9] = calcCRC(txBuf, 9, true);
    txLen     = 10;
    txPktType = PKT_EPOCH_SYNC;
}

static void buildStart(uint32_t timeLeft) {
    memset(txBuf, 0, 10);
    txBuf[0] = NETWORK_ID[0];
    txBuf[1] = NETWORK_ID[1];
    txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_START;
    txBuf[4] = UNIT_ID;
    txBuf[5] = (timeLeft >> 24) & 0xFF;
    txBuf[6] = (timeLeft >> 16) & 0xFF;
    txBuf[7] = (timeLeft >> 8) & 0xFF;
    txBuf[8] = timeLeft & 0xFF;
    txBuf[9] = calcCRC(txBuf, 9, true);
    txLen = 10;
    txPktType = PKT_START;
}

static void buildConquest(uint8_t winnerTeam) {
    memset(txBuf, 0, 7);
    txBuf[0] = NETWORK_ID[0];
    txBuf[1] = NETWORK_ID[1];
    txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_CONQUEST;
    txBuf[4] = UNIT_ID;
    txBuf[5] = winnerTeam;
    txBuf[6] = calcCRC(txBuf, 6, true);
    txLen = 7;
    txPktType = PKT_CONQUEST;
}

static void buildRestart() {
    memset(txBuf, 0, 6);
    txBuf[0] = NETWORK_ID[0];
    txBuf[1] = NETWORK_ID[1];
    txBuf[2] = NETWORK_ID[2];
    txBuf[3] = PKT_RESTART;
    txBuf[4] = UNIT_ID;
    txBuf[5] = calcCRC(txBuf, 5, false);  // FARA SyncID
    txLen = 6;
    txPktType = PKT_RESTART;
}

// ============================================================
// Porneste transmisia
// ============================================================
static void startTransmit() {
    if (txState != TX_IDLE) return;
    txState = TX_WAIT_AIR_FREE;
    txStateStart = millis();
}

// ============================================================
// Actiuni Trailing Edge — dupa ce transmisia s-a terminat
// ============================================================
static void onTransmitDone(bool& isTimeOut, bool& isGameTimerRunning, uint32_t& gameTimeLeftSeconds) {
    uint32_t now = millis();
    lastSeenTime[UNIT_ID - 1] = now;

    if (txPktType == PKT_HEARTBEAT) {
        Serial.print("[LORA] Heartbeat transmis. Unit "); Serial.print(UNIT_ID);
        printTimingSuffix();
        if (pendingEventType != EVT_NONE) {
            Serial.print("[LORA] Piggyback livrat: ");
            Serial.println(pendingEventType);
            pendingEventType = EVT_NONE;
            pendingEventTeam = 0;
        }

    } else if (txPktType == PKT_SYNC) {
        Serial.print("[LORA] SYNC trimis. SyncID: "); Serial.println(currentSyncID);
        syncReceivedTime         = now;
        isNetworkSynced          = true;
        isMasterNode             = true;
        hasTransmittedThisMinute = false;
        finalHeartbeatSent       = false;
        syncEpochSeconds         = 59;
        lastEpochTick            = now + 4000;
        if (epochSyncTimer == 0) epochSyncTimer = now;
        loraMasterTimerTick = millis() - (1000 - (millis() - s_lastTimerTick)) + 400;
        loraSyncTimerReset = true;
    } else if (txPktType == PKT_START) {
        Serial.println("[LORA] START trimis.");
        loraStartGameTimeLeft = s_gameTimeLeft;
        loraStartJustSent     = true;
    } else if (txPktType == PKT_URGENT) {
        Serial.print("[LORA] Urgent trimis: ");
        Serial.println(s_urgentEvent);
        if (s_urgentEvent == EVT_GAME_PAUSED) loraPauseJustSent = true;
        else if (s_urgentEvent == EVT_GAME_RESUMED) loraResumeJustSent = true;
    } else if (txPktType == PKT_EPOCH_SYNC) {
        Serial.println("[LORA] EpochSync trimis.");
        uint8_t mySecond = (UNIT_ID - 1) * 5;
        syncEpochSeconds = mySecond;
        lastEpochTick = now;
        epochSyncTimer = now;
        hasTransmittedThisMinute = true;
        loraSyncTimerReset = true;
    } else if (txPktType == PKT_CONQUEST || txPktType == PKT_RESTART) {
        if (retxCount < retxMax) {
            retxCount++;
            retxStart = now;
            // Retransmisia va fi initiata dupa retxDelay
        }
    }
}

// ============================================================
// Procesare pachete receptionate
// ============================================================
static void processPacket(byte* buf, uint8_t len, int32_t liveScore[4], uint16_t teamKills[4], bool& isTimeOut, bool& isGameTimerRunning, uint32_t& gameTimeLeftSeconds) {
    if (buf[0] != NETWORK_ID[0] || buf[1] != NETWORK_ID[1] || buf[2] != NETWORK_ID[2]) return;

    uint8_t pktType = buf[3];
    uint8_t sender = buf[4];

    if (sender == UNIT_ID) return;
    if (sender < 1 || sender > MAX_UNITS) return;

    // Verificare CRC
    byte calcCrc = 0;
    for (uint8_t i = 0; i < len - 1; i++) calcCrc ^= buf[i];

    if (pktType == PKT_SYNC) {
        calcCrc ^= buf[5] ^ buf[6] ^ buf[7];  // Noul SyncID din pachet
    } else if (pktType != PKT_RESTART) {
        calcCrc ^= currentSyncID[0] ^ currentSyncID[1] ^ currentSyncID[2];
    }

    if (calcCrc != buf[len - 1]) {
        Serial.println("[LORA] CRC invalid. Ignorat.");
        return;
    }

    Serial.print("[LORA] Pachet 0x0"); Serial.print(pktType, HEX);
    Serial.print(" Unit "); Serial.print(sender);
    printTimingSuffix();

    uint32_t now = millis();
    lastSeenTime[sender - 1] = now;

    if (pktType == PKT_SYNC) {
        currentSyncID[0] = buf[5];
        currentSyncID[1] = buf[6];
        currentSyncID[2] = buf[7];
        currentSyncID[3] = '\0';

        // Setari joc
        rx_gsTimeLimit   = (buf[8]  >> 4) & 0x0F;
        rx_gsBonus       =  buf[8]        & 0x0F;
        rx_bsTimerIdx    = (buf[9]  >> 4) & 0x0F;
        rx_bsCooldownIdx =  buf[9]        & 0x0F;
        rx_bsExpPtsIdx   = (buf[10] >> 4) & 0x0F;
        rx_bsDefPtsIdx   =  buf[10]       & 0x0F;
        rx_rsTimeIdx     = (buf[11] >> 4) & 0x0F;
        rx_rsPenaltyIdx  =  buf[11]       & 0x0F;
        rx_rsLimitIdx[0] = (buf[12] >> 4) & 0x0F;
        rx_rsLimitIdx[1] =  buf[12]       & 0x0F;
        rx_rsLimitIdx[2] = (buf[13] >> 4) & 0x0F;
        rx_rsLimitIdx[3] =  buf[13]       & 0x0F;
        rx_gsWinCond     = (buf[14] >> 4) & 0x03;
        loraSettingsReceived = true;

        // Flags
        bool isRunning  = (buf[14] & 0x80) != 0;
        bool wasTimeOut = (buf[14] & 0x40) != 0;

        // Timp ramas
        uint32_t timeLeft = ((uint32_t)buf[15] << 24) |
        ((uint32_t)buf[16] << 16) |
        ((uint32_t)buf[17] << 8)  |
        buf[18];

        for (uint8_t i = 0; i < 4; i++) {
            uint8_t b = 21 + i * 4;
            int32_t rx = ((int32_t)(int8_t)buf[b]   << 24) |
            ((int32_t)buf[b+1] << 16) |
            ((int32_t)buf[b+2] << 8)  |
            buf[b+3];
            if (rx > loraRxScores[i]) loraRxScores[i] = rx;
        }
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t b = 37 + i * 2;
            uint16_t rx = ((uint16_t)buf[b] << 8) | buf[b+1];
            if (rx > loraRxKills[i]) loraRxKills[i] = rx;
        }

        for (uint8_t i = 0; i < 4; i++) {
            uint8_t b = 45 + i*4;
            int32_t rx = ((int32_t)(int8_t)buf[b]   << 24) |
            ((int32_t)buf[b+1] << 16) |
            ((int32_t)buf[b+2] << 8)  |
            buf[b+3];
            if (rx > loraRxPenalties[i]) loraRxPenalties[i] = rx;
        }

        uint16_t msUntilNext = ((uint16_t)buf[19] << 8) | buf[20];
        if (msUntilNext > 1000) msUntilNext = 1000;
        // Compensam latenta transmisiei (~400ms la SF9 pentru 62 bytes)
        uint32_t latency = 400;
        uint32_t elapsed = (1000 - msUntilNext) + latency;
        if (elapsed >= 1000) {
            // A trecut o secunda completa in timpul transmisiei
            if (gameTimeLeftSeconds > 0) gameTimeLeftSeconds--;
            elapsed -= 1000;
        }
        loraRxTimerTick = millis() - elapsed;

        if (isRunning) {
            isGameTimerRunning  = true;
            gameTimeLeftSeconds = timeLeft;
            loraSyncTimerReset  = false;
        } else if (wasTimeOut) {
            isTimeOut           = true;
            isGameTimerRunning  = false;
            gameTimeLeftSeconds = 0;
        } else {
            // Joc configurat dar nepornit — aplicam timpul setat
            isGameTimerRunning  = false;
            gameTimeLeftSeconds = timeLeft;
        }

        // Trailing Edge sync
        syncReceivedTime         = now;
        isNetworkSynced          = true;
        isMasterNode             = false;
        hasTransmittedThisMinute = false;
        finalHeartbeatSent       = false;
        syncEpochSeconds         = 59;
        lastEpochTick            = now + 4000;

        loraSyncJustReceived = true;
        loraSyncFromUnit     = sender;
        loraSyncTimerReset = true;

        Serial.print("[LORA] SYNC primit. SyncID: ");
        Serial.println(currentSyncID);
    } else if (pktType == PKT_START) {
        if (!isGameTimerRunning) {
            uint32_t timeLeft = ((uint32_t)buf[5] << 24) | ((uint32_t)buf[6] << 16) |
            ((uint32_t)buf[7] << 8) | buf[8];
            loraStartGameTimeLeft = timeLeft;  // ← adauga asta
            loraStartJustSent     = true;
            tone(PIN_BUZZER, 1500, 500);
            Serial.println("[LORA] START primit!");
        }
    }else if (pktType == PKT_HEARTBEAT) {
        // Scoruri int32_t
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t b = 5 + i*4;
            int32_t rx = ((int32_t)(int8_t)buf[b]   << 24) |
            ((int32_t)buf[b+1] << 16) |
            ((int32_t)buf[b+2] << 8)  |
            buf[b+3];
            if (rx > loraRxScores[i]) loraRxScores[i] = rx;
        }

        // Killuri uint16_t
        for (uint8_t i = 0; i < 4; i++) {
            uint16_t rx = ((uint16_t)buf[21 + i*2] << 8) | buf[22 + i*2];
            if (rx > loraRxKills[i]) loraRxKills[i] = rx;
        }

        // Penalizari int32_t
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t b = 29 + i*4;
            int32_t rx = ((int32_t)(int8_t)buf[b]   << 24) |
            ((int32_t)buf[b+1] << 16) |
            ((int32_t)buf[b+2] << 8)  |
            buf[b+3];
            if (rx > loraRxPenalties[i]) loraRxPenalties[i] = rx;
        }

        uint8_t mode   = (buf[45] >> 4) & 0x0F;
        uint8_t status =  buf[45]       & 0x0F;
        globalUnitMode[sender-1] = mode;
        if      (mode == 1) globalUnitStatus[sender-1] = (Team)status;
        else if (mode == 2) globalUnitStatus[sender-1] = (status == 9) ? TEAM_PLANTED : TEAM_NEUTRAL;
        else if (mode == 3) globalUnitStatus[sender-1] = (Team)status;
        else                globalUnitStatus[sender-1] = TEAM_NEUTRAL;

        globalBattery[sender-1] = (buf[46] >> 4) & 0x0F;
        uint8_t piggy           =  buf[46]       & 0x0F;
        if (piggy != EVT_NONE) {
            if (piggy == EVT_SECTOR_CAPTURED)
                globalEventTime[sender-1] = now;
            else if (piggy == EVT_SECTOR_NEUTRAL)
                globalEventTime[sender-1] = 0;
            else if (piggy == EVT_BOMB_ARMED) {
                if (globalUnitStatus[sender-1] != TEAM_PLANTED)
                    globalEventTime[sender-1] = now;
            }
            else if (piggy == EVT_BOMB_DEFUSED || piggy == EVT_BOMB_EXPLODED) {
                if (globalUnitStatus[sender-1] == TEAM_PLANTED)
                    globalEventTime[sender-1] = now;
            } else if (piggy == EVT_GAME_PAUSED) {
                loraPauseJustSent = true;
            } else if (piggy == EVT_GAME_RESUMED) {
                loraResumeJustSent = true;
            }
        }
    } else if (pktType == PKT_URGENT) {
        uint8_t eType = (buf[5] >> 4) & 0x0F;
        uint8_t teamId = buf[5] & 0x0F;

        Serial.print("[LORA] Urgent ");
        Serial.print(eType);
        Serial.print(" team ");
        Serial.print(teamId);
        Serial.print(" Unit ");
        Serial.println(sender);

        if (eType == EVT_SECTOR_CAPTURED) {
            globalUnitStatus[sender - 1] = (Team)teamId;
            globalEventTime[sender - 1] = now;
            globalUnitMode[sender - 1] = 1;
        } else if (eType == EVT_SECTOR_NEUTRAL) {
            globalUnitStatus[sender - 1] = TEAM_NEUTRAL;
            globalEventTime[sender - 1] = 0;
        } else if (eType == EVT_BOMB_ARMED) {
            if (globalUnitStatus[sender-1] != TEAM_PLANTED)
                globalEventTime[sender-1] = now;
            globalUnitStatus[sender-1] = TEAM_PLANTED;
            globalUnitMode[sender-1] = 2;
        } else if (eType == EVT_BOMB_DEFUSED || eType == EVT_BOMB_EXPLODED) {
            if (globalUnitStatus[sender-1] == TEAM_PLANTED)
                globalEventTime[sender-1] = now;
            globalUnitStatus[sender-1] = TEAM_NEUTRAL;
        } else if (eType == EVT_MODE_SECTOR) {
            globalUnitMode[sender - 1] = 1;
            globalUnitStatus[sender - 1] = TEAM_NEUTRAL;
        } else if (eType == EVT_MODE_BOMB) {
            globalUnitMode[sender - 1] = 2;
            globalUnitStatus[sender - 1] = TEAM_NEUTRAL;
        } else if (eType == EVT_MODE_RESPAWN) {
            globalUnitMode[sender - 1] = 3;
            globalUnitStatus[sender - 1] = (Team)teamId;
        } else if (eType == EVT_MODE_UNSET) {
            globalUnitMode[sender - 1] = 0;
            globalUnitStatus[sender - 1] = TEAM_NEUTRAL;
            globalEventTime[sender - 1] = 0;
        } else if (eType == EVT_GAME_PAUSED) {
            loraPauseJustSent = true;
            Serial.println("[LORA] PAUSE primit!");
        } else if (eType == EVT_GAME_RESUMED) {
            loraResumeJustSent = true;
            Serial.println("[LORA] RESUME primit!");
        }

    } else if (pktType == PKT_EPOCH_SYNC) {
        uint32_t masterSecond    = (sender - 1) * 5;
        syncEpochSeconds         = masterSecond;
        lastEpochTick            = now;
        hasTransmittedThisMinute = false;

        uint32_t rxTime = ((uint32_t)buf[5] << 24) |
        ((uint32_t)buf[6] << 16) |
        ((uint32_t)buf[7] << 8)  |
        buf[8];
        if (isGameTimerRunning && rxTime > 0)
            gameTimeLeftSeconds = rxTime;

        Serial.print("[LORA] EpochSync -> secunda "); Serial.println(masterSecond);
    } else if (pktType == PKT_RESTART) {
        // Semnalam prin valoare speciala — .ino face restart-ul
        lastSeenTime[sender - 1] = 0xFFFFFFFF;
        Serial.println("[LORA] RESTART primit!");

    } else if (pktType == PKT_CONQUEST) {
        if (!isTimeOut) {
            isTimeOut = true;
            isGameTimerRunning = false;
            Serial.print("[LORA] CONQUEST! Winner: ");
            Serial.println(buf[5]);
            tone(PIN_BUZZER, 1800, 600);
        }
    }
}

// ============================================================
// loraInit()
// ============================================================
void loraInit() {
    pinMode(PIN_LORA_AUX, INPUT);
    LoRaSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
    randomSeed(analogRead(36) + millis());

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 3; i++) currentSyncID[i] = charset[random(0, 62)];
    currentSyncID[3] = '\0';

    for (uint8_t i = 0; i < MAX_UNITS; i++) globalUnitStatus[i] = TEAM_NEUTRAL;

    Serial.print("[LORA] Init. SyncID izolat: ");
    Serial.println(currentSyncID);
}

// ============================================================
// loraUpdate()
// ============================================================
void loraUpdate(int32_t liveScore[4], uint16_t teamKills[4], int32_t  appliedPenalties[4], uint8_t selectedMode, Team sectorOwner, bool isBombArmed, Team respawnTeam, uint8_t batteryPercent, bool& isTimeOut, bool& isGameTimerRunning, uint32_t& gameTimeLeftSeconds) {
    uint32_t now = millis();

    // --------------------------------------------------------
    // 1. RECEPTIE — masina de stari
    // --------------------------------------------------------
    static byte rxBuf[62] = {0};
    static uint8_t  rxLen      = 0;   // cati bytes asteptam
    static uint8_t  rxCount    = 0;   // cati bytes am primit
    static uint32_t rxStart    = 0;   // cand a inceput receptia

    // Aruncam bytes invalizi pana la NetworkID[0]
    while (LoRaSerial.available() > 0 &&
        rxCount == 0 &&
        LoRaSerial.peek() != (byte)NETWORK_ID[0]) {
        LoRaSerial.read();
        }

        // Citim ce avem disponibil in buffer
        while (LoRaSerial.available() > 0 && rxCount < 62) {
            rxBuf[rxCount++] = LoRaSerial.read();

            // Dupa primul byte, initializam timerul
            if (rxCount == 1) rxStart = now;

            // Dupa 4 bytes stim tipul si lungimea asteptata
            if (rxCount == 4) {
                uint8_t pktType = rxBuf[3];
                rxLen = 7; // default urgent
                if      (pktType == PKT_SYNC)       rxLen = 62;
                else if (pktType == PKT_HEARTBEAT)  rxLen = 48;
                else if (pktType == PKT_START)      rxLen = 10;
                else if (pktType == PKT_EPOCH_SYNC) rxLen = 10;
                else if (pktType == PKT_RESTART)    rxLen = 6;
                else if (pktType == PKT_CONQUEST)   rxLen = 7;
            }

            // Am primit pachetul complet?
            if (rxCount >= 4 && rxCount == rxLen) {
                processPacket(rxBuf, rxLen,
                              liveScore, teamKills,
                              isTimeOut, isGameTimerRunning,
                              gameTimeLeftSeconds);
                rxCount = 0;
                rxLen   = 0;
                break;
            }
        }

        // Timeout receptie — daca nu soseste pachetul complet in 200ms, aruncam
        if (rxCount > 0 && now - rxStart > 500) {
            Serial.print("[LORA] RX Timeout dupa ");
            Serial.print(rxCount);
            Serial.print(" bytes. rxLen=");
            Serial.print(rxLen);
            Serial.print(" pktType=0x");
            Serial.println(rxBuf[3], HEX);
            rxCount = 0;
            rxLen   = 0;
        }

    // --------------------------------------------------------
    // 2. MASINA DE STARI TRANSMISIE
    // --------------------------------------------------------
    switch (txState) {
        case TX_IDLE:
            break;

        case TX_WAIT_AIR_FREE:
            if (digitalRead(PIN_LORA_AUX) == LOW) {
                loraPrintTiming(txPktType == PKT_SYNC ? "TX SYNC" : txPktType == PKT_HEARTBEAT ? "TX HEARTBEAT" : txPktType == PKT_URGENT ? "TX URGENT" : txPktType == PKT_START ? "TX START" : txPktType == PKT_CONQUEST ? "TX CONQUEST" : txPktType == PKT_RESTART ? "TX RESTART" : txPktType == PKT_EPOCH_SYNC ? "TX EPOCHSYNC" : "TX");
                LoRaSerial.write(txBuf, txLen);
                txState = TX_SENDING;
                txStateStart = now;
            } else if (now - txStateStart > txTimeout) {
                Serial.println("[LORA] TX Timeout: aer blocat.");
                txState = TX_IDLE;
            }
            break;

        case TX_SENDING:
            if (digitalRead(PIN_LORA_AUX) == HIGH) {
                txState = TX_WAIT_DONE;
                txStateStart = now;
            } else if (now - txStateStart > 200) {
                // Pachet mic — AUX poate sa nu urce vizibil
                txState = TX_WAIT_DONE;
                txStateStart = now;
            }
            break;

        case TX_WAIT_DONE:
            // Trailing Edge: AUX coboara = pachetul a ajuns la destinatari
            if (digitalRead(PIN_LORA_AUX) == LOW) {
                txState = TX_POST_ACTION;
            } else if (now - txStateStart > 2000) {
                Serial.println("[LORA] TX Timeout: wait done.");
                txState = TX_POST_ACTION;
            }
            break;

        case TX_POST_ACTION:
            onTransmitDone(isTimeOut, isGameTimerRunning, gameTimeLeftSeconds);
            txState = TX_IDLE;
            break;
    }

    // --------------------------------------------------------
    // 3. JITTER PENDING
    // --------------------------------------------------------
    if (jitterPending && txState == TX_IDLE && now - jitterStart >= jitterDelay) {
        if (jitterPktType == PKT_SYNC && !inSafeWindow()) {
            jitterDelay = smartJitter(1100);
            jitterStart = now;
            return;
        }

        if (jitterPktType == PKT_URGENT && !inSafeWindow()) {
            jitterDelay = smartJitter(500);
            jitterStart = now;
            return;
        }

        if (jitterPktType == PKT_START && !inSafeWindow()) {
            jitterDelay = smartJitter(500);
            jitterStart = now;
            return;
        }

        if (jitterPktType == PKT_CONQUEST && !inSafeWindow()) {
            jitterDelay = smartJitter(500);
            jitterStart = now;
            return;
        }

        if (jitterPktType == PKT_RESTART && !inSafeWindow()) {
            jitterDelay = smartJitter(500);
            jitterStart = now;
            return;
        }

        jitterPending = false;
        if (jitterPktType == PKT_SYNC)
            buildSync();
        else if (jitterPktType == PKT_URGENT)
            buildUrgent(s_urgentEvent, s_urgentTeam);
        else if (jitterPktType == PKT_START)
            buildStart(s_gameTimeLeft);
        else if (jitterPktType == PKT_CONQUEST)
            buildConquest(s_conquestWinner);
        else if (jitterPktType == PKT_RESTART)
            buildRestart();
        startTransmit();
    }

    // --------------------------------------------------------
    // 4. RETRANSMISIE
    // --------------------------------------------------------
    if (retxCount > 0 && retxCount <= retxMax && txState == TX_IDLE && now - retxStart >= retxDelay) {
        startTransmit();  // txBuf inca valid
    }

    // --------------------------------------------------------
    // 5. CEAS TDMA
    // --------------------------------------------------------
    now = millis();  // ← refresh dupa ce onTransmitDone poate modifica lastEpochTick
    if (!isNetworkSynced) return;
    if (now - syncReceivedTime < 5000) return;

    if (now - lastEpochTick >= 1000) {
        syncEpochSeconds++;
        if (syncEpochSeconds >= 60) {
            syncEpochSeconds = 0;
            hasTransmittedThisMinute = false;
        }
        lastEpochTick += 1000;

        uint8_t myStart = (UNIT_ID - 1) * 5;

        if (syncEpochSeconds == myStart && !hasTransmittedThisMinute) {
            hasTransmittedThisMinute = true;

            if (isMasterNode && epochSyncTimer > 0 &&
                now - epochSyncTimer >= 18000000 &&
                pendingEventType == EVT_NONE) {
                buildEpochSync(gameTimeLeftSeconds);
                } else {
                    buildHeartbeat(liveScore, teamKills, appliedPenalties,
                                   selectedMode, sectorOwner, isBombArmed,
                                   respawnTeam, batteryPercent);
                    if (epochSyncTimer == 0) epochSyncTimer = now;
                }

            if (txState == TX_IDLE) startTransmit();

            if (isTimeOut && !finalHeartbeatSent) {
                finalHeartbeatSent = true;
            }
        }
    }
}

// ============================================================
// API public
// ============================================================
void loraSendSync(uint8_t gsTimeLimit, uint8_t gsBonus, uint8_t gsWinCond, uint8_t bsTimerIdx, uint8_t bsCooldownIdx, uint8_t bsExpPtsIdx, uint8_t bsDefPtsIdx, uint8_t rsTimeIdx, uint8_t rsPenaltyIdx, uint8_t rsLimitIdx[4], bool isRunning, bool isOver, uint32_t gameTimeLeft, int32_t scores[4], uint16_t kills[4], int32_t penalties[4], uint32_t lastTimerTick) {

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 3; i++) currentSyncID[i] = charset[random(0, 62)];
    currentSyncID[3] = '\0';
    Serial.print("[LORA] Nou SyncID: ");
    Serial.println(currentSyncID);

    for (uint8_t i = 0; i < 4; i++) s_penalties[i] = penalties[i];

    s_gsTimeLimit = gsTimeLimit;
    s_gsBonus = gsBonus;
    s_gsWinCond = gsWinCond;
    s_bsTimerIdx = bsTimerIdx;
    s_bsCooldownIdx = bsCooldownIdx;
    s_bsExpPtsIdx = bsExpPtsIdx;
    s_bsDefPtsIdx = bsDefPtsIdx;
    s_rsTimeIdx = rsTimeIdx;
    s_rsPenaltyIdx = rsPenaltyIdx;
    for (uint8_t i = 0; i < 4; i++) s_rsLimitIdx[i] = rsLimitIdx[i];
    s_isRunning = isRunning;
    s_isTimeOut = isOver;
    s_gameTimeLeft = gameTimeLeft;
    s_lastTimerTick = lastTimerTick;
    for (uint8_t i = 0; i < 4; i++) {
        s_scores[i] = scores[i];
        s_kills[i] = kills[i];
    }

    if (!isNetworkSynced) {
        buildSync();
        startTransmit();
    } else {
        uint32_t now = millis();
        uint32_t minJitter = (now < lastEpochTick) ? (lastEpochTick - now + 500) : 0;
        jitterPktType = PKT_SYNC;
        jitterStart = now;
        jitterDelay = max(smartJitter(1100), minJitter);
        jitterPending = true;
        Serial.print("[LORA] Sync programat cu jitter: ");
        Serial.println(jitterDelay);
    }
}

void loraSendStart(uint32_t gameTimeLeftSeconds) {
    s_gameTimeLeft = gameTimeLeftSeconds;
    buildStart(gameTimeLeftSeconds);
    if (!inSafeWindow() || txState != TX_IDLE) {
        jitterPktType = PKT_START;
        jitterDelay = smartJitter(500);
        jitterStart = millis();
        jitterPending = true;
        return;
    }
    startTransmit();
}

void loraSendConquest(uint8_t winnerTeam) {
    s_conquestWinner = winnerTeam;  // ← adaugam variabila statica
    buildConquest(winnerTeam);
    retxCount = 0;
    retxMax = 1;
    retxDelay = 200;
    retxStart = 0;
    if (!inSafeWindow() || txState != TX_IDLE) {
        jitterPktType = PKT_CONQUEST;
        jitterDelay = smartJitter(500);
        jitterStart = millis();
        jitterPending = true;
        return;
    }
    startTransmit();
}

void loraSendRestart() {
    buildRestart();
    retxCount = 0;
    retxMax = 1;
    retxDelay = 200;
    retxStart = 0;
    if (!inSafeWindow() || txState != TX_IDLE) {
        jitterPktType = PKT_RESTART;
        jitterDelay = smartJitter(500);
        jitterStart = millis();
        jitterPending = true;
        return;
    }
    startTransmit();
}

bool loraSendUrgent(uint8_t eventType, uint8_t teamId) {
    pendingEventType = eventType;
    pendingEventTeam = teamId;
    s_urgentEvent = eventType;
    s_urgentTeam = teamId;

    if (!inSafeWindow() || txState != TX_IDLE) {
        jitterPktType = PKT_URGENT;
        jitterDelay = smartJitter(500);
        jitterStart = millis();
        jitterPending = true;
        return false;
    }

    buildUrgent(eventType, teamId);
    startTransmit();
    return true;
}

bool loraRestartPending() {
    for (uint8_t i = 0; i < MAX_UNITS; i++) {
        if (lastSeenTime[i] == 0xFFFFFFFF) return true;
    }
    return false;
}

void loraSendRestartBlocking() {
    buildRestart();
    loraPrintTiming("TX RESTART BLOCKING");  // ← adauga asta
    for (uint8_t r = 0; r < 2; r++) {
        uint32_t t = millis();
        while (digitalRead(PIN_LORA_AUX) == HIGH && millis() - t < 2000) {
        }
        if (digitalRead(PIN_LORA_AUX) == LOW) {
            LoRaSerial.write(txBuf, txLen);
            t = millis();
            while (digitalRead(PIN_LORA_AUX) == LOW && millis() - t < 200) {
            }
            while (digitalRead(PIN_LORA_AUX) == HIGH) {
            }
        }
    }
}
