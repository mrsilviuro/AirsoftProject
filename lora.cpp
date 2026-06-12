// ============================================================
// lora.cpp — Strat transport LoRa (DX-LR03) + ceas comun
// ============================================================
#include "lora.h"
#include "game.h"   // valorile model (gameTimeLimitSeconds, bombTimerMs, ...)

HardwareSerial LoRaSerial(1);   // UART1

// --- Ceas + stare retea ---
uint32_t localTime       = 0;
uint8_t  loraTimeAction  = 0;
uint16_t loraResumeTime  = 0;   // secunda primita in alerta de RESUME
uint16_t loraTimeSyncSec = 0;   // secunda primita in TIME_SYNC
uint8_t  loraEvtUnit     = 0;
uint8_t  loraEvtTeam     = 0;
int32_t  loraEvtPoints   = 0;
uint32_t lastLocalTick   = 0;
bool     localTimePaused = false;
bool     isSynced        = false;
bool     isTimeMaster    = false;   // true daca eu am trimis sync (autoritatea de timp)
uint8_t  syncedByUnit    = 0;

// Pachet SYNC: [NET][TYPE][UNIT][lt2][lt1][lt0][set x6][CRC] = 13 octeti
#define SYNC_PKT_LEN 14
#define RESTART_PKT_LEN 5       // [NET][TYPE][UNIT][SES][CRC]
#define MODE_PKT_LEN    7       // [NET][TYPE][UNIT][mode][team][SES][CRC]
#define TIME_PKT_LEN    5       // [NET][TYPE][UNIT][SES][CRC] (tipul = actiunea)
#define TIME_RESUME_PKT_LEN 7   // RESUME: [NET][TYPE][UNIT][sec_hi][sec_lo][SES][CRC]
#define CAPTURE_PKT_LEN 6       // [NET][TYPE][UNIT][team][SES][CRC]
#define NEUT_PKT_LEN    8       // [NET][TYPE][UNIT][team][pts_hi][pts_lo][SES][CRC]
#define RESPAWN_PKT_LEN 8       // [NET][TYPE][UNIT][team][kills_hi][kills_lo][SES][CRC]
#define BOMB_PKT_LEN    6       // [NET][TYPE][UNIT][team][SES][CRC]
#define KILLRESET_PKT_LEN 8     // [NET][TYPE][UNIT][winnerTeam][pts_hi][pts_lo][SES][CRC]
#define HEARTBEAT_PKT_LEN 5     // [NET][TYPE][UNIT][SES][CRC] (bateria e deja in UNIT)
#define TIME_SYNC_PKT_LEN 7     // [NET][TYPE][UNIT][sec_hi][sec_lo][SES][CRC]

// ============================================================
// Bit-cursor: scriem/citim cate nbits, MSB-first, pe un buffer
// ============================================================
static void putBits(uint8_t* buf, uint16_t& bitpos, uint32_t val, uint8_t nbits) {
    for (int8_t b = nbits - 1; b >= 0; b--) {
        uint16_t idx = bitpos >> 3;
        uint8_t  off = 7 - (bitpos & 7);
        if ((val >> b) & 1) buf[idx] |= (1 << off);
        else                buf[idx] &= ~(1 << off);
        bitpos++;
    }
}
static uint32_t getBits(const uint8_t* buf, uint16_t& bitpos, uint8_t nbits) {
    uint32_t val = 0;
    for (uint8_t b = 0; b < nbits; b++) {
        uint16_t idx = bitpos >> 3;
        uint8_t  off = 7 - (bitpos & 7);
        val = (val << 1) | ((buf[idx] >> off) & 1);
        bitpos++;
    }
    return val;
}

// ============================================================
// Coada TX non-blocant — alertele se trimit in background
// ============================================================
#define TX_QUEUE_SIZE 6
#define TX_MAX_LEN    16
static uint8_t  txQueue[TX_QUEUE_SIZE][TX_MAX_LEN];
static uint8_t  txQueueLen[TX_QUEUE_SIZE];
static uint8_t  txHead = 0, txTail = 0, txCount = 0;
enum TxState { TX_IDLE, TX_START, TX_SENDING, TX_WAIT_DONE };
static TxState  txState = TX_IDLE;
static uint32_t txTimer = 0;

// Heartbeat: keep-alive usor (doar nivelul bateriei, deja in unitByte) trimis la
// 20-30 min de la ultima transmisie. ORICE alta alerta reseteaza timerul.
static uint32_t nextHeartbeat = 0;   // 0 = neinitializat
#define HB_MIN_MS  (20UL * 60000UL)
#define HB_MAX_MS  (30UL * 60000UL)
static void heartbeatReschedule() {
    nextHeartbeat = millis() + HB_MIN_MS + (uint32_t)random(0, (long)(HB_MAX_MS - HB_MIN_MS) + 1);
    if (nextHeartbeat == 0) nextHeartbeat = 1;   // 0 e rezervat pentru "neinitializat"
}

static void loraQueueSend(const uint8_t* buf, uint8_t len) {
    if (len > TX_MAX_LEN || txCount >= TX_QUEUE_SIZE) return;
    memcpy(txQueue[txTail], buf, len);
    txQueueLen[txTail] = len;
    txTail = (txTail + 1) % TX_QUEUE_SIZE;
    txCount++;
    heartbeatReschedule();   // orice transmisie reseteaza timerul de heartbeat
}

// A doua copie a unei alerte se trimite intr-un SLOT determinist pe baza UNIT_ID,
// ca benzile celor 12 unitati sa fie disjuncte (copia a 2-a a doua unitati nu se mai
// poate suprapune niciodata). SLOT > airtime maxim (la SF10 ~400ms).
#define TX_DEFER_SIZE  6
#define TX_SLOT_MS     700   // latimea slotului per unitate
#define TX_SLOT_RAND   200   // variatie aleatoare in slot (SLOT - RAND > airtime)
static uint8_t  deferBuf[TX_DEFER_SIZE][TX_MAX_LEN];
static uint8_t  deferLen[TX_DEFER_SIZE];
static uint32_t deferTime[TX_DEFER_SIZE] = {0};   // 0 = slot liber; altfel = momentul de eliberare (millis)

// prima copie imediat + a doua programata cu jitter
static void loraQueueSendDup(const uint8_t* buf, uint8_t len) {
    loraQueueSend(buf, len);
    if (len > TX_MAX_LEN) return;
    for (uint8_t i = 0; i < TX_DEFER_SIZE; i++) {
        if (deferTime[i] == 0) {
            memcpy(deferBuf[i], buf, len);
            deferLen[i]  = len;
            uint32_t t = millis() + (uint32_t)UNIT_ID * TX_SLOT_MS + (uint32_t)random(0, TX_SLOT_RAND);
            if (t == 0) t = 1;   // 0 e rezervat pentru "slot liber"
            deferTime[i] = t;
            return;
        }
    }
    // niciun slot liber -> renuntam la a doua copie (prima a plecat deja)
}

// eliberam copiile intarziate ajunse la scadenta (apelat din loraTxUpdate)
static void loraDeferUpdate() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < TX_DEFER_SIZE; i++) {
        if (deferTime[i] != 0 && (int32_t)(now - deferTime[i]) >= 0) {
            if (txCount < TX_QUEUE_SIZE) {       // doar daca avem loc in coada principala
                loraQueueSend(deferBuf[i], deferLen[i]);
                deferTime[i] = 0;                // eliberam slotul
            }
            // altfel pastram slotul si reincercam la urmatorul loop
        }
    }
}

// nivelul bateriei locale impachetat cu UNIT_ID: bits[6:4]=baterie(0-4), bits[3:0]=unit
static uint8_t unitByte() {
    return ((globalBattery[UNIT_ID - 1] & 0x07) << 4) | (UNIT_ID & 0x0F);
}

void loraTxUpdate() {
    loraDeferUpdate();
    switch (txState) {
        case TX_IDLE:
            if (txCount > 0) { txState = TX_START; txTimer = millis(); }
            break;
        case TX_START:
            if (digitalRead(PIN_LORA_AUX) == LOW) {
                LoRaSerial.write(txQueue[txHead], txQueueLen[txHead]);
                txTimer = millis();
                txState = TX_SENDING;
            } else if (millis() - txTimer > 3000) {   // modulul nu se elibereaza -> renuntam
                txHead = (txHead + 1) % TX_QUEUE_SIZE; txCount--; txState = TX_IDLE;
            }
            break;
        case TX_SENDING:
            if (digitalRead(PIN_LORA_AUX) == HIGH || millis() - txTimer > 200) {
                txTimer = millis(); txState = TX_WAIT_DONE;
            }
            break;
        case TX_WAIT_DONE:
            if (digitalRead(PIN_LORA_AUX) == LOW || millis() - txTimer > 2000) {
                lastSeenTime[UNIT_ID - 1] = (isGamePaused ? pauseStartTime : millis());   // am transmis -> "ultimul semnal" local (pag.5)
                heartbeatReschedule();                  // sincronizarea e si ea o transmisie
                txHead = (txHead + 1) % TX_QUEUE_SIZE; txCount--; txState = TX_IDLE;
            }
            break;
    }
}

// Heartbeat / TIME_SYNC — apelate din loop()
bool loraHeartbeatDue() {
    if (!isSynced) return false;
    if (nextHeartbeat == 0) { heartbeatReschedule(); return false; }   // init la prima sincronizare
    return (int32_t)(millis() - nextHeartbeat) >= 0;
}

// Sigileaza pachetul: octet de sesiune la [len-2], apoi CRC (XOR) la [len-1] peste tot restul.
static void sealPacket(uint8_t* buf, uint8_t len) {
    buf[len - 2] = sessionId;
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len - 1; i++) cs ^= buf[i];
    buf[len - 1] = cs;
}

void loraSendHeartbeat(bool dup) {          // keep-alive simplu (dup=true dublat; dup=false o singura alerta)
    uint8_t hb[HEARTBEAT_PKT_LEN];
    hb[0] = (uint8_t)NETWORK_ID;
    hb[1] = PKT_HEARTBEAT;
    hb[2] = unitByte();
    sealPacket(hb, HEARTBEAT_PKT_LEN);
    if (dup) loraQueueSendDup(hb, HEARTBEAT_PKT_LEN);
    else     loraQueueSend(hb, HEARTBEAT_PKT_LEN);
}

void loraSendTimeSync(uint16_t sec) {      // corectie de timp de la maestru (SINGLE send: o pauza, valoare proaspata)
    uint8_t b[TIME_SYNC_PKT_LEN];
    b[0] = (uint8_t)NETWORK_ID;
    b[1] = PKT_TIME_SYNC;
    b[2] = unitByte();
    b[3] = (sec >> 8) & 0xFF;
    b[4] = sec & 0xFF;
    sealPacket(b, TIME_SYNC_PKT_LEN);
    loraQueueSend(b, TIME_SYNC_PKT_LEN);
}

// ============================================================
// applySettingsFromIndices() — converteste indecsii in valori
// (aceleasi tabele de optiuni ca la "CONFIRM" din admin)
// ============================================================
void applySettingsFromIndices() {
    const uint32_t tl[] = {0, 900, 1800, 3600, 7200, 10800, 14400, 18000, 21600, 25200, 28800, 32400, 36000, 39600, 43200, 86400};
    const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
    const uint32_t at[] = {5000, 10000, 15000, 20000};
    const uint32_t tv[] = {5, 10, 15, 20, 30, 45, 60, 120};
    const uint32_t pv[] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000, 2500, 3000};
    const uint32_t ts[] = {10, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1500, 1800};
    const uint16_t pp[] = {0, 5, 10, 25, 50, 75, 100};
    const uint16_t lm[] = {0, 10, 25, 50, 75, 100, 200, 300, 400, 500, 1000};

    gameTimeLimitSeconds = tl[gsTimeLimit];
    bonusIntervalMinutes = bn[gsBonus];
    currentWinCondition  = (WinCondition)gsWinCond;
    actionTimeMs         = at[gsActionIdx];
    bombTimerMs          = tv[bsTimerIdx] * 60000UL;
    cooldownMs           = tv[bsCooldownIdx] * 60000UL;
    bombPointsExplode    = pv[bsExpPtsIdx];
    bombPointsDefuse     = pv[bsDefPtsIdx];
    respawnTimeMs        = ts[rsTimeIdx] * 1000UL;
    respawnPenaltyPoints = pp[rsPenaltyIdx];
    for (uint8_t i = 0; i < 4; i++) teamMaxRespawns[i] = lm[rsLimitIdx[i]];

    // ceasul live preia limita doar daca jocul nu e in desfasurare
    if (!isGameTimerRunning && !isGamePaused && !isTimeOut)
        gameTimeLeftSeconds = gameTimeLimitSeconds;
}

// ============================================================
// loraInit()
// ============================================================
void loraInit() {
    pinMode(PIN_LORA_AUX, INPUT);
    LoRaSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
    localTime     = 0;
    lastLocalTick = millis();
    Serial.println("[LORA] Init UART.");
}

// ============================================================
// loraTick() — +1 secunda la localTime (daca nu e pe pauza)
// ============================================================
void loraTick() {
    if (localTimePaused) return;
    uint32_t now = millis();
    if (now - lastLocalTick >= 1000) {
        localTime++;
        lastLocalTick += 1000;
    }
}

// ============================================================
// buildSyncPacket()
// ============================================================
static void buildSyncPacket(uint8_t* buf) {
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_SYNC;
    buf[2] = UNIT_ID;
    buf[3] = (localTime >> 16) & 0xFF;
    buf[4] = (localTime >> 8) & 0xFF;
    buf[5] =  localTime        & 0xFF;

    for (uint8_t i = 6; i < 12; i++) buf[i] = 0;
    uint16_t bit = 0;
    putBits(&buf[6], bit, gsTimeLimit,   4);
    putBits(&buf[6], bit, gsWinCond,     2);
    putBits(&buf[6], bit, gsActionIdx,   2);
    putBits(&buf[6], bit, gsBonus,       3);
    putBits(&buf[6], bit, bsTimerIdx,    3);
    putBits(&buf[6], bit, bsCooldownIdx, 3);
    putBits(&buf[6], bit, bsExpPtsIdx,   4);
    putBits(&buf[6], bit, bsDefPtsIdx,   4);
    putBits(&buf[6], bit, rsTimeIdx,     4);
    putBits(&buf[6], bit, rsPenaltyIdx,  3);
    putBits(&buf[6], bit, rsLimitIdx[0], 4);
    putBits(&buf[6], bit, rsLimitIdx[1], 4);
    putBits(&buf[6], bit, rsLimitIdx[2], 4);
    putBits(&buf[6], bit, rsLimitIdx[3], 4);

    sealPacket(buf, SYNC_PKT_LEN);
}

// ============================================================
// loraSendSyncBlocking() — pauza ceas -> trimite -> AUX LOW -> reia
// ============================================================
void loraSendSyncBlocking() {
    // golim coada TX inainte (timp curat pentru sincronizare)
    uint32_t t0 = millis();
    while ((txCount > 0 || txState != TX_IDLE) && millis() - t0 < 3000) loraTxUpdate();

    localTimePaused = true;             // inghetam ceasul cat dureaza transmisia
    if (sessionId == 0) sessionId = (uint8_t)random(1, 256);   // sesiune noua la primul sync (ramane stabila apoi)
    uint8_t txBuf[SYNC_PKT_LEN];
    buildSyncPacket(txBuf);

    uint32_t t = millis();
    while (digitalRead(PIN_LORA_AUX) == HIGH && millis() - t < 2000) { }  // aer liber (AUX LOW)
    LoRaSerial.write(txBuf, SYNC_PKT_LEN);
    LoRaSerial.flush();
    t = millis();
    while (digitalRead(PIN_LORA_AUX) == LOW  && millis() - t < 200)  { }  // inceput transmisie (AUX HIGH)
    t = millis();
    while (digitalRead(PIN_LORA_AUX) == HIGH && millis() - t < 2000) { }  // final (trailing edge: AUX LOW)

    localTimePaused = false;            // livrat -> reluam numaratoarea
    lastLocalTick   = millis();
    isSynced        = true;
    isTimeMaster    = true;                 // eu am trimis sync -> sunt autoritatea de timp
    lastSeenTime[UNIT_ID - 1] = (isGamePaused ? pauseStartTime : millis());   // am transmis -> "ultimul semnal" local (pag.5)
    Serial.print("[LORA] SYNC trimis @localTime=");
    Serial.println(localTime);
}

// ============================================================
// loraSendRestartBlocking() — anunta reteaua, apoi unitatea da reboot
// ============================================================
void loraSendRestart() {
    if (!isSynced) return;   // OFFLINE: nu transmitem alerte
    uint8_t buf[RESTART_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_RESTART;
    buf[2] = unitByte();
    sealPacket(buf, RESTART_PKT_LEN);
    loraQueueSend(buf, RESTART_PKT_LEN);
    loraQueueSend(buf, RESTART_PKT_LEN);   // de 2x pentru siguranta
    Serial.println("[LORA] RESTART pus in coada.");
}

// ============================================================
// loraSendModeBlocking() — anunta modul ales (sau 0 = UNKNOWN la change mode)
// ============================================================
void loraSendMode(uint8_t mode, uint8_t team) {
    if (!isSynced) return;   // OFFLINE: nu transmitem alerte
    uint8_t buf[MODE_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_MODE;
    buf[2] = unitByte();
    buf[3] = mode;
    buf[4] = team;
    sealPacket(buf, MODE_PKT_LEN);
    loraQueueSendDup(buf, MODE_PKT_LEN);
    Serial.print("[LORA] MODE pus in coada: mode="); Serial.print(mode);
    Serial.print(" team="); Serial.println(team);
}

// ============================================================
// loraSendTime() — alerta de timp (start/pauza/resume/reset), NON-BLOCANT.
// Pune pachetul in coada; expeditorul isi aplica actiunea cand coada s-a
// golit (loraTxIdle() == true, adica AUX a redevenit LOW dupa transmisie).
// ============================================================
void loraSendTime(uint8_t pktType, uint16_t timeVal) {
    if (!isSynced) return;   // OFFLINE: standalone, nu transmite
    if (pktType == PKT_TIME_RESUME) {
        uint8_t buf[TIME_RESUME_PKT_LEN];
        buf[0] = (uint8_t)NETWORK_ID;
        buf[1] = pktType;
        buf[2] = unitByte();
        buf[3] = (timeVal >> 8) & 0xFF;
        buf[4] = timeVal & 0xFF;
        sealPacket(buf, TIME_RESUME_PKT_LEN);
        loraQueueSendDup(buf, TIME_RESUME_PKT_LEN);
    } else {
        uint8_t buf[TIME_PKT_LEN];
        buf[0] = (uint8_t)NETWORK_ID;
        buf[1] = pktType;
        buf[2] = unitByte();
        sealPacket(buf, TIME_PKT_LEN);
        loraQueueSendDup(buf, TIME_PKT_LEN);
    }
    Serial.print("[LORA] TIME pus in coada, type="); Serial.println(pktType);
}

bool loraTxIdle() { return txCount == 0 && txState == TX_IDLE; }

// ============================================================
// loraSendCapture() / loraSendNeutralize() — alerte sector (background)
// ============================================================
void loraSendCapture(uint8_t team) {
    if (!isSynced) return;
    uint8_t buf[CAPTURE_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_CAPTURE;
    buf[2] = unitByte();
    buf[3] = team;
    sealPacket(buf, CAPTURE_PKT_LEN);
    loraQueueSendDup(buf, CAPTURE_PKT_LEN);
    Serial.print("[LORA] CAPTURE pus in coada, team="); Serial.println(team);
}

void loraSendNeutralize(uint8_t team, int32_t points) {
    if (!isSynced) return;
    uint16_t p = (points < 0) ? 0 : (points > 65535 ? 65535 : (uint16_t)points);
    uint8_t buf[NEUT_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_NEUTRALIZE;
    buf[2] = unitByte();
    buf[3] = team;
    buf[4] = (p >> 8) & 0xFF;
    buf[5] = p & 0xFF;
    sealPacket(buf, NEUT_PKT_LEN);
    loraQueueSendDup(buf, NEUT_PKT_LEN);
    Serial.print("[LORA] NEUTRALIZE pus in coada, team="); Serial.print(team);
    Serial.print(" pts="); Serial.println(p);
}

void loraSendRespawn(uint8_t team, uint16_t totalKills) {
    if (!isSynced) return;
    uint8_t buf[RESPAWN_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_RESPAWN;
    buf[2] = unitByte();
    buf[3] = team;
    buf[4] = (totalKills >> 8) & 0xFF;
    buf[5] = totalKills & 0xFF;
    sealPacket(buf, RESPAWN_PKT_LEN);
    loraQueueSendDup(buf, RESPAWN_PKT_LEN);
    Serial.print("[LORA] RESPAWN pus in coada, team="); Serial.print(team);
    Serial.print(" totalKills="); Serial.println(totalKills);
}

void loraSendBombPlant(uint8_t team) {
    if (!isSynced) return;
    uint8_t buf[BOMB_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_BOMB_PLANT;
    buf[2] = unitByte();
    buf[3] = team;
    sealPacket(buf, BOMB_PKT_LEN);
    loraQueueSendDup(buf, BOMB_PKT_LEN);
    Serial.print("[LORA] BOMB PLANT pus in coada, team="); Serial.println(team);
}

void loraSendBombDefuse(uint8_t team) {
    if (!isSynced) return;
    uint8_t buf[BOMB_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_BOMB_DEFUSE;
    buf[2] = unitByte();
    buf[3] = team;
    sealPacket(buf, BOMB_PKT_LEN);
    loraQueueSendDup(buf, BOMB_PKT_LEN);
    Serial.print("[LORA] BOMB DEFUSE pus in coada, team="); Serial.println(team);
}

void loraSendKillReset(uint8_t winnerTeam, uint16_t pts) {
    if (!isSynced) return;
    uint8_t buf[KILLRESET_PKT_LEN];
    buf[0] = (uint8_t)NETWORK_ID;
    buf[1] = PKT_KILLRESET;
    buf[2] = unitByte();
    buf[3] = winnerTeam;                  // 0 = fara puncte, 1..4 = castigator
    buf[4] = (pts >> 8) & 0xFF;
    buf[5] = pts & 0xFF;
    sealPacket(buf, KILLRESET_PKT_LEN);
    loraQueueSendDup(buf, KILLRESET_PKT_LEN);
    Serial.print("[LORA] KILLRESET pus in coada, winner="); Serial.print(winnerTeam);
    Serial.print(" pts="); Serial.println(pts);
}

// ============================================================
// loraPoll() — citeste UART; intoarce evenimentul primit
// ============================================================
LoraEvent loraPoll() {
    static uint8_t  rxBuf[16];
    static uint8_t  rxCount = 0;
    static uint8_t  rxLen   = 0;   // lungime asteptata (0 = inca necunoscuta)
    static uint32_t rxStart = 0;
    uint32_t now = millis();

    // aruncam bytes pana la markerul NETWORK_ID
    while (LoRaSerial.available() && rxCount == 0 &&
        (uint8_t)LoRaSerial.peek() != (uint8_t)NETWORK_ID)
        LoRaSerial.read();

    while (LoRaSerial.available() && rxCount < sizeof(rxBuf) &&
        (rxLen == 0 || rxCount < rxLen)) {
        rxBuf[rxCount++] = LoRaSerial.read();
    if (rxCount == 1) rxStart = now;
    if (rxCount == 2) {            // tipul e cunoscut -> stim lungimea
        if      (rxBuf[1] == PKT_SYNC)    rxLen = SYNC_PKT_LEN;
        else if (rxBuf[1] == PKT_RESTART) rxLen = RESTART_PKT_LEN;
        else if (rxBuf[1] == PKT_MODE)    rxLen = MODE_PKT_LEN;
        else if (rxBuf[1] == PKT_TIME_RESUME) rxLen = TIME_RESUME_PKT_LEN;
        else if (rxBuf[1] >= PKT_TIME_START && rxBuf[1] <= PKT_TIME_RESET) rxLen = TIME_PKT_LEN;
        else if (rxBuf[1] == PKT_CAPTURE)    rxLen = CAPTURE_PKT_LEN;
        else if (rxBuf[1] == PKT_NEUTRALIZE) rxLen = NEUT_PKT_LEN;
        else if (rxBuf[1] == PKT_RESPAWN)    rxLen = RESPAWN_PKT_LEN;
        else if (rxBuf[1] == PKT_BOMB_PLANT || rxBuf[1] == PKT_BOMB_DEFUSE) rxLen = BOMB_PKT_LEN;
        else if (rxBuf[1] == PKT_KILLRESET) rxLen = KILLRESET_PKT_LEN;
        else if (rxBuf[1] == PKT_HEARTBEAT) rxLen = HEARTBEAT_PKT_LEN;
        else if (rxBuf[1] == PKT_TIME_SYNC) rxLen = TIME_SYNC_PKT_LEN;
        else { rxCount = 0; rxLen = 0; break; }   // tip necunoscut -> resync
    }
        }

        if (rxCount > 0 && (rxLen == 0 || rxCount < rxLen) && now - rxStart > 500) { rxCount = 0; rxLen = 0; return LORA_EVT_NONE; }
        if (rxLen == 0 || rxCount < rxLen) return LORA_EVT_NONE;

        uint8_t type = rxBuf[1];
    uint8_t len  = rxLen;
    rxCount = 0; rxLen = 0;            // pachet complet — il consumam

    if (rxBuf[0] != (uint8_t)NETWORK_ID) return LORA_EVT_NONE;
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len - 1; i++) cs ^= rxBuf[i];
    if (cs != rxBuf[len - 1]) return LORA_EVT_NONE;

    // Filtru de sesiune: ignoram pachetele cu alta sesiune (unitate straina / sync accidental).
    // Exceptie: SYNC, care tocmai poarta sesiunea ce trebuie adoptata.
    if (type != PKT_SYNC && rxBuf[len - 2] != sessionId) return LORA_EVT_NONE;

    // --- RESTART ---
    if (type == PKT_RESTART) {
        // RESTART vine doar de la o unitate SINCRONIZATA (filtrul e la emisie);
        // toate unitatile, inclusiv cele nesincronizate, dau reboot.
        Serial.print("[LORA] RESTART primit de la unit "); Serial.println(rxBuf[2] & 0x0F);
        return LORA_EVT_RESTART;
    }

    // --- MODE (select / change mode) ---
    if (type == PKT_MODE) {
        if (!isSynced) return LORA_EVT_NONE;   // OFFLINE ignora alertele de joc
        uint8_t u = rxBuf[2] & 0x0F;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        uint8_t mode = rxBuf[3], team = rxBuf[4];
        if (u >= 1 && u <= MAX_UNITS && u != UNIT_ID) {
            if (unitTable[u - 1].mode != mode) {        // doar la SCHIMBARE reala de mod -> nu sterge o cucerire activa la copia dubla
                unitTable[u - 1].mode       = mode;
                unitTable[u - 1].status     = 0;        // fara actiune inca (neutral/idle)
                unitTable[u - 1].team       = (Team)team;
                unitTable[u - 1].actionTime = 0;
            } else if (mode == 3) {
                unitTable[u - 1].team       = (Team)team;   // respawn isi poate schimba echipa fara reset
            }
            globalBattery[u - 1] = batt;
            lastSeenTime[u - 1] = (isGamePaused ? pauseStartTime : millis());
            Serial.print("[LORA] MODE de la unit ");
            Serial.print(u);
            Serial.print(" -> mode="); Serial.print(mode);
            Serial.print(" team="); Serial.println(team);
        }
        return LORA_EVT_NONE;
    }

    // --- TIME (start / pauza / resume / reset) ---
    if (type >= PKT_TIME_START && type <= PKT_TIME_RESET) {
        if (!isSynced) return LORA_EVT_NONE;   // OFFLINE ignora alertele de joc
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;   // nu reactionam la propria alerta
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) {
            globalBattery[u - 1] = batt;
            lastSeenTime[u - 1] = (isGamePaused ? pauseStartTime : millis());
        }
        loraTimeAction = type - PKT_TIME_START; // 0=start,1=pause,2=resume,3=reset
        if (type == PKT_TIME_RESUME) loraResumeTime = ((uint16_t)rxBuf[3] << 8) | rxBuf[4];   // secunda la care reluam
        Serial.print("[LORA] TIME primit, actiune="); Serial.println(loraTimeAction);
        return LORA_EVT_TIME;
    }

    // --- CAPTURE sector ---
    if (type == PKT_CAPTURE) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraEvtUnit = u;
        loraEvtTeam = rxBuf[3];
        Serial.print("[LORA] CAPTURE de la unit "); Serial.print(u); Serial.print(" team="); Serial.println(loraEvtTeam);
        return LORA_EVT_CAPTURE;
    }

    // --- NEUTRALIZE sector ---
    if (type == PKT_NEUTRALIZE) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraEvtUnit = u;
        loraEvtTeam = rxBuf[3];
        loraEvtPoints = ((int32_t)rxBuf[4] << 8) | rxBuf[5];
        Serial.print("[LORA] NEUTRALIZE de la unit "); Serial.print(u); Serial.print(" pts="); Serial.println(loraEvtPoints);
        return LORA_EVT_NEUTRALIZE;
    }

    if (type == PKT_RESPAWN) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraEvtUnit = u;
        loraEvtTeam = rxBuf[3];
        loraEvtPoints = ((int32_t)rxBuf[4] << 8) | rxBuf[5];   // total curent kill-uri
        Serial.print("[LORA] RESPAWN de la unit "); Serial.print(u); Serial.print(" totalKills="); Serial.println(loraEvtPoints);
        return LORA_EVT_RESPAWN;
    }

    if (type == PKT_BOMB_PLANT || type == PKT_BOMB_DEFUSE) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraEvtUnit = u;
        loraEvtTeam = rxBuf[3];
        Serial.print("[LORA] BOMB "); Serial.print(type == PKT_BOMB_PLANT ? "PLANT" : "DEFUSE");
        Serial.print(" de la unit "); Serial.println(u);
        return (type == PKT_BOMB_PLANT) ? LORA_EVT_BOMB_PLANT : LORA_EVT_BOMB_DEFUSE;
    }

    if (type == PKT_KILLRESET) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraEvtUnit = u;
        loraEvtTeam = rxBuf[3];                                 // 0 = fara puncte, 1..4 = castigator
        loraEvtPoints = ((int32_t)rxBuf[4] << 8) | rxBuf[5];    // puncte exacte
        Serial.print("[LORA] KILLRESET de la unit "); Serial.print(u);
        Serial.print(" winner="); Serial.print(loraEvtTeam); Serial.print(" pts="); Serial.println(loraEvtPoints);
        return LORA_EVT_KILLRESET;
    }

    if (type == PKT_HEARTBEAT) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }  // doar tinem unitatea "vie"
        return LORA_EVT_NONE;   // fara handler in .ino (silentios, ca MODE)
    }

    if (type == PKT_TIME_SYNC) {
        if (!isSynced) return LORA_EVT_NONE;
        uint8_t u = rxBuf[2] & 0x0F;
        if (u == UNIT_ID) return LORA_EVT_NONE;
        uint8_t batt = (rxBuf[2] >> 4) & 0x07;
        if (u >= 1 && u <= MAX_UNITS) { globalBattery[u-1] = batt; lastSeenTime[u-1] = millis(); }
        loraTimeSyncSec = ((uint16_t)rxBuf[3] << 8) | rxBuf[4];   // timpul ramas, de la maestru
        return LORA_EVT_TIME_SYNC;
    }

    // --- SYNC ---
    // Daca jocul nostru ruleaza, NU acceptam sync (preventiv: o unitate abia deschisa
    // ar putea trimite sync din greseala si ne-ar suprascrie starea). Acceptam doar
    // cand suntem pe pauza, cu jocul neinceput sau terminat (time out).
    if (isGameTimerRunning && !isGamePaused && !isTimeOut) return LORA_EVT_NONE;
    sessionId = rxBuf[len - 2];          // adoptam sesiunea maestrului
    uint8_t  fromUnit = rxBuf[2];
    uint32_t lt = ((uint32_t)rxBuf[3] << 16) | ((uint32_t)rxBuf[4] << 8) | rxBuf[5];

    uint16_t bit = 0;
    gsTimeLimit   = getBits(&rxBuf[6], bit, 4);
    gsWinCond     = getBits(&rxBuf[6], bit, 2);
    gsActionIdx   = getBits(&rxBuf[6], bit, 2);
    gsBonus       = getBits(&rxBuf[6], bit, 3);
    bsTimerIdx    = getBits(&rxBuf[6], bit, 3);
    bsCooldownIdx = getBits(&rxBuf[6], bit, 3);
    bsExpPtsIdx   = getBits(&rxBuf[6], bit, 4);
    bsDefPtsIdx   = getBits(&rxBuf[6], bit, 4);
    rsTimeIdx     = getBits(&rxBuf[6], bit, 4);
    rsPenaltyIdx  = getBits(&rxBuf[6], bit, 3);
    rsLimitIdx[0] = getBits(&rxBuf[6], bit, 4);
    rsLimitIdx[1] = getBits(&rxBuf[6], bit, 4);
    rsLimitIdx[2] = getBits(&rxBuf[6], bit, 4);
    rsLimitIdx[3] = getBits(&rxBuf[6], bit, 4);

    applySettingsFromIndices();
    localTime       = lt;             // sincronizam ceasul 1:1
    lastLocalTick   = now;            // numaram imediat de la valoarea primita
    localTimePaused = false;
    isSynced        = true;
    isTimeMaster    = false;                // am primit sync -> nu sunt autoritatea de timp
    syncedByUnit    = fromUnit;
    if (fromUnit >= 1 && fromUnit <= MAX_UNITS) lastSeenTime[fromUnit - 1] = now;
    Serial.print("[LORA] SYNC primit de la unit ");
    Serial.print(", localTime="); Serial.println(lt);
    return LORA_EVT_SYNC;
}
