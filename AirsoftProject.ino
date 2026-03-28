#include "buttons.h"
#include "config.h"
#include "display.h"
#include "lora.h"
#include "rfid.h"
#include <esp_wifi.h>
#include <esp_bt.h>

// ============================================================
// Starea masinii de stari
// ============================================================
GameState currentState = STATE_BOOT;
bool needsDisplayUpdate = true;

// --- Display brightness ---
uint32_t lastActivityTime = 0;
bool isDimmed = false;

// --- Meniu ---
uint8_t menuIndex = 0;
int8_t selectedMode = -1;
uint32_t loadingStartTime = 0;
uint32_t readyScreenStart = 0;

// --- Pagini ---
uint8_t currentPage = 0;

// --- Bonus Screen ---
uint16_t bonusPoints = 0;
uint32_t bonusScreenStart = 0;

// --- Sector ---
Team sectorOwner = TEAM_NEUTRAL;
uint32_t captureStartTime = 0;
uint32_t currentCapturePoints = 0;

// --- Joc ---
uint32_t gameTimeLeftSeconds = 0;
bool isGameTimerRunning = false;
uint32_t bonusIntervalMinutes = 30;
WinCondition currentWinCondition = WIN_BY_POINTS;
bool isTimeOut = false;
Team conquestWinner = TEAM_NEUTRAL;

// --- Baterie ---
uint8_t batteryPercent = 100;

// --- Bomb ---
bool isBombArmed = false;
Team bombOwner = TEAM_NEUTRAL;  // Echipa care a amorsat
uint32_t bombPlantTime = 0;
uint32_t bombTimerMs = 15 * 60000UL;  // index 2 = 15 minute
bool isCooldownActive = false;
uint32_t cooldownStartTime = 0;
uint32_t cooldownMs = 20 * 60000UL;  // index 3 = 20 minute
uint32_t bombPointsExplode = 600;
uint32_t bombPointsDefuse = 300;

// --- Respawn ---
Team respawnTeam = TEAM_NEUTRAL;
uint32_t respawnTimeMs = 300000;  // 5 minute
uint16_t respawnPenaltyPoints = 25;
uint16_t teamKills[4] = {0, 0, 0, 0};
uint16_t teamMaxRespawns[4] = {0, 0, 0, 0};  // 0 = nelimitat
uint8_t queueCount = 0;
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint32_t respawnQueue[100] = {0};

// --- Puncte ---
int32_t liveScore[4] = {0, 0, 0, 0};
uint32_t lastPointTick = 0;  // Cand s-au dat ultimele puncte

// --- Actiune curenta ---
ActionType currentAction = ACT_NONE;
uint8_t actingTeam = 255;  // index buton (0-3)
uint32_t actionStartTime = 0;

// --- Success ---
uint32_t successStartTime = 0;

// --- Releu ---
bool isRelayActive = false;
uint32_t relayTurnOffTime = 0;

// --- Context display ---
PageContext ctx;

// --- Admin ---
uint8_t adminMenuIndex = 0;
uint8_t adminScrollIndex = 0;
uint8_t adminSelectedPage = 0;
uint32_t adminSavedTime = 0;
GameState previousStateBeforeAdmin = STATE_MENU;
static uint32_t lastRfidRead = 0;
uint32_t syncingStartTime = 0;
uint32_t syncedScreenStart = 0;

// Indecsi setari
uint8_t gsIndex = 0, gsWinCond = 0, gsTimeLimit = 0, gsBonus = 2;
uint8_t bsIndex = 0, bsTimerIdx = 2, bsCooldownIdx = 3;
uint8_t bsExpPtsIdx = 6, bsDefPtsIdx = 3;
uint8_t rsIndex = 0, rsTimeIdx = 6, rsPenaltyIdx = 3;
uint8_t rsLimitIdx[4] = {0, 0, 0, 0};
uint8_t twIndex = 0, twOptionIdx = 0;

// Tag Writer state
uint8_t tagStatusMsg = 0;
uint32_t tagStatusTime = 0;
uint32_t tagWaitStart = 0;

uint32_t lastTimerTick = 0;

int32_t appliedPenalties[4] = {0, 0, 0, 0};
uint32_t loraStartPendingTime = 0;

AdminContext buildAdminContext() {
    AdminContext ac;
    ac.selectedPage = adminSelectedPage;
    ac.gsIndex = gsIndex;
    ac.gsWinCond = gsWinCond;
    ac.gsTimeLimit = gsTimeLimit;
    ac.gsBonus = gsBonus;
    ac.bsIndex = bsIndex;
    ac.bsTimerIdx = bsTimerIdx;
    ac.bsCooldownIdx = bsCooldownIdx;
    ac.bsExpPtsIdx = bsExpPtsIdx;
    ac.bsDefPtsIdx = bsDefPtsIdx;
    ac.rsIndex = rsIndex;
    ac.rsTimeIdx = rsTimeIdx;
    ac.rsPenaltyIdx = rsPenaltyIdx;
    for (uint8_t i = 0; i < 4; i++) ac.rsLimitIdx[i] = rsLimitIdx[i];
    ac.twIndex = twIndex;
    ac.twOptionIdx = twOptionIdx;
    return ac;
}

// ============================================================
// updateBattery()
// Management baterie;
// ============================================================
void updateBattery() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 20; i++) sum += analogRead(PIN_BATTERY);
    float adcAvg = sum / 20.0;
    float pinV = (adcAvg / 4095.0) * 3.56;
    float batV = pinV * ((10.0 + 3.3) / 3.3);  // Divizor 10k/3.3k

    uint8_t newPercent = 0;
    if (batV >= 12.6)
        newPercent = 100;
    else if (batV <= 9.0)
        newPercent = 0;
    else
        newPercent = (uint8_t)(((batV - 9.0) / (12.6 - 9.0)) * 100.0);

    // Smoothing — evitam salturi bruste
    if (batteryPercent == 100)
        batteryPercent = newPercent;
    else
        batteryPercent = (batteryPercent * 80 + newPercent * 20) / 100;
}

// ============================================================
// refreshLEDs()
// Aprinde LED-ul echipei proprietare + orice buton tinut fizic
// ============================================================
void refreshLEDs() {
    for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], LOW);

    // LED-ul echipei proprietare — mereu aprins
    if (selectedMode == 0 && sectorOwner != TEAM_NEUTRAL)
        digitalWrite(PIN_LEDS[sectorOwner - 1], HIGH);
    else if (selectedMode == 1 && isBombArmed)
        digitalWrite(PIN_LEDS[bombOwner - 1], HIGH);
    else if (selectedMode == 2 && respawnTeam != TEAM_NEUTRAL)
        digitalWrite(PIN_LEDS[respawnTeam - 1], HIGH);

    // LED-urile butoanelor fizic apasate — aprins pe durata actiunii
    // DAR nu stingem LED-ul proprietarului chiar daca altcineva apasa
    for (uint8_t i = 0; i < 4; i++)
        if (digitalRead(PIN_BTNS[i]) == LOW) digitalWrite(PIN_LEDS[i], HIGH);
}

// ============================================================
// handleActionProgress()
// Apelata in loop() cat timp currentState == STATE_ACTION.
// Non-blocking — nu contine niciun delay().
// ============================================================
void handleActionProgress() {
    uint32_t now = millis();
    uint32_t elapsed = now - actionStartTime;

    // Daca dezamorsam si bomba a explodat intre timp, iesim imediat
    if (currentAction == ACT_DEFUSE && !isBombArmed) {
        currentAction = ACT_NONE;
        currentState = STATE_BOOM;
        needsDisplayUpdate = true;
        return;
    }

    // 1. Butonul a fost eliberat? — ANULARE
    if (digitalRead(PIN_BTNS[actingTeam]) == HIGH) {
        currentState = STATE_PAGES;
        currentPage = 0;
        currentAction = ACT_NONE;
        needsDisplayUpdate = true;
        tone(PIN_BUZZER, 300, 300);
        refreshLEDs();
        Serial.println("[ACTION] Anulat — buton eliberat.");
        return;
    }

    // 2. Timp atins? — SUCCESS
    if (elapsed >= ACTION_TIME_MS) {
        // Aplicam efectul actiunii
        if (currentAction == ACT_CAPTURE) {
            sectorOwner = (Team)(actingTeam + 1);
            captureStartTime = now;
            currentCapturePoints = 0;
            lastPointTick = now;  // ← now, nu millis()
            Serial.print("[SECTOR] Cucerit de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
            loraSendUrgent(EVT_SECTOR_CAPTURED, (uint8_t)sectorOwner);
        } else if (currentAction == ACT_NEUTRALIZE) {
            sectorOwner = TEAM_NEUTRAL;
            captureStartTime = 0;
            currentCapturePoints = 0;
            lastPointTick = 0;  // ← doar asta
            Serial.print("[SECTOR] Neutralizat de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
            loraSendUrgent(EVT_SECTOR_NEUTRAL, 0);
        } else if (currentAction == ACT_ARM) {
            isBombArmed = true;
            bombOwner = (Team)(actingTeam + 1);
            bombPlantTime = now;
            noTone(PIN_BUZZER);
            Serial.print("[BOMB] Amorsata de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
            loraSendUrgent(EVT_BOMB_ARMED, (uint8_t)bombOwner);
        } else if (currentAction == ACT_DEFUSE) {
            isBombArmed = false;
            isCooldownActive = true;
            cooldownStartTime = now;
            liveScore[actingTeam] += bombPointsDefuse;
            noTone(PIN_BUZZER);
            Serial.print("[BOMB] Dezamorsata de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
            loraSendUrgent(EVT_BOMB_DEFUSED, (uint8_t)(actingTeam + 1));
        }

        // Releu 3 secunde (beep/sirena scurta)
        digitalWrite(PIN_RELAY, LOW);
        isRelayActive = true;
        relayTurnOffTime = now + 3000;

        // Trecem pe ecranul de succes
        currentState = STATE_SUCCESS;
        successStartTime = now;
        currentAction = ACT_NONE;
        drawSuccessScreen();
        refreshLEDs();
        tone(PIN_BUZZER, 1800, 600);
        return;
    }

    // 3. Actiunea e in desfasurare — redesenam bara de progres
    drawActionScreen(currentAction, actingTeam, elapsed, ACTION_TIME_MS);
}

// ============================================================
// buildContext()
// ============================================================
void buildContext() {
    ctx.batteryPercent = batteryPercent;
    ctx.currentPage = currentPage;
    ctx.selectedMode = selectedMode;
    ctx.isTimeOut = isTimeOut;
    ctx.conquestWinner = conquestWinner;
    ctx.winCondition = currentWinCondition;
    ctx.isGameTimerRunning = isGameTimerRunning;
    ctx.gameTimeLeftSeconds = gameTimeLeftSeconds;

    // Sector — date REALE
    ctx.sectorOwner = sectorOwner;
    ctx.captureStartTime = captureStartTime;
    ctx.currentCapturePoints = currentCapturePoints;
    ctx.bonusIntervalMinutes = bonusIntervalMinutes;

    // Bomb (placeholder)
    ctx.isBombArmed = isBombArmed;
    ctx.isCooldownActive = isCooldownActive;
    ctx.bombPlantTime = bombPlantTime;
    ctx.bombTimerMs = bombTimerMs;
    ctx.cooldownStartTime = cooldownStartTime;
    ctx.cooldownMs = cooldownMs;

    // Respawn (placeholder)
    ctx.respawnTeam = respawnTeam;
    ctx.queueCount = queueCount;
    ctx.queueHead = queueHead;
    ctx.respawnPenaltyPoints = respawnPenaltyPoints;
    ctx.respawnQueue[0] = respawnQueue[queueHead];
    memcpy(ctx.teamMaxRespawns, teamMaxRespawns, sizeof(teamMaxRespawns));
    memcpy(ctx.teamKills, teamKills, sizeof(teamKills));

    // Scoruri (placeholder)
    for (uint8_t i = 0; i < 4; i++)
        ctx.liveScore[i] = liveScore[i];
    memset(ctx.globalKills, 0, sizeof(ctx.globalKills));
    for (uint8_t i = 0; i < 4; i++)
        ctx.globalKills[0][i] = teamKills[i];
    for (uint8_t i = 0; i < 4; i++)
        ctx.appliedPenalties[i] = appliedPenalties[i];

    // Datele unitatii locale — reale
    uint8_t myMode = 0;
    if (selectedMode == 0)
        myMode = 1;
    else if (selectedMode == 1)
        myMode = 2;
    else if (selectedMode == 2)
        myMode = 3;

    ctx.globalUnitMode[UNIT_ID - 1] = myMode;
    ctx.lastSeenTime[UNIT_ID - 1] = lastSeenTime[UNIT_ID - 1]; // ← din lora.cpp

    if (myMode == 1) {
        ctx.globalUnitStatus[UNIT_ID - 1] = sectorOwner;
        ctx.globalEventTime[UNIT_ID - 1] = captureStartTime;
    } else if (myMode == 2) {
        ctx.globalUnitStatus[UNIT_ID - 1] = isBombArmed ? TEAM_PLANTED : TEAM_NEUTRAL;
        ctx.globalEventTime[UNIT_ID - 1] = isBombArmed ? bombPlantTime : isCooldownActive ? cooldownStartTime : 0;
    } else if (myMode == 3) {
        ctx.globalUnitStatus[UNIT_ID - 1] = respawnTeam;
        ctx.globalEventTime[UNIT_ID - 1] = 0;
    }

    // Restul unitatilor — golim (LoRa va popula mai tarziu)
    for (uint8_t i = 0; i < MAX_UNITS; i++) {
        if (i == UNIT_ID - 1) continue;
        ctx.globalUnitMode[i] = globalUnitMode[i];
        ctx.globalUnitStatus[i] = globalUnitStatus[i];
        ctx.lastSeenTime[i] = lastSeenTime[i];
        ctx.globalEventTime[i] = globalEventTime[i];
        ctx.globalBattery[i] = globalBattery[i];
    }

    ctx.page4ScrollIndex = 0;
    ctx.page5ScrollIndex = 0;
}

void setBrightness(uint8_t level) {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(level);
}

void resetActivity() {
    lastActivityTime = millis();
    if (isDimmed) {
        isDimmed = false;
        setBrightness(204);  // 80%
    }
}

void loraPrintTiming(const char* label) {
    uint32_t now = millis();

    uint32_t cycleS = 0;
    uint32_t cycleMs = 0;

    if (isNetworkSynced && now >= lastEpochTick) {
        uint32_t msInCycle = (syncEpochSeconds * 1000) + (now - lastEpochTick);
        cycleS = (msInCycle / 1000) % 60;
        cycleMs = msInCycle % 1000;
    } else if (isNetworkSynced) {
        // Inca in fereastra de 15s de asteptare dupa sync
        cycleS = 0;
        cycleMs = 0;
    }

    uint32_t elapsed = now - syncReceivedTime;
    uint32_t elH = elapsed / 3600000;
    uint32_t elM = (elapsed % 3600000) / 60000;
    uint32_t elS = (elapsed % 60000) / 1000;
    uint32_t elMs = elapsed % 1000;

    Serial.print("[");
    Serial.print(label);
    Serial.print("] ");
    if (isNetworkSynced && now < lastEpochTick) {
        Serial.print("Epoch: waiting 5s...");
    } else {
        Serial.print("Epoch: ");
        Serial.print(cycleS);
        Serial.print("s ");
        Serial.print(cycleMs);
        Serial.print("ms");
    }
    Serial.print(" / From Sync: ");
    if (elH > 0) {
        Serial.print(elH);
        Serial.print("h ");
    }
    if (elM > 0 || elH > 0) {
        Serial.print(elM);
        Serial.print("m ");
    }
    Serial.print(elS);
    Serial.print("s ");
    Serial.print(elMs);
    Serial.println("ms");
}

void syncAdminIndices() {
    gsIndex = bsIndex = rsIndex = twIndex = 0;

    // Game Settings — recalculam gsTimeLimit din gameTimeLeftSeconds
    const uint32_t tl[] = {0, 10, 3600, 7200, 10800, 14400, 18000,
        21600, 25200, 28800, 32400, 36000, 39600, 43200, 86400};
        for (uint8_t i = 0; i < 15; i++)
            if (tl[i] == gameTimeLeftSeconds) { gsTimeLimit = i; break; }

            const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
        for (uint8_t i = 0; i < 7; i++)
            if (bn[i] == bonusIntervalMinutes) { gsBonus = i; break; }

            gsWinCond = (uint8_t)currentWinCondition;

        // Bomb Settings
        const uint32_t tv[] = {5, 10, 15, 20, 30, 45, 60, 120};
        for (uint8_t i = 0; i < 8; i++)
            if (tv[i] * 60000UL == bombTimerMs)  { bsTimerIdx = i; break; }
            for (uint8_t i = 0; i < 8; i++)
                if (tv[i] * 60000UL == cooldownMs)   { bsCooldownIdx = i; break; }

                const uint32_t pv[] = {50, 100, 200, 300, 400, 500, 600, 700,
                    800, 900, 1000, 1500, 2000, 2500, 3000};
                    for (uint8_t i = 0; i < 15; i++)
                        if (pv[i] == bombPointsExplode) { bsExpPtsIdx = i; break; }
                        for (uint8_t i = 0; i < 15; i++)
                            if (pv[i] == bombPointsDefuse)  { bsDefPtsIdx = i; break; }

                            // Respawn Settings
                            const uint32_t ts[] = {10, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1500, 1800};
                        for (uint8_t i = 0; i < 12; i++)
                            if (ts[i] * 1000UL == respawnTimeMs) { rsTimeIdx = i; break; }

                            const uint16_t pp[] = {0, 5, 10, 25, 50, 75, 100};
                        for (uint8_t i = 0; i < 7; i++)
                            if (pp[i] == respawnPenaltyPoints) { rsPenaltyIdx = i; break; }

                            const uint16_t lm[] = {0, 10, 25, 50, 75, 100, 200, 300, 400, 500, 1000};
                        for (uint8_t t = 0; t < 4; t++)
                            for (uint8_t i = 0; i < 11; i++)
                                if (lm[i] == teamMaxRespawns[t]) { rsLimitIdx[t] = i; break; }
}

// ============================================================
// setup()
// ============================================================
void setup() {
    esp_wifi_stop();
    esp_bt_controller_disable();
    Serial.begin(115200);

    for (uint8_t i = 0; i < 4; i++) {
        pinMode(PIN_LEDS[i], OUTPUT);
        digitalWrite(PIN_LEDS[i], LOW);
    }

    pinMode(PIN_RELAY, OUTPUT_OPEN_DRAIN);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);

    buttonsInit();
    rfidInit();
    loraInit();
    displayInit();

    Serial.println("[BOOT] Setup complet.");
}

// ============================================================
// loop()
// ============================================================
void loop() {
    uint32_t now = millis();
    // LoRa update — receptie + TDMA + transmisii programate
    loraUpdate(liveScore, teamKills, appliedPenalties,
               selectedMode, sectorOwner, isBombArmed,
               respawnTeam, batteryPercent,
               isTimeOut, isGameTimerRunning, gameTimeLeftSeconds);

    for (uint8_t i = 0; i < 4; i++) {
        if (loraRxScores[i] > liveScore[i])
            liveScore[i] = loraRxScores[i];
        if (loraRxKills[i] > teamKills[i])
            teamKills[i] = loraRxKills[i];
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (loraRxPenalties[i] > appliedPenalties[i])
            appliedPenalties[i] = loraRxPenalties[i];
    }

    if (loraSyncTimerReset) {
        loraSyncTimerReset = false;
        lastTimerTick = loraMasterTimerTick;  // ← nu millis()!
    }

    if (loraRxTimerTick > 0) {
        Serial.print("[SYNC] loraRxTimerTick="); Serial.print(loraRxTimerTick);
        Serial.print(" millis()="); Serial.print(millis());
        Serial.print(" diff="); Serial.println(millis() - loraRxTimerTick);
        lastTimerTick  = loraRxTimerTick;
        loraRxTimerTick = 0;
    }

    if (loraStartJustSent) {
        loraStartJustSent   = false;
        // Asteptam cat dureaza receptia pe celelalte unitati
        // La SF9, 48 bytes ≈ 340ms — nu folosim delay() deci folosim un flag cu timestamp
        loraStartPendingTime = millis() + 400;  // ← 400ms delay
    }

    if (loraStartPendingTime > 0 && millis() >= loraStartPendingTime) {
        loraStartPendingTime = 0;
        Serial.print("[START] loraStartGameTimeLeft=");
        Serial.println(loraStartGameTimeLeft);
        Serial.print("[START] gameTimeLeftSeconds=");
        Serial.println(gameTimeLeftSeconds);
        if (loraStartGameTimeLeft > 0) {
            isGameTimerRunning  = true;
            gameTimeLeftSeconds = loraStartGameTimeLeft;
            lastTimerTick       = millis();
            digitalWrite(PIN_RELAY, LOW);
            isRelayActive       = true;
            relayTurnOffTime    = millis() + 5000;
        }
    }

    if (loraSettingsReceived) {
        loraSettingsReceived = false;

        // Game Settings
        gsTimeLimit         = rx_gsTimeLimit;
        gsBonus             = rx_gsBonus;
        gsWinCond           = rx_gsWinCond;
            const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
            bonusIntervalMinutes = bn[gsBonus];
            currentWinCondition  = (WinCondition)gsWinCond;

            // Bomb Settings
            bsTimerIdx           = rx_bsTimerIdx;
            bsCooldownIdx        = rx_bsCooldownIdx;
            bsExpPtsIdx          = rx_bsExpPtsIdx;
            bsDefPtsIdx          = rx_bsDefPtsIdx;
            const uint32_t tv[]  = {5, 10, 15, 20, 30, 45, 60, 120};
            const uint32_t pv[]  = {50, 100, 200, 300, 400, 500, 600, 700,
                800, 900, 1000, 1500, 2000, 2500, 3000};
                bombTimerMs          = tv[bsTimerIdx]   * 60000UL;
                cooldownMs           = tv[bsCooldownIdx] * 60000UL;
                bombPointsExplode    = pv[bsExpPtsIdx];
                bombPointsDefuse     = pv[bsDefPtsIdx];

                // Respawn Settings
                rsTimeIdx            = rx_rsTimeIdx;
                rsPenaltyIdx         = rx_rsPenaltyIdx;
                for (uint8_t i = 0; i < 4; i++) rsLimitIdx[i] = rx_rsLimitIdx[i];
                const uint32_t ts[]  = {10, 30, 60, 120, 180, 240, 300,
                    600, 900, 1200, 1500, 1800};
                    const uint16_t pp[]  = {0, 5, 10, 25, 50, 75, 100};
                    const uint16_t lm[]  = {0, 10, 25, 50, 75, 100, 200, 300, 400, 500, 1000};
                    respawnTimeMs        = ts[rsTimeIdx] * 1000UL;
                    respawnPenaltyPoints = pp[rsPenaltyIdx];
                    for (uint8_t i = 0; i < 4; i++)
                        teamMaxRespawns[i] = lm[rsLimitIdx[i]];

        needsDisplayUpdate = true;
        Serial.println("[SYNC] Setari aplicate de la master.");
    }

    if (loraSyncJustReceived) {
        loraSyncJustReceived = false;
        syncedScreenStart    = millis();
        // Salvam starea doar daca nu suntem deja in admin
        if (currentState != STATE_ADMIN_MENU &&
            currentState != STATE_ADMIN_PAGES &&
            currentState != STATE_ADMIN_SYNC_WARN &&
            currentState != STATE_SYNC_RECEIVED) {
            previousStateBeforeAdmin = currentState;
            }
            currentState       = STATE_SYNCED_RECEIVED;
        needsDisplayUpdate = true;
        tone(PIN_BUZZER, 1500, 500);
    }

    // Restart global primit prin LoRa
    if (loraRestartPending()) {
        display.clearDisplay();
        display.setTextSize(2);
        uint8_t rx = (SCREEN_WIDTH - (strlen("REBOOTING") * 12)) / 2;
        display.setCursor(rx, 24);
        display.print("REBOOTING");
        display.display();
        tone(PIN_BUZZER, 2000, 800);
        unsigned long t = millis();
        while (millis() - t < 1000) {
        }  // Singura exceptie acceptata — inainte de restart
        ESP.restart();
    }
    static bool isBombBeeping = false;

    static bool wasGameTimerRunning = false;
    if (isGameTimerRunning && !wasGameTimerRunning) {
        lastTimerTick = millis();
    }
    wasGameTimerRunning = isGameTimerRunning;

    if (isGameTimerRunning && (now - lastTimerTick > 2000)) {
        lastTimerTick = now - 1000;
    }

    // Scadere timer joc
    if (isGameTimerRunning && gameTimeLeftSeconds > 0) {
        if (now - lastTimerTick >= 1000) {
            lastTimerTick += 1000;
            gameTimeLeftSeconds--;
            if (gameTimeLeftSeconds == 0) {
                Serial.println("[TIMEOUT] gameTimeLeftSeconds ajuns la 0!");
                isGameTimerRunning = false;
                isTimeOut = true;
                digitalWrite(PIN_RELAY, LOW);
                isRelayActive = true;
                relayTurnOffTime = now + 20000;
                currentPage = 0;
                needsDisplayUpdate = true;
                Serial.println("[GAME] TIME OUT!");
            }
        }
    }

    // Oprire releu automata
    if (isRelayActive && now >= relayTurnOffTime) {
        digitalWrite(PIN_RELAY, HIGH);
        isRelayActive = false;
    }

    // Citire baterie la fiecare 10 secunde
    static uint32_t lastBatteryCheck = 0;
    if (now - lastBatteryCheck >= 10000 || lastBatteryCheck == 0) {
        lastBatteryCheck = now;
        updateBattery();
    }

    // Auto-dim display
    if (!isDimmed && (now - lastActivityTime >= 15000)) {
        isDimmed = true;
        setBrightness(10);  // ~10%
    }

    if (selectedMode == 0 && sectorOwner != TEAM_NEUTRAL && lastPointTick == 0) {
        lastPointTick = now;
    }

    // Generare puncte sector (la 10 secunde)
    if (!isTimeOut && selectedMode == 0 && sectorOwner != TEAM_NEUTRAL) {
        if (now - lastPointTick >= 10000) {
            uint32_t minutesHeld = (now - captureStartTime) / 60000;
            uint32_t bonus = (bonusIntervalMinutes > 0) ? (minutesHeld / bonusIntervalMinutes) : 0;
            uint32_t points = 3 + bonus;

            liveScore[sectorOwner - 1] += points;
            currentCapturePoints += points;
            lastPointTick += 10000;  // ← adaugam, nu resetam!
        }
    }

    // Timer bomba
    if (selectedMode == 1 && isBombArmed) {
        uint32_t elapsed = now - bombPlantTime;
        uint32_t remaining = bombTimerMs - elapsed;

        if (elapsed >= bombTimerMs) {
            // EXPLOZIE!
            isBombArmed = false;
            isCooldownActive = true;
            cooldownStartTime = now;
            liveScore[bombOwner - 1] += bombPointsExplode;
            Team explodedBy = bombOwner;  // ← salvam INAINTE de reset
            bombOwner = TEAM_NEUTRAL;
            isBombBeeping = false;
            digitalWrite(PIN_RELAY, LOW);
            isRelayActive = true;
            relayTurnOffTime = now + 10000;
            noTone(PIN_BUZZER);
            currentState = STATE_BOOM;
            needsDisplayUpdate = true;  // ← asta lipsea
            refreshLEDs();
            Serial.println("[BOMB] EXPLOZIE!");
            loraSendUrgent(EVT_BOMB_EXPLODED, (uint8_t)explodedBy);  // ← folosim copia
        } else {
            // Beep ritmic — accelereaza pe masura ce se apropie explozia
            uint32_t interval = 6000;
            if (remaining <= 3000)
                interval = 100;
            else if (remaining <= 6000)
                interval = 300;
            else if (remaining <= 10000)
                interval = 500;
            else if (remaining <= 20000)
                interval = 1000;
            else if (remaining <= 30000)
                interval = 3000;

            bool shouldBeep = (elapsed % interval) < 200;
            if (remaining <= 3000) shouldBeep = true;

            if (shouldBeep && !isBombBeeping) {
                isBombBeeping = true;
                tone(PIN_BUZZER, 1500);
                for (uint8_t i = 0; i < 4; i++)
                    if ((Team)(i + 1) != bombOwner) digitalWrite(PIN_LEDS[i], HIGH);
            } else if (!shouldBeep && isBombBeeping) {
                isBombBeeping = false;
                noTone(PIN_BUZZER);
                refreshLEDs();
            }
        }
    }

    // Cooldown terminat
    if (selectedMode == 1 && isCooldownActive) {
        if (now - cooldownStartTime >= cooldownMs) {
            isCooldownActive = false;
            Serial.println("[BOMB] Cooldown terminat.");
        }
    }

    // Citire RFID
    if ((currentState == STATE_PAGES || currentState == STATE_MENU || currentState == STATE_RESPAWN_SETUP) && (millis() - lastRfidRead >= 1000)) {
        RfidReadData rfid = rfidReadTag();

        if (rfid.result == RFID_READ_ADMIN) {
            // Card Admin — intram in admin mode
            tone(PIN_BUZZER, 2000, 200);
            previousStateBeforeAdmin = currentState;
            adminMenuIndex = 0;
            adminScrollIndex = 0;
            currentAction = ACT_NONE;
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
            Serial.println("[RFID] Admin tag detectat!");
            lastRfidRead = millis();
            resetActivity();

        } else if (rfid.result == RFID_READ_POINTS && selectedMode != -1 && !isTimeOut) {
            // Determinam echipa proprietara
            Team owner = TEAM_NEUTRAL;
            if (selectedMode == 0)
                owner = sectorOwner;
            else if (selectedMode == 1 && isBombArmed)
                owner = bombOwner;
            else if (selectedMode == 2)
                owner = respawnTeam;

            if (owner == TEAM_NEUTRAL) {
                // Nu ardem cardul — jucatorul il poate folosi cand unitatea are proprietar
                tone(PIN_BUZZER, 300, 400);
                Serial.println("[RFID] Niciun proprietar. Cardul ramas intact.");
                lastRfidRead = millis();
            } else {
                // Ardem cardul ACUM, doar dupa ce stim ca avem proprietar valid
                if (rfidBurnTag()) {
                    liveScore[owner - 1] += rfid.points;
                    bonusPoints = rfid.points;
                    bonusScreenStart = millis();
                    currentState = STATE_BONUS_SCREEN;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1200, 200);
                    Serial.print("[RFID] +");
                    Serial.print(rfid.points);
                    Serial.print(" pts -> ");
                    Serial.println(TEAM_NAMES[owner - 1]);
                } else {
                    tone(PIN_BUZZER, 200, 300);
                    Serial.println("[RFID] EROARE la ardere! Punctele NU se acorda.");
                }
                lastRfidRead = millis();
            }

        } else if (rfid.result == RFID_READ_INVALID) {
            tone(PIN_BUZZER, 200, 500);
            Serial.println("[RFID] Card invalid sau checksum gresit!");
            lastRfidRead = millis();
            resetActivity();
        }
    }

    // Iesire jucatori din coada respawn
    static uint8_t flashStep = 0;
    static uint32_t flashStepStart = 0;

    if (selectedMode == 2 && queueCount > 0) {
        if (now >= respawnQueue[queueHead]) {
            queueCount--;
            queueHead = (queueHead + 1) % 100;
            flashStep = 1;
            flashStepStart = now;
            needsDisplayUpdate = true;
            Serial.println("[RESPAWN] Jucator iesit din coada!");
        }
    }

    // Animatia flash ruleaza INDEPENDENT de queueCount
    if (selectedMode == 2 && flashStep > 0 && (now - flashStepStart >= 100)) {
        flashStepStart = now;
        if (flashStep % 2 == 1) {
            for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);
            tone(PIN_BUZZER, 1500, 80);
        } else {
            for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], LOW);
        }
        flashStep++;
        if (flashStep > 6) {
            flashStep = 0;
            refreshLEDs();
        }
    }

    switch (currentState) {
        case STATE_BOOT:
            if (handleBoot()) {
                currentState = STATE_MENU;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_RESPAWN_SETUP:
            if (needsDisplayUpdate) {
                drawRespawnSetup();
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_ADMIN_SYNC_WARN:
            if (needsDisplayUpdate) {
                drawSyncWarningScreen();
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_SYNC_RECEIVED:
            if (needsDisplayUpdate) {
                drawSyncingScreen();
                needsDisplayUpdate = false;
            }
            if (millis() - syncingStartTime >= 2000) {
                currentState = STATE_ADMIN_MENU;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_SYNCED_RECEIVED:
            if (needsDisplayUpdate) {
                drawSyncedScreen(loraSyncFromUnit);
                needsDisplayUpdate = false;
            }
            if (millis() - syncedScreenStart >= 2000) {
                currentState = previousStateBeforeAdmin;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_MENU:
            if (needsDisplayUpdate) {
                drawMenu(menuIndex);
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_BOOM:
            drawBoomScreen();
            if (now - cooldownStartTime >= 3000) {
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_ADMIN_MENU:
            if (needsDisplayUpdate) {
                drawAdminMenu(adminMenuIndex, adminScrollIndex, selectedMode);
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_LOADING: {
            uint32_t elapsed = millis() - loadingStartTime;
            drawLoadingScreen(elapsed, 2000);
            if (elapsed >= 2000) {
                readyScreenStart = millis();
                currentState = STATE_READY;
                needsDisplayUpdate = true;
                for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);
                tone(PIN_BUZZER, 1500, 500);  // ← aici, o singura data
            }
            break;
        }

        case STATE_READY: {
            if (needsDisplayUpdate) {
                drawReadyScreen(selectedMode);
                needsDisplayUpdate = false;
            }
            // Beepuri non-blocking folosind elapsed
            uint32_t el = millis() - readyScreenStart;

            if (el >= 2000) {
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
                refreshLEDs(); // Resetam LED-urile la starea corecta
            }
            break;
        }

        case STATE_WAIT_ADMIN_TAG: {
            if (needsDisplayUpdate) {
                drawWaitAdminTag();
                needsDisplayUpdate = false;
            }

            // Timeout 5 secunde — anulam daca nu apare cardul
            static uint32_t waitAdminStart = 0;
            if (needsDisplayUpdate == false && waitAdminStart == 0) waitAdminStart = millis();

            if (millis() - waitAdminStart >= 5000) {
                waitAdminStart = 0;
                currentState = STATE_PAGES;
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 200, 300);
                break;
            }

            // Citim RFID
            if (millis() - lastRfidRead >= 500) {
                RfidReadData rfid = rfidReadTag();
                lastRfidRead = millis();

                if (rfid.result == RFID_READ_ADMIN) {
                    loraSendStart(gameTimeLeftSeconds);
                    // ← fara releu si fara isGameTimerRunning aici!
                    waitAdminStart = 0;
                    currentState = STATE_PAGES;
                    currentPage = 5;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1500, 200);
                    Serial.println("[GAME] Start transmis!");
                } else if (rfid.result == RFID_READ_POINTS || rfid.result == RFID_READ_INVALID) {
                    // Card gresit — refuz
                    tone(PIN_BUZZER, 200, 300);
                }
            }

            // RED anuleaza
            handleButtons();
            break;
        }

        case STATE_ADMIN_PAGES:
            if (needsDisplayUpdate) {
                AdminContext ac = buildAdminContext();
                drawAdminPages(ac);
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_BONUS_SCREEN:
            drawBonusScreen(bonusPoints);
            if (millis() - bonusScreenStart >= 2000) {
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_ADMIN_SAVED:
            drawAdminSaved();
            if (millis() - adminSavedTime >= 2000) {
                currentState = STATE_ADMIN_MENU;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_ADMIN_TAG_WRITE: {
            // Timeout 3 secunde
            if (tagStatusMsg == 0 && millis() - tagWaitStart >= 3000) {
                tagStatusMsg = 6;
                tagStatusTime = millis();
                tone(PIN_BUZZER, 200, 300);
            }

            // Afisam mesajul curent
            drawTagWriter(tagStatusMsg);

            // Dupa 2 secunde pe ecranul de rezultat, revenim
            if (tagStatusMsg != 0 && millis() - tagStatusTime >= 2000) {
                currentState = STATE_ADMIN_PAGES;
                tagStatusMsg = 0;
                needsDisplayUpdate = true;
                break;
            }

            // Daca asteptam card, incercam sa scriem
            if (tagStatusMsg == 0) {
                const uint16_t ptsOpt[] = {50, 100, 150, 200, 250, 500, 750, 1000, 1500, 2000, 0};
                uint8_t type = (twOptionIdx == 10) ? 2 : 1;
                uint16_t points = ptsOpt[twOptionIdx];

                RfidWriteResult r = rfidWriteTag(type, points);

                if (r == RFID_TIMEOUT) break;  // Niciun card inca, continuam

                // Am obtinut un rezultat
                switch (r) {
                    case RFID_OK_NEW:
                        tagStatusMsg = 1;
                        tone(PIN_BUZZER, 1500, 500);
                        break;
                    case RFID_OK_OVERWRITE:
                        tagStatusMsg = 2;
                        tone(PIN_BUZZER, 1500, 500);
                        break;
                    case RFID_OK_ADMIN:
                        tagStatusMsg = 3;
                        tone(PIN_BUZZER, 1500, 500);
                        break;
                    case RFID_OK_REVOKED:
                        tagStatusMsg = 4;
                        tone(PIN_BUZZER, 1500, 500);
                        break;
                    case RFID_DENIED:
                        tagStatusMsg = 5;
                        tone(PIN_BUZZER, 200, 400);
                        break;
                    default:
                        tagStatusMsg = 7;
                        tone(PIN_BUZZER, 200, 300);
                        break;
                }
                tagStatusTime = millis();
            }
            handleButtons();
            break;
        }

                    case STATE_PAGES: {
                        static uint32_t lastRefresh = 0;
                        uint32_t refreshInterval = 200;
                        if (selectedMode == 1 && isBombArmed && currentPage == 0) refreshInterval = 50;
                        if (needsDisplayUpdate || (millis() - lastRefresh >= refreshInterval)) {
                            buildContext();
                            drawPages(ctx);
                            needsDisplayUpdate = false;
                            lastRefresh = millis();
                        }
                        handleButtons();
                        break;
                    }

                    case STATE_ACTION:
                        // Nu apelam handleButtons() aici — citim direct starea butonului
                        // in handleActionProgress() prin isButtonHeld()
                        handleActionProgress();
                        break;

                    case STATE_SUCCESS:
                        // Asteptam 3 secunde apoi revenim la pagini
                        if (now - successStartTime >= 3000) {
                            currentState = STATE_PAGES;
                            currentPage = 0;
                            needsDisplayUpdate = true;
                        }
                        break;

                    default:
                        break;
    }
}

// ============================================================
// CALLBACKS BUTOANE
// ============================================================

void onShortPress(uint8_t btnIndex) {
    resetActivity();
    tone(PIN_BUZZER, 800, 30);

    if (currentState == STATE_MENU) {
        if (btnIndex == 2) {
            menuIndex++;
            if (menuIndex > 2) menuIndex = 0;
            needsDisplayUpdate = true;
        } else if (btnIndex == 3) {
            selectedMode = menuIndex;
            if (selectedMode == 2) {
                currentState = STATE_RESPAWN_SETUP;
            } else {
                currentState = STATE_LOADING;
                loadingStartTime = millis();
            }
            needsDisplayUpdate = true;
            Serial.print("[MENU] Mod selectat: ");
            Serial.println(selectedMode);
            if (selectedMode == 0)
                loraSendUrgent(EVT_MODE_SECTOR, 0);
            else if (selectedMode == 1)
                loraSendUrgent(EVT_MODE_BOMB, 0);
            else if (selectedMode == 2)
                loraSendUrgent(EVT_MODE_RESPAWN, (uint8_t)respawnTeam);
        }

    } else if (currentState == STATE_PAGES) {
        // GALBEN in modul Respawn — kill inainte de orice navigare
        if (selectedMode == 2 && btnIndex == 3 &&
            !(currentPage == 5 && gameTimeLeftSeconds > 0 && !isGameTimerRunning)) {
            bool limitReached = (teamMaxRespawns[respawnTeam - 1] > 0 && teamKills[respawnTeam - 1] >= teamMaxRespawns[respawnTeam - 1]);

            if (!isTimeOut && !limitReached && queueCount < 100 && !(gameTimeLeftSeconds > 0 && !isGameTimerRunning)) {
                respawnQueue[queueTail] = millis() + respawnTimeMs;
                queueTail = (queueTail + 1) % 100;
                queueCount++;
                teamKills[respawnTeam - 1]++;
                int32_t penalizare = min((int32_t)respawnPenaltyPoints,
                                         liveScore[respawnTeam - 1] - appliedPenalties[respawnTeam - 1]);
                if (penalizare > 0) appliedPenalties[respawnTeam - 1] += penalizare;
                tone(PIN_BUZZER, 1200, 100);
                needsDisplayUpdate = true;
                Serial.print("[RESPAWN] Kill inregistrat. Queue: ");
                Serial.println(queueCount);
            } else if (gameTimeLeftSeconds > 0 && !isGameTimerRunning) {
                tone(PIN_BUZZER, 200, 200);
            } else if (limitReached) {
                tone(PIN_BUZZER, 200, 300);
                return;
            }
            return;
        }

        // Navigare normala
        if (btnIndex == 0) {
            currentPage = (currentPage == 0) ? 5 : currentPage - 1;
            needsDisplayUpdate = true;
        } else if (btnIndex == 1) {
            currentPage = (currentPage >= 5) ? 0 : currentPage + 1;
            needsDisplayUpdate = true;
        } else if (btnIndex == 2) {
            if (currentPage == 3) {
                ctx.page4ScrollIndex++;
                needsDisplayUpdate = true;
            } else if (currentPage == 4) {
                ctx.page5ScrollIndex++;
                needsDisplayUpdate = true;
            }
        } else if (btnIndex == 3 && currentPage == 5) {
            // YELLOW pe pagina 6 — cerem card admin pentru start
            if (gameTimeLeftSeconds > 0 && !isGameTimerRunning) {
                currentState = STATE_WAIT_ADMIN_TAG;
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 1000, 50);
            } else {
                tone(PIN_BUZZER, 200, 200);  // Nu avem time limit setat
            }
        }

    } else if (currentState == STATE_RESPAWN_SETUP) {
        respawnTeam = (Team)(btnIndex + 1);
        currentState = STATE_LOADING;
        loadingStartTime = millis();
        queueCount = queueHead = queueTail = 0;
        memset(respawnQueue, 0, sizeof(respawnQueue));
        refreshLEDs();
        needsDisplayUpdate = true;
        Serial.print("[RESPAWN] Echipa selectata: ");
        Serial.println(TEAM_NAMES[btnIndex]);
    } else if (currentState == STATE_ADMIN_MENU) {
        if (btnIndex == 2) {
            // VERDE — scroll jos
            adminMenuIndex++;
            if (adminMenuIndex == 5 && selectedMode == -1) adminMenuIndex++;
            if (adminMenuIndex >= 7) {
                adminMenuIndex = 0;
                adminScrollIndex = 0;
            } else {
                uint8_t vis = 0;
                for (uint8_t i = adminScrollIndex; i <= adminMenuIndex; i++) {
                    if (i == 5 && selectedMode == -1) continue;
                    vis++;
                }
                while (vis > 5) {
                    adminScrollIndex++;
                    if (adminScrollIndex == 4 && selectedMode == -1) adminScrollIndex++;
                    vis--;
                }
            }
            needsDisplayUpdate = true;

        } else if (btnIndex == 3) {
            if (adminMenuIndex == 3) {
                currentState = STATE_ADMIN_SYNC_WARN;
                needsDisplayUpdate = true;
            }
            // GALBEN — confirmare
            else if (adminMenuIndex == 5) {
                // CHANGE MODE
                selectedMode = -1;
                sectorOwner = TEAM_NEUTRAL;
                isBombArmed = false;
                isCooldownActive = false;
                respawnTeam = TEAM_NEUTRAL;
                queueCount = queueHead = queueTail = 0;
                currentState = STATE_MENU;
                menuIndex = 0;
                adminMenuIndex = 0;
                adminScrollIndex = 0;
                refreshLEDs();
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 1000, 300);
                loraSendUrgent(EVT_MODE_UNSET, 0);
            } else if (adminMenuIndex == 6) {
                // SYSTEM RESTART
                display.clearDisplay();
                display.setTextSize(2);
                uint8_t x = (SCREEN_WIDTH - (strlen("REBOOTING") * 12)) / 2;
                display.setCursor(x, 24);
                display.print("REBOOTING");
                display.display();
                tone(PIN_BUZZER, 2000, 800);
                loraSendRestartBlocking();
                ESP.restart();

            } else {
                if      (adminMenuIndex == 0) adminSelectedPage = 0;
                else if (adminMenuIndex == 1) adminSelectedPage = 1;
                else if (adminMenuIndex == 2) adminSelectedPage = 2;
                else if (adminMenuIndex == 4) adminSelectedPage = 3;
                syncAdminIndices();  // ← o singura linie in loc de tot blocul
                currentState = STATE_ADMIN_PAGES;
                needsDisplayUpdate = true;
            }

        } else if (btnIndex == 0) {
            // ROSU — inapoi la joc
            currentState = previousStateBeforeAdmin;
            needsDisplayUpdate = true;
        }

    } else if (currentState == STATE_ADMIN_PAGES) {
        if (btnIndex == 0) {
            // ROSU — inapoi la meniu admin
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
            return;
        }

        if (adminSelectedPage == 0) {
            if (btnIndex == 2) {
                gsIndex = (gsIndex + 1) % 4;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if (gsIndex == 0)
                    gsWinCond = (gsWinCond + 1) % 3;
                else if (gsIndex == 1)
                    gsTimeLimit = (gsTimeLimit + 1) % 15;
                else if (gsIndex == 2)
                    gsBonus = (gsBonus + 1) % 7;
                else if (gsIndex == 3) {
                    // CONFIRM
                    const uint32_t tl[] = {0, 10, 3600, 7200, 10800, 14400, 18000, 21600, 25200, 28800, 32400, 36000, 39600, 43200, 86400};
                    const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
                    gameTimeLeftSeconds = tl[gsTimeLimit];
                    bonusIntervalMinutes = bn[gsBonus];
                    currentWinCondition = (WinCondition)gsWinCond;
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 1) {
            if (btnIndex == 2) {
                bsIndex = (bsIndex + 1) % 5;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if (bsIndex == 0)
                    bsTimerIdx = (bsTimerIdx + 1) % 8;
                else if (bsIndex == 1)
                    bsCooldownIdx = (bsCooldownIdx + 1) % 8;
                else if (bsIndex == 2)
                    bsExpPtsIdx = (bsExpPtsIdx + 1) % 15;
                else if (bsIndex == 3)
                    bsDefPtsIdx = (bsDefPtsIdx + 1) % 15;
                else if (bsIndex == 4) {
                    const uint32_t tv[] = {5, 10, 15, 20, 30, 45, 60, 120};
                    const uint32_t pv[] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000, 2500, 3000};
                    bombTimerMs = tv[bsTimerIdx] * 60000UL;
                    cooldownMs = tv[bsCooldownIdx] * 60000UL;
                    bombPointsExplode = pv[bsExpPtsIdx];
                    bombPointsDefuse = pv[bsDefPtsIdx];
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 2) {
            if (btnIndex == 2) {
                rsIndex = (rsIndex + 1) % 7;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if (rsIndex == 0)
                    rsTimeIdx = (rsTimeIdx + 1) % 12;
                else if (rsIndex == 1)
                    rsPenaltyIdx = (rsPenaltyIdx + 1) % 7;
                else if (rsIndex >= 2 && rsIndex <= 5)
                    rsLimitIdx[rsIndex - 2] = (rsLimitIdx[rsIndex - 2] + 1) % 11;
                else if (rsIndex == 6) {
                    const uint32_t ts[] = {10, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1500, 1800};
                    const uint16_t pp[] = {0, 5, 10, 25, 50, 75, 100};
                    const uint16_t lm[] = {0, 10, 25, 50, 75, 100, 200, 300, 400, 500, 1000};
                    respawnTimeMs = ts[rsTimeIdx] * 1000UL;
                    respawnPenaltyPoints = pp[rsPenaltyIdx];
                    for (uint8_t i = 0; i < 4; i++) teamMaxRespawns[i] = lm[rsLimitIdx[i]];
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 3) {
            if (btnIndex == 2) {
                twIndex = (twIndex + 1) % 2;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if (twIndex == 0) {
                    twOptionIdx = (twOptionIdx + 1) % 11;
                    needsDisplayUpdate = true;
                } else {
                    // START WRITE
                    tagStatusMsg = 0;
                    tagWaitStart = millis();
                    currentState = STATE_ADMIN_TAG_WRITE;
                    tone(PIN_BUZZER, 1000, 50);
                    needsDisplayUpdate = true;
                }
            }
        }
    } else if (currentState == STATE_WAIT_ADMIN_TAG) {
        if (btnIndex == 0) {  // RED — anulare
            currentState = STATE_PAGES;
            needsDisplayUpdate = true;
            tone(PIN_BUZZER, 200, 200);
        }
    } else if (currentState == STATE_ADMIN_SYNC_WARN) {
        if (btnIndex == 0) {
            // RED — inapoi la admin menu
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
        } else if (btnIndex == 1) {
            loraSendSync(
                gsTimeLimit, gsBonus, gsWinCond,
                bsTimerIdx, bsCooldownIdx,
                bsExpPtsIdx, bsDefPtsIdx,
                rsTimeIdx, rsPenaltyIdx,
                rsLimitIdx,
                isGameTimerRunning, isTimeOut,
                gameTimeLeftSeconds,
                liveScore,
                teamKills,
                appliedPenalties,
                lastTimerTick  // ← adauga doar asta
            );
            syncingStartTime = millis();
            currentState = STATE_SYNC_RECEIVED;
            needsDisplayUpdate = true;
            tone(PIN_BUZZER, 1800, 400);
        }
    }
}

void onLongPress(uint8_t btnIndex) {
    resetActivity();
    // Actiunile de gameplay sunt permise doar pe pagina 1, in STATE_PAGES
    if (currentState != STATE_PAGES || isTimeOut) return;
    if (gameTimeLeftSeconds > 0 && !isGameTimerRunning) {
        tone(PIN_BUZZER, 200, 200);
        return;
    }

    Team btnTeam = (Team)(btnIndex + 1);  // 0=Rosu -> TEAM_RED(1), etc.

    if (selectedMode == 0) {  // SECTOR UNIT
        if (sectorOwner == TEAM_NEUTRAL) {
            // Sector neutru -> oricine poate cuceri
            currentAction = ACT_CAPTURE;
        } else if (btnTeam == sectorOwner) {
            // Echipa proprie nu poate neutraliza propriul sector
            tone(PIN_BUZZER, 200, 200);
            return;
        } else {
            // Alta echipa -> neutralizare
            currentAction = ACT_NEUTRALIZE;
        }

        actingTeam = btnIndex;
        actionStartTime = millis();
        currentState = STATE_ACTION;
        tone(PIN_BUZZER, 1000, 100);
        Serial.print("[ACTION] Start: ");
        Serial.println(currentAction == ACT_CAPTURE ? "CAPTURE" : "NEUTRALIZE");
    } else if (selectedMode == 1) {  // BOMB UNIT
        if (isCooldownActive) {
            tone(PIN_BUZZER, 200, 200);
            return;
        }
        if (!isBombArmed) {
            // Oricine poate arma bomba
            currentAction = ACT_ARM;
        } else {
            // Echipa care a amorsat NU poate dezamorsa
            if (btnTeam == bombOwner) {
                tone(PIN_BUZZER, 200, 200);
                return;
            }
            currentAction = ACT_DEFUSE;
        }
        actingTeam = btnIndex;
        actionStartTime = millis();
        currentState = STATE_ACTION;
        tone(PIN_BUZZER, 1000, 100);
    }
}

void onAdminCombo() {
    resetActivity();
    tone(PIN_BUZZER, 2000, 500);
    previousStateBeforeAdmin = currentState;
    adminMenuIndex = 0;
    adminScrollIndex = 0;
    currentState = STATE_ADMIN_MENU;
    needsDisplayUpdate = true;
    Serial.println("[ADMIN] Intram in Admin Mode.");
}
