// ============================================================
// NOVA Units — sketch principal
// Faza 1 (interfata). Pasul 3: cele 6 pagini (statice).
// ============================================================
#include "config.h"
#include "display.h"
#include "buttons.h"
#include "game.h"
#include "rfid.h"
#include "lora.h"

#define FW_VERSION "NU-diag-1"   // DIAG: bumpeaza la fiecare build ca sa verifici ca toate unitatile au acelasi cod

#include <esp_wifi.h>
#include <esp_bt.h>

// ============================================================
// Stare masina de stari
// ============================================================
GameState currentState      = STATE_BOOT;
bool      needsDisplayUpdate = true;

// Anti-drift display
uint32_t lastDisplayRefresh = 0;

// --- Meniu / UI ---
uint8_t  menuIndex        = 0;
uint32_t loadingStartTime = 0;
uint32_t readyScreenStart = 0;

// --- Pagini ---
uint8_t     currentPage      = 0;
uint8_t     page4ScrollIndex = 0;
uint8_t     page5ScrollIndex = 0;
PageContext ctx;

// --- Sistem ---
uint8_t batteryPercent = 100;

// --- Power off (latch) ---
uint32_t powerOffStart  = 0;
bool     powerOffPulsed = false;
uint32_t latchLowStart  = 0;
bool     latchPulsing   = false;

// --- Admin ---
uint8_t   adminMenuIndex    = 0;
uint8_t   adminScrollIndex  = 0;
uint8_t   adminSelectedPage = 0;
uint32_t  adminSavedTime    = 0;
GameState previousStateBeforeAdmin = STATE_MENU;
GameState syncReturnState          = STATE_MENU;

// Indecsi setari admin (UI)
uint8_t gsIndex = 0, gsWinCond = 0, gsTimeLimit = 0, gsBonus = 2, gsActionIdx = 2;
uint8_t bsIndex = 0, bsTimerIdx = 2, bsCooldownIdx = 3, bsExpPtsIdx = 6, bsDefPtsIdx = 3;
uint8_t rsIndex = 0, rsTimeIdx = 6, rsPenaltyIdx = 3;
uint8_t rsLimitIdx[4] = {0, 0, 0, 0};
uint8_t twIndex = 0, twOptionIdx = 0;

// --- RFID ---
uint8_t  tagStatusMsg  = 0;
uint32_t tagStatusTime = 0;
uint32_t tagWaitStart  = 0;
uint32_t lastRfidRead  = 0;

// --- Actiuni / gameplay ---
ActionType currentAction    = ACT_NONE;
uint8_t    actingTeam       = 0;
uint32_t   actionStartTime  = 0;
uint32_t   successStartTime = 0;
bool       isRelayActive    = false;
uint32_t   relayTurnOffTime = 0;
bool       isBombBeeping    = false;
uint8_t    flashStep        = 0;
uint32_t   flashStepStart   = 0;

// --- Timer / pauza joc ---
uint32_t   lastTimerTick      = 0;
uint8_t    pendingAdminAction = 0;   // 0=start, 1=pause, 2=resume, 3=reset ceas
uint32_t   waitAdminTagStart  = 0;
uint32_t   rfidIgnoreUntil    = 0;   // ignoram RFID dupa o confirmare (sa scoatem cardul)

// --- Auto-dim / auto-return ---
uint32_t   lastActivityTime   = 0;
bool       isDimmed           = false;

// --- Kill reset ---
uint32_t   killResetAdminStart = 0;
uint8_t    killResetWinnerTeam = 255;
uint16_t   killResetPoints     = 0;
bool       killResetHasPoints  = false;
uint32_t   killResetDoneStart  = 0;
uint32_t   lastKillResetAt     = 0;   // dedup KILLRESET
uint32_t   lastDiagPrint       = 0;   // DIAG: ultimul dump pe serial

// --- Card puncte (bonus) ---
uint16_t   bonusPoints         = 0;
uint8_t    bonusTeam           = 0;
uint32_t   bonusScreenStart    = 0;
uint32_t   syncedScreenStart   = 0;
uint32_t   syncDoneStart       = 0;
GameState  alertReturnState    = STATE_MENU;
uint8_t    timeAlertAction     = 0;
uint32_t   timeAlertStart      = 0;
bool       emitterApplyArmed   = false;   // expeditor: aplica actiunea cand TX s-a golit
uint8_t    emitterPendingAction = 0;
uint32_t   lastTimeAt[4]        = {0, 0, 0, 0};   // dedup TIME: momentul ultimei procesari per actiune
bool       sectorApplyArmed    = false;   // cucerire/neutralizare locala dupa AUX LOW
uint8_t    sectorApplyType     = 0;       // 0=capture, 1=neutralize
uint8_t    sectorApplyTeam     = 0;
int32_t    sectorApplyPts      = 0;
uint32_t   blockMsgStart       = 0;
uint32_t   respawnWindowStart  = 0;
GameState  blockReturnState    = STATE_ADMIN_MENU;

// (selectedMode si starea de joc sunt in game.cpp — modelul/tabelul)

// ============================================================
// refreshLEDs()
// Aprinde LED-ul echipei proprietare + orice buton tinut fizic.
// ============================================================
void resetActivity() {
    lastActivityTime = millis();
    if (isDimmed) {
        isDimmed = false;
        setBrightness(175);   // luminozitate normala
    }
}

void refreshLEDs() {
    for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], LOW);

    // LED-ul echipei proprietare — mereu aprins (din randul propriu)
    Team owner = TEAM_NEUTRAL;
    if (selectedMode == 0 && myRow().status == SEC_CAPTURED) owner = myRow().team;
    else if (selectedMode == 1 && myRow().status == BOMB_ARMED) owner = myRow().team;
    else if (selectedMode == 2) owner = myRow().team;
    if (owner != TEAM_NEUTRAL) digitalWrite(PIN_LEDS[owner - 1], HIGH);

    // LED-urile butoanelor fizic apasate
    for (uint8_t i = 0; i < 4; i++)
        if (digitalRead(PIN_BTNS[i]) == LOW) digitalWrite(PIN_LEDS[i], HIGH);
}

// ============================================================
// updateBattery() — GPIO36, divizor 10k/3.3k, smoothing 80/20
// ============================================================
void updateBattery() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 20; i++) sum += analogRead(PIN_BATTERY);
    float adcAvg = sum / 20.0;
    float pinV = (adcAvg / 4095.0) * 3.56;
    float batV = pinV * ((10.0 + 3.3) / 3.3);

    uint8_t newPercent = 0;
    if (batV >= 12.6)      newPercent = 100;
    else if (batV <= 9.0)  newPercent = 0;
    else                   newPercent = (uint8_t)(((batV - 9.0) / (12.6 - 9.0)) * 100.0);

    if (batteryPercent == 100) batteryPercent = newPercent;
    else batteryPercent = (batteryPercent * 80 + newPercent * 20) / 100;
}

// ============================================================
// buildAdminContext() — impacheteaza indicii admin pentru drawAdminPages
// ============================================================
AdminContext buildAdminContext() {
    AdminContext ac;
    ac.selectedPage = adminSelectedPage;
    ac.gsIndex = gsIndex;
    ac.gsWinCond = gsWinCond;
    ac.gsTimeLimit = gsTimeLimit;
    ac.gsBonus = gsBonus;
    ac.gsActionIdx = gsActionIdx;
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
// syncAdminIndices() — recalculeaza indicii admin din valorile modelului
// (ca sub-pagina sa afiseze setarile curente cand o deschizi)
// ============================================================
void syncAdminIndices() {
    gsIndex = bsIndex = rsIndex = twIndex = 0;

    const uint32_t tl[] = {0, 900, 1800, 3600, 7200, 10800, 14400, 18000,
                           21600, 25200, 28800, 32400, 36000, 39600, 43200, 86400};
    for (uint8_t i = 0; i < 16; i++)
        if (tl[i] == gameTimeLimitSeconds) { gsTimeLimit = i; break; }

    const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
    for (uint8_t i = 0; i < 7; i++)
        if (bn[i] == bonusIntervalMinutes) { gsBonus = i; break; }

    gsWinCond = (uint8_t)currentWinCondition;

    const uint32_t at[] = {5000, 10000, 15000, 20000};
    for (uint8_t i = 0; i < 4; i++)
        if (at[i] == actionTimeMs) { gsActionIdx = i; break; }

    const uint32_t tv[] = {5, 10, 15, 20, 30, 45, 60, 120};
    for (uint8_t i = 0; i < 8; i++)
        if (tv[i] * 60000UL == bombTimerMs) { bsTimerIdx = i; break; }
    for (uint8_t i = 0; i < 8; i++)
        if (tv[i] * 60000UL == cooldownMs) { bsCooldownIdx = i; break; }

    const uint32_t pv[] = {50, 100, 200, 300, 400, 500, 600, 700,
                           800, 900, 1000, 1500, 2000, 2500, 3000};
    for (uint8_t i = 0; i < 15; i++)
        if (pv[i] == bombPointsExplode) { bsExpPtsIdx = i; break; }
    for (uint8_t i = 0; i < 15; i++)
        if (pv[i] == bombPointsDefuse) { bsDefPtsIdx = i; break; }

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
// handleActionProgress() — hold-to-progress + aplicare actiune
// ============================================================
void handleActionProgress() {
    uint32_t now = millis();
    uint32_t elapsed = now - actionStartTime;

    // 0. Dezamorsam, dar bomba a explodat intre timp -> BOOM
    if (currentAction == ACT_DEFUSE && myRow().status != BOMB_ARMED) {
        currentAction = ACT_NONE;
        currentState = STATE_BOOM;
        needsDisplayUpdate = true;
        return;
    }

    // 1. Buton eliberat -> ANULARE
    if (digitalRead(PIN_BTNS[actingTeam]) == HIGH) {
        currentState = STATE_PAGES;
        currentPage = 0;
        currentAction = ACT_NONE;
        needsDisplayUpdate = true;
        tone(PIN_BUZZER, 300, 300);
        refreshLEDs();
        Serial.println("[ACTION] Anulat - buton eliberat.");
        return;
    }

    // 2. Timp atins -> SUCCESS
    if (elapsed >= actionTimeMs) {
        if (currentAction == ACT_CAPTURE) {
            // trimitem alerta in fundal; declaram cucerirea local dupa AUX LOW
            loraSendCapture(actingTeam + 1);
            sectorApplyArmed = true;
            sectorApplyType  = 0;
            sectorApplyTeam  = actingTeam + 1;
            Serial.print("[SECTOR] Cucerire trimisa: ");
            Serial.println(TEAM_NAMES[actingTeam]);
        } else if (currentAction == ACT_NEUTRALIZE) {
            // puncte exacte = contorul live al acestei unitati; trimitem, aplicam dupa AUX LOW
            sectorApplyPts   = liveCapture[UNIT_ID - 1];
            loraSendNeutralize((uint8_t)myRow().team, sectorApplyPts);
            sectorApplyArmed = true;
            sectorApplyType  = 1;
            sectorApplyTeam  = (uint8_t)myRow().team;
            Serial.print("[SECTOR] Neutralizare trimisa, pts=");
            Serial.println(sectorApplyPts);
        } else if (currentAction == ACT_ARM) {
            myRow().status     = BOMB_ARMED;
            myRow().team       = (Team)(actingTeam + 1);
            myRow().actionTime = now;
            loraSendBombPlant((uint8_t)(actingTeam + 1));   // anuntam reteaua
            noTone(PIN_BUZZER);
            Serial.print("[BOMB] Amorsata de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
        } else if (currentAction == ACT_DEFUSE) {
            myRow().status     = BOMB_COOLDOWN;
            myRow().team       = TEAM_NEUTRAL;
            myRow().actionTime = now;                 // start cooldown
            myRow().savedPoints[actingTeam] += (int32_t)bombPointsDefuse;
            loraSendBombDefuse((uint8_t)(actingTeam + 1));   // anuntam reteaua
            noTone(PIN_BUZZER);
            Serial.print("[BOMB] Dezamorsata de: ");
            Serial.println(TEAM_NAMES[actingTeam]);
        }

        // Releu scurt (sirena)
        digitalWrite(PIN_RELAY, LOW);
        isRelayActive = true;
        relayTurnOffTime = now + 5000;

        currentState = STATE_SUCCESS;
        successStartTime = now;
        currentAction = ACT_NONE;
        drawSuccessScreen();
        refreshLEDs();
        tone(PIN_BUZZER, 1800, 600);
        return;
    }

    // 3. In desfasurare -> bara de progres
    drawActionScreen(currentAction, actingTeam, elapsed, actionTimeMs);
}

// ============================================================
// applyGamePause() / applyGameResume()
// ============================================================
void applyGamePause() {
    isGamePaused = true;
    pauseStartTime = millis();
    needsDisplayUpdate = true;
    Serial.println("[GAME] PAUSED!");
}

void applyGameResume() {
    uint32_t pauseDuration = millis() - pauseStartTime;
    isGamePaused = false;
    // Ajustam toti timpii absoluti cu durata pauzei (ca sa nu "sara" timpul)
    if (isGameTimerRunning)      lastTimerTick += pauseDuration;
    // sectoare cucerite (toate unitatile): ajustam actionTime + tick
    for (uint8_t u = 0; u < MAX_UNITS; u++) {
        if (unitTable[u].mode == 1 && unitTable[u].status == SEC_CAPTURED) {
            if (unitTable[u].actionTime > 0) unitTable[u].actionTime += pauseDuration;
            if (lastPointTick[u] > 0)        lastPointTick[u] += pauseDuration;
        }
    }
    // bombe (toate unitatile, arm/cooldown): ajustam actionTime
    for (uint8_t u = 0; u < MAX_UNITS; u++) {
        if (unitTable[u].mode == 2 && (unitTable[u].status == BOMB_ARMED || unitTable[u].status == BOMB_COOLDOWN) && unitTable[u].actionTime > 0)
            unitTable[u].actionTime += pauseDuration;
    }
    for (uint8_t i = 0; i < queueCount; i++) {
        uint8_t idx = (queueHead + i) % 100;
        respawnQueue[idx] += pauseDuration;
    }
    needsDisplayUpdate = true;
    Serial.println("[GAME] RESUMED!");
}

// ============================================================
// applyTimerAction() — aplica local actiunea de timp (0=start,1=pause,2=resume,3=reset)
// ============================================================
void applyTimerAction(uint8_t action) {
    if (action == 0) {            // START
        isGameTimerRunning = true;
        lastTimerTick = millis();
        digitalWrite(PIN_RELAY, LOW);          // alarma 3s la start (emitator + receptoare)
        isRelayActive = true;
        relayTurnOffTime = millis() + 3000;
        Serial.println("[GAME] START!");
    } else if (action == 1) {     // PAUSE
        applyGamePause();
    } else if (action == 2) {     // RESUME
        applyGameResume();
    } else if (action == 3) {     // RESET CEAS
        gameTimeLeftSeconds = gameTimeLimitSeconds;
        isGameTimerRunning  = false;
        isTimeOut           = false;
        lastTimerTick       = millis();
        Serial.println("[GAME] Ceas resetat la timpul initial!");
    }
}

// ============================================================
// applyCapture() / applyNeutralize() — folosite si local (dupa AUX LOW) si la receptie
// ============================================================
void applyCapture(uint8_t u, uint8_t team) {
    unitTable[u].status     = SEC_CAPTURED;
    unitTable[u].team       = (Team)team;
    unitTable[u].actionTime = millis();
    liveCapture[u]   = 0;
    lastPointTick[u] = millis();
}

void applyNeutralize(uint8_t u, uint8_t team, int32_t exactPts) {
    if (team >= 1 && team <= 4) {
        unitTable[u].savedPoints[team - 1] -= liveCapture[u];   // scot estimarea live
        unitTable[u].savedPoints[team - 1] += exactPts;         // pun valoarea exacta (de la sursa)
    }
    liveCapture[u]   = 0;
    lastPointTick[u] = 0;
    unitTable[u].status     = SEC_NEUTRAL;
    unitTable[u].team       = TEAM_NEUTRAL;
    unitTable[u].actionTime = millis();
}

// ============================================================
// applyKillsReset() — reset kill-uri pe tot tabelul + coada
// ============================================================
void applyKillsReset() {
    for (uint8_t u = 0; u < MAX_UNITS; u++)
        for (uint8_t t = 0; t < 4; t++)
            unitTable[u].kills[t] = 0;
    for (uint8_t t = 0; t < 4; t++)
        appliedPenalties[t] = 0;   // penalizarile sunt derivate din kill-uri -> le resetam impreuna
        queueCount = 0;
    queueHead = 0;
    queueTail = 0;
    needsDisplayUpdate = true;
    Serial.println("[KILLS] Reset kill-uri si coada respawn!");
}

// ============================================================
// doReboot() — ecran REBOOTING + beep continuu + restart
// ============================================================
void doReboot() {
    display.clearDisplay();
    display.setTextSize(2);
    uint8_t x = (SCREEN_WIDTH - (strlen("REBOOTING") * 12)) / 2;
    display.setCursor(x, 24);
    display.print("REBOOTING");
    display.display();
    tone(PIN_BUZZER, 2000);                          // beep continuu
    for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);   // feedback vizual
    uint32_t t = millis();
    while (millis() - t < 1500) { loraTxUpdate(); }  // pompam coada (iese RESTART) inainte de restart
    noTone(PIN_BUZZER);
    ESP.restart();
}

void setup() {
    // Oprim WiFi + Bluetooth (economie + stabilitate)
    esp_wifi_stop();
    esp_bt_controller_disable();

    Serial.begin(115200);

    // Seed per-unitate pentru jitter-ul copiei a doua (UNIT_ID garanteaza secvente diferite)
    randomSeed(micros() ^ ((uint32_t)UNIT_ID * 2654435761UL));

    // Power latch — tinem alimentarea pornita
    pinMode(PIN_LATCH, OUTPUT);
    digitalWrite(PIN_LATCH, HIGH);

    // LED-uri
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(PIN_LEDS[i], OUTPUT);
        digitalWrite(PIN_LEDS[i], LOW);
    }

    // Releu (open-drain, HIGH = oprit) + Buzzer
    pinMode(PIN_RELAY, OUTPUT_OPEN_DRAIN);
    digitalWrite(PIN_RELAY, HIGH);
    pinMode(PIN_BUZZER, OUTPUT);

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

    // LoRa: ceas comun + coada TX (background) + receptie pachete
    loraTick();
    loraTxUpdate();
    if (emitterApplyArmed && loraTxIdle()) {   // alerta de timp a iesit (AUX LOW) -> aplicam local
        applyTimerAction(emitterPendingAction);
        emitterApplyArmed = false;
    }
    if (sectorApplyArmed && loraTxIdle()) {    // alerta de sector a iesit (AUX LOW) -> aplicam local
        if (sectorApplyType == 0) applyCapture(UNIT_ID - 1, sectorApplyTeam);
        else                      applyNeutralize(UNIT_ID - 1, sectorApplyTeam, sectorApplyPts);
        sectorApplyArmed = false;
    }
    if (respawnWindowStart != 0 && now >= respawnWindowStart && now - respawnWindowStart >= 15000) {
        if (myRow().team != TEAM_NEUTRAL)
            loraSendRespawn((uint8_t)myRow().team, myRow().kills[myRow().team - 1]);   // trimitem totalul curent
            respawnWindowStart = 0;
    }
    if (currentState != STATE_SYNCING && currentState != STATE_SYNC_WARNING &&
        currentState != STATE_SYNCED && currentState != STATE_SYNC_DONE) {
        LoraEvent ev = loraPoll();
    if (ev == LORA_EVT_SYNC) {
        syncReturnState = currentState;   // de unde am venit (revenire daca nu e mod)
        syncedScreenStart = now;
        currentState = STATE_SYNCED;
        needsDisplayUpdate = true;
        for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);
        tone(PIN_BUZZER, 1500, 600);
    } else if (ev == LORA_EVT_RESTART) {
        doReboot();
    } else if (ev == LORA_EVT_TIME) {
        // dedup: ignoram copia a doua a aceleiasi actiuni (fereastra > slotul maxim ~8.6s)
        if (loraTimeAction < 4 &&
            (lastTimeAt[loraTimeAction] == 0 || now - lastTimeAt[loraTimeAction] >= 12000)) {
            lastTimeAt[loraTimeAction] = now;
        if (loraTimeAction == 2) gameTimeLeftSeconds = loraResumeTime;   // snap la secunda primita inainte de resume
        applyTimerAction(loraTimeAction);            // aplicam imediat actiunea
        timeAlertAction  = loraTimeAction;
        timeAlertStart   = now;
        alertReturnState = (currentState == STATE_TIME_ALERT) ? alertReturnState : currentState;
        currentState = STATE_TIME_ALERT;
        needsDisplayUpdate = true;
        for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);
        tone(PIN_BUZZER, 1500, 600);
            }
    } else if (ev == LORA_EVT_CAPTURE) {
        if (unitTable[loraEvtUnit - 1].status != SEC_CAPTURED) {   // ignoram copia dubla
            applyCapture(loraEvtUnit - 1, loraEvtTeam);
            tone(PIN_BUZZER, 1800, 600);   // doar audio (fara LED/ecran)
            needsDisplayUpdate = true;
        }
    } else if (ev == LORA_EVT_NEUTRALIZE) {
        if (unitTable[loraEvtUnit - 1].status == SEC_CAPTURED) {    // ignoram copia dubla
            applyNeutralize(loraEvtUnit - 1, loraEvtTeam, loraEvtPoints);
            tone(PIN_BUZZER, 1800, 600);   // doar audio (fara LED/ecran)
            needsDisplayUpdate = true;
        }
    } else if (ev == LORA_EVT_RESPAWN) {
        uint8_t ti = loraEvtTeam - 1;
        if ((uint16_t)loraEvtPoints > unitTable[loraEvtUnit - 1].kills[ti]) {   // total nou > cunoscut -> aplicam delta (autocorectie); copia dubla e ignorata
            uint16_t delta = (uint16_t)loraEvtPoints - unitTable[loraEvtUnit - 1].kills[ti];
            unitTable[loraEvtUnit - 1].kills[ti] = (uint16_t)loraEvtPoints;
            appliedPenalties[ti] += (int32_t)delta * (int32_t)respawnPenaltyPoints;   // necplafonat; afisarea clampeaza la 0
            tone(PIN_BUZZER, 1500, 100);   // beep scurt
            needsDisplayUpdate = true;
        }
    } else if (ev == LORA_EVT_BOMB_PLANT) {
        if (unitTable[loraEvtUnit - 1].status != BOMB_ARMED) {   // ignoram copia dubla
            unitTable[loraEvtUnit - 1].mode       = 2;
            unitTable[loraEvtUnit - 1].status     = BOMB_ARMED;
            unitTable[loraEvtUnit - 1].team       = (Team)loraEvtTeam;
            unitTable[loraEvtUnit - 1].actionTime = now;
            tone(PIN_BUZZER, 1800, 600);
            needsDisplayUpdate = true;
        }
    } else if (ev == LORA_EVT_BOMB_DEFUSE) {
        if (unitTable[loraEvtUnit - 1].status == BOMB_ARMED) {   // ignoram copia dubla / cursa cu explozia
            unitTable[loraEvtUnit - 1].status     = BOMB_COOLDOWN;
            unitTable[loraEvtUnit - 1].team       = TEAM_NEUTRAL;
            unitTable[loraEvtUnit - 1].actionTime = now;            // start cooldown
            if (loraEvtTeam >= 1 && loraEvtTeam <= 4)
                unitTable[loraEvtUnit - 1].savedPoints[loraEvtTeam - 1] += (int32_t)bombPointsDefuse;
            tone(PIN_BUZZER, 1800, 600);
            needsDisplayUpdate = true;
        }
    } else if (ev == LORA_EVT_KILLRESET) {
        if (lastKillResetAt == 0 || now - lastKillResetAt >= 12000) {   // dedup: ignoram copia a doua
            lastKillResetAt = now;
            applyKillsReset();
            if (loraEvtTeam >= 1 && loraEvtTeam <= 4 && loraEvtPoints > 0)
                unitTable[loraEvtUnit - 1].savedPoints[loraEvtTeam - 1] += (int32_t)loraEvtPoints;
            tone(PIN_BUZZER, 1500, 200);
            needsDisplayUpdate = true;
        }
    }
        }

    // Refresh registre display — previne "drift"-ul de imagine
    if (now - lastDisplayRefresh >= 3000) {
        lastDisplayRefresh = now;
        displayRefreshRegisters();
    }

    // Citire baterie la fiecare 10 secunde
    static uint32_t lastBatteryCheck = 0;
    if (now - lastBatteryCheck >= 10000 || lastBatteryCheck == 0) {
        lastBatteryCheck = now;
        updateBattery();
    }

    // Auto-dim la 10% + auto-return la pagina 1 (dupa 30s inactivitate)
    if (!isDimmed && (now - lastActivityTime >= 30000)) {
        isDimmed = true;
        setBrightness(10);
        if (currentState == STATE_PAGES && currentPage > 0) {
            currentPage = 0;
            needsDisplayUpdate = true;
        }
    }

    // Power latch pulse non-blocking (power-off)
    if (latchPulsing && millis() - latchLowStart >= 100) {
        latchPulsing = false;
        digitalWrite(PIN_LATCH, HIGH);
    }

    // Citire RFID — card Admin + carduri de puncte
    if ((currentState == STATE_PAGES || currentState == STATE_MENU || currentState == STATE_RESPAWN_SETUP)
            && (millis() - lastRfidRead >= 100) && millis() >= rfidIgnoreUntil) {
        RfidReadData rfid = rfidReadTag();
        if (rfid.result == RFID_READ_ADMIN) {
            tone(PIN_BUZZER, 2000, 200);
            previousStateBeforeAdmin = currentState;
            adminMenuIndex = 0;
            adminScrollIndex = 0;
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
            rfidIgnoreUntil = millis() + 2000;
            resetActivity();
            Serial.println("[RFID] Admin tag detectat!");
        } else if (rfid.result == RFID_READ_POINTS && selectedMode != -1 && !isTimeOut && !isGamePaused) {
            // Echipa proprietara = exact echipa al carei LED e aprins
            Team owner = TEAM_NEUTRAL;
            if (selectedMode == 0 && myRow().status == SEC_CAPTURED) owner = myRow().team;
            else if (selectedMode == 1 && myRow().status == BOMB_ARMED) owner = myRow().team;
            else if (selectedMode == 2) owner = myRow().team;

            if (owner == TEAM_NEUTRAL) {
                // Unitatea nu apartine niciunei echipe -> nu ardem cardul
                tone(PIN_BUZZER, 300, 400);
                rfidIgnoreUntil = millis() + 1500;
                Serial.println("[RFID] Unitate fara proprietar. Card intact.");
            } else if (rfidBurnTag()) {
                // Cardul s-a ars cu succes -> acordam punctele echipei
                myRow().savedPoints[owner - 1] += (int32_t)rfid.points;
                bonusPoints = rfid.points;
                bonusTeam = owner;
                bonusScreenStart = millis();
                currentState = STATE_BONUS_SCREEN;
                needsDisplayUpdate = true;
                rfidIgnoreUntil = millis() + 2000;
                tone(PIN_BUZZER, 1200, 200);
                Serial.print("[RFID] +");
                Serial.print(rfid.points);
                Serial.print(" pts -> ");
                Serial.println(TEAM_NAMES[owner - 1]);
                // Faza 2 (LoRa): trimitem alerta ca alte unitati sa-si actualizeze tabelul
            } else {
                // Ardere esuata -> NU acordam puncte
                tone(PIN_BUZZER, 200, 300);
                Serial.println("[RFID] EROARE la ardere! Punctele NU se acorda.");
            }
        }
        lastRfidRead = millis();
    }

    // Timer joc + timeout
    if (isGameTimerRunning && !isGamePaused && (now - lastTimerTick > 2000))
        lastTimerTick = now - 1000;   // clamp anti-drain (tick invechit)
        if (isGameTimerRunning && !isGamePaused && !isTimeOut && gameTimeLeftSeconds > 0 &&
            currentWinCondition != WIN_BY_CONQUEST) {
            if (now - lastTimerTick >= 1000) {
                lastTimerTick += 1000;
                gameTimeLeftSeconds--;
                if (gameTimeLeftSeconds == 0) {
                    isGameTimerRunning = false;
                    isTimeOut = true;
                    gameOverTime = now;
                    digitalWrite(PIN_RELAY, LOW);
                    isRelayActive = true;
                    relayTurnOffTime = now + 20000;
                    currentPage = 0;
                    needsDisplayUpdate = true;
                    Serial.println("[GAME] TIME OUT!");
                }
            }
        }

        // Conquest — o echipa a cucerit TOATE sectoarele -> game over
        if (isGameTimerRunning && !isTimeOut && !isGamePaused &&
            (currentWinCondition == WIN_BY_CONQUEST || currentWinCondition == WIN_BY_ANY)) {
        Team w = checkConquest();
        if (w != TEAM_NEUTRAL) {
            conquestWinner = w;
            isTimeOut = true;
            isGameTimerRunning = false;
            gameOverTime = now;
            digitalWrite(PIN_RELAY, LOW);
            isRelayActive = true;
            relayTurnOffTime = now + 20000;
            currentPage = 0;
            needsDisplayUpdate = true;
            Serial.print("[GAME] CONQUEST WINNER: ");
            Serial.println(TEAM_NAMES[w - 1]);
        }
    }

    // Sector — puncte LIVE per unitate cucerita (3 + bonus la fiecare 10s)
    if (!isTimeOut && !isGamePaused) {
        for (uint8_t u = 0; u < MAX_UNITS; u++) {
            if (unitTable[u].mode == 1 && unitTable[u].status == SEC_CAPTURED && unitTable[u].team != TEAM_NEUTRAL) {
                if (lastPointTick[u] == 0 || lastPointTick[u] > now) lastPointTick[u] = now;   // clamp: evita underflow
                if (now - lastPointTick[u] >= 10000) {
                    // +5s (jumatate de tick): pragul de bonus (15min) cade fix pe o granita de tick;
                    // un decalaj de ~1ms intre actionTime si now facea minutesHeld sa fie 14 in loc de 15
                    // pe unele unitati -> pierdeau o treapta de bonus. Evaluam la mijlocul intervalului.
                    uint32_t heldMs = (now > unitTable[u].actionTime) ? (now - unitTable[u].actionTime) : 0;
                    uint32_t minutesHeld = (heldMs + 5000) / 60000;
                    uint32_t bonus = (bonusIntervalMinutes > 0) ? (minutesHeld / bonusIntervalMinutes) : 0;
                    if (bonus > 3) bonus = 3;
                    int32_t gain = 3 + bonus;
                    unitTable[u].savedPoints[unitTable[u].team - 1] += gain;   // comitem in timp real
                    liveCapture[u] += gain;                                    // estimare cucerire curenta
                    lastPointTick[u] += 10000;
                }
            }
        }
    }

    // ============================================================
    // DIAG SCOR (temporar) — dump pe serial la 60s. Pune cele 3 unitati pe
    // serial, lasa-le ~20 min, compara liniile "src Ux" intre unitati.
    // ============================================================
    if (lastDiagPrint == 0 || now - lastDiagPrint >= 60000) {
        lastDiagPrint = now;
        Serial.println();
        Serial.print("=== DIAG U"); Serial.print(UNIT_ID);
        Serial.print(" fw="); Serial.print(FW_VERSION);
        Serial.print(" build="); Serial.print(__DATE__); Serial.print(" "); Serial.print(__TIME__);
        Serial.print(" bonusInt="); Serial.print(bonusIntervalMinutes);
        Serial.print(" now="); Serial.println(now);
        for (uint8_t u = 0; u < MAX_UNITS; u++) {
            if (unitTable[u].mode == 1 && unitTable[u].status == SEC_CAPTURED && unitTable[u].team != TEAM_NEUTRAL) {
                uint32_t held = (now > unitTable[u].actionTime) ? (now - unitTable[u].actionTime) : 0;
                uint32_t mh = (held + 5000) / 60000;
                uint32_t bn = (bonusIntervalMinutes > 0) ? (mh / bonusIntervalMinutes) : 0;
                if (bn > 3) bn = 3;
                Serial.print("  src U"); Serial.print(u + 1);
                Serial.print(" team="); Serial.print((int)unitTable[u].team);
                Serial.print(" pts="); Serial.print(unitTable[u].savedPoints[unitTable[u].team - 1]);
                Serial.print(" actT="); Serial.print(unitTable[u].actionTime);
                Serial.print(" lpt="); Serial.print(lastPointTick[u]);
                Serial.print(" held="); Serial.print(held);
                Serial.print(" min="); Serial.print(mh);
                Serial.print(" bonus="); Serial.println(bn);
            }
        }
    }

    // Timer bomba + beep accelerat + explozie
    if (selectedMode == 1 && myRow().status == BOMB_ARMED && !isGamePaused && !isTimeOut) {
        uint32_t elapsed = now - myRow().actionTime;
        uint32_t remaining = (elapsed < bombTimerMs) ? (bombTimerMs - elapsed) : 0;

        if (elapsed >= bombTimerMs) {
            // EXPLOZIE
            Team explodedBy = myRow().team;
            myRow().status     = BOMB_COOLDOWN;
            myRow().team       = TEAM_NEUTRAL;
            myRow().actionTime = now;                 // start cooldown
            if (explodedBy != TEAM_NEUTRAL)
                myRow().savedPoints[explodedBy - 1] += (int32_t)bombPointsExplode;
            isBombBeeping = false;
            digitalWrite(PIN_RELAY, LOW);
            isRelayActive = true;
            relayTurnOffTime = now + 10000;
            noTone(PIN_BUZZER);
            currentState = STATE_BOOM;
            needsDisplayUpdate = true;
            refreshLEDs();
            Serial.println("[BOMB] EXPLOZIE!");
        } else {
            uint32_t interval = 6000;
            if (remaining <= 3000)       interval = 100;
            else if (remaining <= 6000)  interval = 300;
            else if (remaining <= 10000) interval = 500;
            else if (remaining <= 20000) interval = 1000;
            else if (remaining <= 30000) interval = 3000;

            bool shouldBeep = (elapsed % interval) < 200;
            if (remaining <= 3000) shouldBeep = true;

            if (shouldBeep && !isBombBeeping) {
                isBombBeeping = true;
                tone(PIN_BUZZER, 1500);
                for (uint8_t i = 0; i < 4; i++)
                    if ((Team)(i + 1) != myRow().team) digitalWrite(PIN_LEDS[i], HIGH);
            } else if (!shouldBeep && isBombBeeping) {
                isBombBeeping = false;
                noTone(PIN_BUZZER);
                refreshLEDs();
            }
        }
    }

    // Cooldown terminat
    if (selectedMode == 1 && myRow().status == BOMB_COOLDOWN && !isGamePaused && !isTimeOut) {
        if (now - myRow().actionTime >= cooldownMs) {
            myRow().status = BOMB_IDLE;
            Serial.println("[BOMB] Cooldown terminat.");
        }
    }

    // Bombe de pe ALTE unitati: explozie + cooldown autonom (doar tabel + scor, fara efecte locale)
    if (!isGamePaused && !isTimeOut) {
        for (uint8_t u = 0; u < MAX_UNITS; u++) {
            if (u == UNIT_ID - 1 || unitTable[u].mode != 2) continue;
            if (unitTable[u].status == BOMB_ARMED) {
                if (unitTable[u].actionTime != 0 && now >= unitTable[u].actionTime &&
                    now - unitTable[u].actionTime >= bombTimerMs) {     // EXPLOZIE autonoma
                        Team by = unitTable[u].team;
                        unitTable[u].status     = BOMB_COOLDOWN;
                        unitTable[u].team       = TEAM_NEUTRAL;
                        unitTable[u].actionTime = now;                       // start cooldown
                        if (by != TEAM_NEUTRAL) unitTable[u].savedPoints[by - 1] += (int32_t)bombPointsExplode;
                        needsDisplayUpdate = true;
                    }
            } else if (unitTable[u].status == BOMB_COOLDOWN) {
                if (unitTable[u].actionTime != 0 && now >= unitTable[u].actionTime &&
                    now - unitTable[u].actionTime >= cooldownMs) {
                    unitTable[u].status = BOMB_IDLE;
                needsDisplayUpdate = true;
                    }
            }
        }
    }

    // Respawn — iesire din coada + animatie flash
    if (selectedMode == 2 && queueCount > 0 && !isGamePaused && !isTimeOut) {
        if (now >= respawnQueue[queueHead]) {
            queueCount--;
            queueHead = (queueHead + 1) % 100;
            flashStep = 1;
            flashStepStart = now;
            needsDisplayUpdate = true;
            Serial.println("[RESPAWN] Jucator iesit din coada!");
        }
    }
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

    // Releu — oprire non-blocking
    if (isRelayActive && millis() >= relayTurnOffTime) {
        digitalWrite(PIN_RELAY, HIGH);
        isRelayActive = false;
    }

    switch (currentState) {
        case STATE_BOOT:
            if (handleBoot()) {
                currentState = STATE_MENU;
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

        case STATE_RESPAWN_SETUP:
            if (needsDisplayUpdate) {
                drawRespawnSetup();
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
                tone(PIN_BUZZER, 1500, 500);
            }
            break;
        }

        case STATE_READY: {
            if (needsDisplayUpdate) {
                drawReadyScreen(selectedMode);
                needsDisplayUpdate = false;
            }
            if (millis() - readyScreenStart >= 2000) {
                currentState = STATE_PAGES;
                needsDisplayUpdate = true;
                refreshLEDs();
            }
            break;
        }

        case STATE_PAGES: {
            static uint32_t lastRefresh = 0;
            uint32_t refreshInterval = 200;
            // pe pagina 1, mod Bomb amorsata, vom avea nevoie de refresh rapid (Pasul Bomb)
            if (selectedMode == 1 && myRow().status == BOMB_ARMED && currentPage == 0) refreshInterval = 50;
            if (needsDisplayUpdate || (millis() - lastRefresh >= refreshInterval)) {
                buildContext(ctx, currentPage, batteryPercent, page4ScrollIndex, page5ScrollIndex);
                drawPages(ctx);
                needsDisplayUpdate = false;
                lastRefresh = millis();
            }
            handleButtons();
            break;
        }

        case STATE_ACTION:
            handleActionProgress();
            break;

        case STATE_SUCCESS:
            if (millis() - successStartTime >= 2000) {
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
                refreshLEDs();
            }
            break;

        case STATE_BOOM:
            drawBoomScreen();
            if (millis() - myRow().actionTime >= 3000) {
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
                refreshLEDs();
            }
            break;

        case STATE_WAIT_ADMIN_TAG: {
            if (needsDisplayUpdate) {
                drawWaitAdminTag();
                needsDisplayUpdate = false;
            }
            // Timeout 3 secunde fara card -> revenim pe pagina 6 + eroare
            if (millis() - waitAdminTagStart >= 3000) {
                currentState = STATE_PAGES;
                currentPage = 5;
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 200, 300);
                break;
            }
            // Citim cardul admin pentru confirmare
            if (millis() - lastRfidRead >= 100) {
                RfidReadData rfid = rfidReadTag();
                lastRfidRead = millis();
                if (rfid.result == RFID_READ_ADMIN) {
                    // feedback identic cu receptoarele; transmitem in fundal, aplicam dupa AUX LOW
                    loraSendTime(PKT_TIME_START + pendingAdminAction, (uint16_t)gameTimeLeftSeconds);
                    emitterPendingAction = pendingAdminAction;
                    emitterApplyArmed    = true;
                    timeAlertAction  = pendingAdminAction;
                    timeAlertStart   = millis();
                    alertReturnState = STATE_PAGES;
                    currentPage      = 5;
                    currentState     = STATE_TIME_ALERT;
                    for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);
                    tone(PIN_BUZZER, 1500, 600);
                    needsDisplayUpdate = true;
                    rfidIgnoreUntil = millis() + 2000;
                } else if (rfid.result == RFID_READ_POINTS || rfid.result == RFID_READ_INVALID) {
                    tone(PIN_BUZZER, 200, 300);   // card gresit
                }
            }
            break;
        }

        case STATE_KILL_RESET_ADMIN: {
            if (needsDisplayUpdate) {
                drawKillResetAdminScreen();
                needsDisplayUpdate = false;
            }
            if (millis() - killResetAdminStart >= 3000) {
                currentState = STATE_PAGES;
                currentPage = 2;
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 200, 300);
                break;
            }
            if (millis() - lastRfidRead >= 100) {
                RfidReadData rfid = rfidReadTag();
                lastRfidRead = millis();
                if (rfid.result == RFID_READ_ADMIN) {
                    tone(PIN_BUZZER, 1500, 200);
                    rfidIgnoreUntil = millis() + 2000;
                    uint8_t teamsWithLimit = 0;
                    for (uint8_t i = 0; i < 4; i++)
                        if (teamMaxRespawns[i] > 0) teamsWithLimit++;
                        if (teamsWithLimit >= 2) {
                            currentState = STATE_KILL_RESET_CONFIRM;   // intrebam DA/NU
                        } else {
                            applyKillsReset();                          // reset direct
                            loraSendKillReset(0, 0);                    // anuntam reteaua (fara puncte)
                            currentPage = 2;
                            killResetHasPoints = false;
                            killResetDoneStart = millis();
                            currentState = STATE_KILL_RESET_DONE;
                        }
                        needsDisplayUpdate = true;
                }
            }
            break;
        }

        case STATE_KILL_RESET_CONFIRM:
            if (needsDisplayUpdate) {
                drawKillResetConfirmScreen();
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_KILL_RESET_WINNER:
            if (needsDisplayUpdate) {
                drawKillResetWinnerScreen();
                needsDisplayUpdate = false;
                for (uint8_t i = 0; i < 4; i++)
                    digitalWrite(PIN_LEDS[i], teamMaxRespawns[i] > 0 ? HIGH : LOW);
            }
            handleButtons();
            break;

        case STATE_KILL_RESET_DONE:
            drawKillResetDoneScreen(killResetPoints, killResetWinnerTeam, killResetHasPoints);
            if (millis() - killResetDoneStart >= 2000) {
                refreshLEDs();
                currentState = STATE_PAGES;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_BONUS_SCREEN:
            drawBonusScreen(bonusPoints, bonusTeam);
            if (millis() - bonusScreenStart >= 2000) {
                refreshLEDs();
                currentState = STATE_PAGES;
                currentPage = 0;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_SYNC_WARNING:
            if (needsDisplayUpdate) { drawSyncWarningScreen(); needsDisplayUpdate = false; }
            handleButtons();
            break;

        case STATE_SYNCING:
            drawSyncingScreen();
            loraSendSyncBlocking();
            tone(PIN_BUZZER, 1500, 300);
            for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], HIGH);   // toate LED-urile pe durata DONE
            syncDoneStart = millis();
        currentState = STATE_SYNC_DONE;
            needsDisplayUpdate = true;
            break;

        case STATE_SYNC_DONE:
            if (needsDisplayUpdate) { drawSyncDoneScreen(); needsDisplayUpdate = false; }
            if (millis() - syncDoneStart >= 2000) {
                refreshLEDs();                    // restauram starea corecta a LED-urilor
                currentState = syncReturnState;   // mereu inapoi de unde am venit (admin)
                needsDisplayUpdate = true;
            }
            break;

        case STATE_TIME_ALERT:
            if (needsDisplayUpdate) { drawTimeAlertScreen(timeAlertAction); needsDisplayUpdate = false; }
            if (millis() - timeAlertStart >= 2000) {
                refreshLEDs();
                currentState = alertReturnState;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_ADMIN_BLOCKED:
            if (needsDisplayUpdate) { drawBlockedScreen(); needsDisplayUpdate = false; }
            if (millis() - blockMsgStart >= 2000) {
                currentState = blockReturnState;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_SYNCED:
            if (needsDisplayUpdate) { drawSyncedScreen(syncedByUnit); needsDisplayUpdate = false; }
            if (millis() - syncedScreenStart >= 2000) {
                refreshLEDs();
                currentState = syncReturnState;   // mereu inapoi de unde am venit
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

        case STATE_ADMIN_PAGES:
            if (needsDisplayUpdate) {
                AdminContext ac = buildAdminContext();
                drawAdminPages(ac);
                needsDisplayUpdate = false;
            }
            handleButtons();
            break;

        case STATE_ADMIN_SAVED:
            drawAdminSaved();
            if (millis() - adminSavedTime >= 2000) {
                currentState = STATE_ADMIN_MENU;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_POWER_OFF:
            if (needsDisplayUpdate) {
                drawPowerOffScreen();
                needsDisplayUpdate = false;
            }
            // Dupa 2 secunde pulsam latch-ul (taie alimentarea daca intrerupatorul e ON)
            if (!powerOffPulsed && millis() - powerOffStart >= 2000) {
                powerOffPulsed = true;
                digitalWrite(PIN_LATCH, LOW);
                latchLowStart = millis();
                latchPulsing = true;
            }
            // Daca pulsul s-a terminat si suntem inca aici, revenim in admin
            if (powerOffPulsed && !latchPulsing) {
                powerOffPulsed = false;
                currentState = STATE_ADMIN_MENU;
                needsDisplayUpdate = true;
            }
            break;

        case STATE_ADMIN_TAG_WRITE: {
            // Timeout 3 secunde fara card
            if (tagStatusMsg == 0 && millis() - tagWaitStart >= 3000) {
                tagStatusMsg = 6;
                tagStatusTime = millis();
                tone(PIN_BUZZER, 200, 300);
            }

            drawTagWriter(tagStatusMsg);

            // Dupa 2 secunde pe ecranul de rezultat, revenim la Tag Writer
            if (tagStatusMsg != 0 && millis() - tagStatusTime >= 2000) {
                currentState = STATE_ADMIN_PAGES;
                tagStatusMsg = 0;
                needsDisplayUpdate = true;
                break;
            }

            // Cat timp asteptam card, incercam sa scriem
            if (tagStatusMsg == 0) {
                const uint16_t ptsOpt[] = {50, 100, 150, 200, 250, 500, 750, 1000, 1500, 2000, 0};
                uint8_t type = (twOptionIdx == 10) ? 2 : 1;
                uint16_t points = ptsOpt[twOptionIdx];

                RfidWriteResult r = rfidWriteTag(type, points);
                if (r == RFID_TIMEOUT) break;  // niciun card inca

                switch (r) {
                    case RFID_OK_NEW:       tagStatusMsg = 1; tone(PIN_BUZZER, 1500, 500); break;
                    case RFID_OK_OVERWRITE: tagStatusMsg = 2; tone(PIN_BUZZER, 1500, 500); break;
                    case RFID_OK_ADMIN:     tagStatusMsg = 3; tone(PIN_BUZZER, 1500, 500); break;
                    case RFID_OK_REVOKED:   tagStatusMsg = 4; tone(PIN_BUZZER, 1500, 500); break;
                    case RFID_DENIED:       tagStatusMsg = 5; tone(PIN_BUZZER, 200, 400);  break;
                    default:                tagStatusMsg = 7; tone(PIN_BUZZER, 200, 300);  break;
                }
                tagStatusTime = millis();
            }
            handleButtons();
            break;
        }

        default:
            break;
    }
}

// ============================================================
// CALLBACKS BUTOANE
// ============================================================

void onShortPress(uint8_t btnIndex) {
    tone(PIN_BUZZER, 800, 30);
    resetActivity();

    if (currentState == STATE_MENU) {
        if (btnIndex == 2) {            // VERDE — scroll
            menuIndex++;
            if (menuIndex > 2) menuIndex = 0;
            needsDisplayUpdate = true;
        } else if (btnIndex == 3) {     // GALBEN — confirm
            selectedMode = menuIndex;
            myRow().mode = (selectedMode == 0) ? 1 : (selectedMode == 1) ? 2 : 3;
            if (selectedMode == 2) {
                currentState = STATE_RESPAWN_SETUP;
            } else {
                loraSendMode(myRow().mode, 0);   // anuntam reteaua (sector/bomba)
                currentState = STATE_LOADING;
                loadingStartTime = millis();
            }
            needsDisplayUpdate = true;
            Serial.print("[MENU] Mod selectat: ");
            Serial.println(selectedMode);
            // (alerta LoRa EVT_MODE_* se adauga in Faza 2)
        }

    } else if (currentState == STATE_RESPAWN_SETUP) {
        myRow().mode = 3;
        myRow().team = (Team)(btnIndex + 1);
        loraSendMode(3, (uint8_t)myRow().team);   // anuntam reteaua (respawn + echipa)
        currentState = STATE_LOADING;
        loadingStartTime = millis();
        refreshLEDs();
        needsDisplayUpdate = true;
        Serial.print("[RESPAWN] Echipa selectata: ");
        Serial.println(TEAM_NAMES[btnIndex]);

    } else if (currentState == STATE_PAGES) {
        if (btnIndex == 0) {            // ROSU — pagina anterioara
            currentPage = (currentPage == 0) ? 5 : currentPage - 1;
            needsDisplayUpdate = true;
        } else if (btnIndex == 1) {     // ALBASTRU — pagina urmatoare
            currentPage = (currentPage >= 5) ? 0 : currentPage + 1;
            needsDisplayUpdate = true;
        } else if (btnIndex == 2) {     // VERDE — scroll pe paginile 4 si 5
            if (currentPage == 2 && !isTimeOut) {
                // PAGE 3 — VERDE: reset kill-uri (blocat cat timp jocul ruleaza)
                if (isGameTimerRunning && !isGamePaused) {
                    blockReturnState = STATE_PAGES;
                    blockMsgStart = millis();
                    currentState = STATE_ADMIN_BLOCKED;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 300, 200);
                } else {
                    killResetAdminStart = millis();
                    currentState = STATE_KILL_RESET_ADMIN;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1000, 100);
                }
            } else if (currentPage == 3) {
                page4ScrollIndex++;
                needsDisplayUpdate = true;
            } else if (currentPage == 4) {
                page5ScrollIndex++;
                needsDisplayUpdate = true;
            } else if (currentPage == 5 && gameTimeLimitSeconds > 0 && !isTimeOut) {
                // PAGE 6 — VERDE: reset ceas (blocat cat timp jocul ruleaza)
                if (isGameTimerRunning && !isGamePaused) {
                    blockReturnState = STATE_PAGES;
                    blockMsgStart = millis();
                    currentState = STATE_ADMIN_BLOCKED;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 300, 200);
                } else {
                    pendingAdminAction = 3;
                    waitAdminTagStart = millis();
                    currentState = STATE_WAIT_ADMIN_TAG;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1000, 100);
                }
            }
            // pag 3 (kill reset) -> pasii urmatori
        } else if (btnIndex == 3) {     // GALBEN
            if (selectedMode == 2 && currentPage != 5) {
                // RESPAWN — inrolare jucator in coada (un kill / respawn)
                Team t = myRow().team;
                if (t != TEAM_NEUTRAL) {
                    uint8_t ti = t - 1;
                    if (isGamePaused) { tone(PIN_BUZZER, 200, 200); return; }
                    bool limitReached = (teamMaxRespawns[ti] > 0 && teamKillTotal(ti) >= teamMaxRespawns[ti]);
                    if (!isTimeOut && !limitReached && queueCount < 100 &&
                        isGameTimerRunning) {
                        respawnQueue[queueTail] = millis() + respawnTimeMs;
                        queueTail = (queueTail + 1) % 100;
                        queueCount++;
                        myRow().kills[ti]++;
                        appliedPenalties[ti] += respawnPenaltyPoints;   // necplafonat; afisarea clampeaza la 0
                        if (respawnWindowStart == 0) respawnWindowStart = millis();   // primul kill -> deschidem fereastra de 15s
                        needsDisplayUpdate = true;
                        Serial.print("[RESPAWN] Kill inregistrat. Queue: ");
                        Serial.println(queueCount);
                        if (teamMaxRespawns[ti] > 0 && teamKillTotal(ti) >= teamMaxRespawns[ti]) {
                            digitalWrite(PIN_RELAY, LOW);
                            isRelayActive = true;
                            relayTurnOffTime = millis() + 3000;
                            tone(PIN_BUZZER, 1800, 600);
                            Serial.println("[RESPAWN] LIMIT REACHED!");
                        } else {
                            tone(PIN_BUZZER, 1200, 100);
                        }
                        } else if (!isGameTimerRunning) {
                            tone(PIN_BUZZER, 200, 200);
                    } else if (limitReached) {
                        tone(PIN_BUZZER, 200, 300);
                    }
                }
            } else if (currentPage == 5 && !isTimeOut) {
                // PAGE 6 — START / PAUSE / RESUME (confirmare prin admin tag 3s)
                bool act = true;
                if (isGamePaused) {
                    pendingAdminAction = 2;        // resume
                } else if (selectedMode != -1 && !isGameTimerRunning) {
                    pendingAdminAction = 0;        // start
                } else if (selectedMode != -1 && isGameTimerRunning) {
                    pendingAdminAction = 1;        // pause
                } else {
                    act = false;
                }
                if (act) {
                    waitAdminTagStart = millis();
                    currentState = STATE_WAIT_ADMIN_TAG;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1000, 100);
                }
            }
        }
    }

    else if (currentState == STATE_KILL_RESET_CONFIRM) {
        if (btnIndex == 0) {            // RED — Nu, reset fara puncte
            applyKillsReset();
            loraSendKillReset(0, 0);    // anuntam reteaua (fara puncte)
            currentPage = 2;
            killResetHasPoints = false;
            killResetDoneStart = millis();
            currentState = STATE_KILL_RESET_DONE;
            tone(PIN_BUZZER, 1500, 300);
            needsDisplayUpdate = true;
        } else if (btnIndex == 1) {     // BLUE — Da, selectam castigatorul
            currentState = STATE_KILL_RESET_WINNER;
            needsDisplayUpdate = true;
        }
    }

    else if (currentState == STATE_KILL_RESET_WINNER) {
        // Doar echipele cu limita de kill-uri pot fi selectate
        if (btnIndex < 4 && teamMaxRespawns[btnIndex] > 0) {
            killResetWinnerTeam = btnIndex;
            uint16_t pts = 0;
            if (teamKillTotal(btnIndex) < teamMaxRespawns[btnIndex])
                pts = (teamMaxRespawns[btnIndex] - teamKillTotal(btnIndex)) * 10;
            killResetPoints = pts;
            killResetHasPoints = true;
            if (pts > 0) myRow().savedPoints[btnIndex] += (int32_t)pts;
            applyKillsReset();
            loraSendKillReset((uint8_t)(btnIndex + 1), pts);   // anuntam reteaua (cu puncte)
            currentPage = 2;
            killResetDoneStart = millis();
            currentState = STATE_KILL_RESET_DONE;
            needsDisplayUpdate = true;
            tone(PIN_BUZZER, 1500, 300);
            Serial.print("[KILLS] Winner: ");
            Serial.print(TEAM_NAMES[btnIndex]);
            Serial.print(" +");
            Serial.println(pts);
        }
    }

    else if (currentState == STATE_SYNC_WARNING) {
        if (btnIndex == 0) {            // RED — inapoi in meniul admin
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
        } else if (btnIndex == 1) {     // BLUE — confirma sincronizarea
            currentState = STATE_SYNCING;
            needsDisplayUpdate = true;
        }
    }

    else if (currentState == STATE_ADMIN_MENU) {
        if (btnIndex == 2) {            // VERDE — scroll jos
            adminMenuIndex++;
            if (adminMenuIndex == 5 && selectedMode == -1) adminMenuIndex++;
            if (adminMenuIndex >= 8) {
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

        } else if (btnIndex == 3) {     // GALBEN — confirmare
            bool gameActive = isGameTimerRunning && !isGamePaused && !isTimeOut;
            blockReturnState = STATE_ADMIN_MENU;
            if (adminMenuIndex == 3) {
                // SYNC UNITS — blocat cat timp jocul ruleaza si timpul se scurge
                if (gameActive) {
                    blockMsgStart = millis();
                    currentState = STATE_ADMIN_BLOCKED;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 300, 200);
                } else {
                    syncReturnState = currentState;   // = STATE_ADMIN_MENU
                    currentState = STATE_SYNC_WARNING;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1000, 50);
                }
            } else if (adminMenuIndex == 5) {
                // CHANGE MODE — blocat daca jocul ruleaza / sector cucerit / bomba armata / queue respawn
                bool changeBlocked = gameActive
                || (myRow().mode == 1 && myRow().status == SEC_CAPTURED)
                || (myRow().mode == 2 && myRow().status == BOMB_ARMED)
                || (queueCount > 0);
                if (changeBlocked) {
                    blockMsgStart = millis();
                    currentState = STATE_ADMIN_BLOCKED;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 300, 200);
                } else {
                    // intai flush fereastra de kill-uri (daca e deschisa), apoi anuntam change mode
                    if (respawnWindowStart != 0 && myRow().team != TEAM_NEUTRAL)
                        loraSendRespawn((uint8_t)myRow().team, myRow().kills[myRow().team - 1]);
                    respawnWindowStart = 0;
                    selectedMode = -1;
                    loraSendMode(0, 0);   // anuntam reteaua: unitatea devine UNKNOWN
                    // Pastram punctele si kill-urile; resetam doar starea de mod
                    myRow().mode       = 0;
                    myRow().status     = 0;
                    myRow().team       = TEAM_NEUTRAL;
                    myRow().actionTime = 0;
                    liveCapture[UNIT_ID - 1] = 0;
                    lastPointTick[UNIT_ID - 1] = 0;
                    queueCount = queueHead = queueTail = 0;
                    currentState = STATE_MENU;
                    menuIndex = 0;
                    adminMenuIndex = 0;
                    adminScrollIndex = 0;
                    refreshLEDs();
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 1000, 300);
                }
            } else if (adminMenuIndex == 6) {
                // SYSTEM RESTART — anuntam reteaua, apoi reboot
                loraSendRestart();
                doReboot();
            } else if (adminMenuIndex == 7) {
                // POWER OFF
                currentState = STATE_POWER_OFF;
                powerOffStart = millis();
                powerOffPulsed = false;
                needsDisplayUpdate = true;
                tone(PIN_BUZZER, 1500, 300);
            } else {
                // Game Settings(0)/Bomb Parameters(1)/Respawn Rules(2) blocate cat timp jocul ruleaza; TAG Writer(4) liber
                if (gameActive && adminMenuIndex != 4) {
                    blockMsgStart = millis();
                    currentState = STATE_ADMIN_BLOCKED;
                    needsDisplayUpdate = true;
                    tone(PIN_BUZZER, 300, 200);
                } else {
                    if      (adminMenuIndex == 0) adminSelectedPage = 0;
                    else if (adminMenuIndex == 1) adminSelectedPage = 1;
                    else if (adminMenuIndex == 2) adminSelectedPage = 2;
                    else if (adminMenuIndex == 4) adminSelectedPage = 3;
                    syncAdminIndices();
                    currentState = STATE_ADMIN_PAGES;
                    needsDisplayUpdate = true;
                }
            }

        } else if (btnIndex == 0) {     // ROSU — inapoi la joc
            currentState = previousStateBeforeAdmin;
            needsDisplayUpdate = true;
        }

    } else if (currentState == STATE_ADMIN_PAGES) {
        if (btnIndex == 0) {            // ROSU — inapoi la meniu admin
            currentState = STATE_ADMIN_MENU;
            needsDisplayUpdate = true;
            return;
        }

        if (adminSelectedPage == 0) {           // GAME SETTINGS
            if (btnIndex == 2) {
                gsIndex = (gsIndex + 1) % 5;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if      (gsIndex == 0) gsWinCond   = (gsWinCond + 1) % 3;
                else if (gsIndex == 1) gsTimeLimit = (gsTimeLimit + 1) % 16;
                else if (gsIndex == 2) gsBonus     = (gsBonus + 1) % 7;
                else if (gsIndex == 3) gsActionIdx = (gsActionIdx + 1) % 4;
                else if (gsIndex == 4) {          // CONFIRM
                    const uint32_t tl[] = {0, 900, 1800, 3600, 7200, 10800, 14400, 18000, 21600, 25200, 28800, 32400, 36000, 39600, 43200, 86400};
                    const uint16_t bn[] = {0, 15, 30, 60, 120, 180, 240};
                    const uint32_t at[] = {5000, 10000, 15000, 20000};
                    gameTimeLimitSeconds = tl[gsTimeLimit];
                    // Aplicam la ceasul live doar daca jocul nu e in desfasurare
                    if (!isGameTimerRunning && !isGamePaused && !isTimeOut)
                        gameTimeLeftSeconds = gameTimeLimitSeconds;
                    bonusIntervalMinutes = bn[gsBonus];
                    currentWinCondition  = (WinCondition)gsWinCond;
                    actionTimeMs         = at[gsActionIdx];
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 1) {    // BOMB PARAMETERS
            if (btnIndex == 2) {
                bsIndex = (bsIndex + 1) % 5;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if      (bsIndex == 0) bsTimerIdx    = (bsTimerIdx + 1) % 8;
                else if (bsIndex == 1) bsCooldownIdx = (bsCooldownIdx + 1) % 8;
                else if (bsIndex == 2) bsExpPtsIdx   = (bsExpPtsIdx + 1) % 15;
                else if (bsIndex == 3) bsDefPtsIdx   = (bsDefPtsIdx + 1) % 15;
                else if (bsIndex == 4) {          // CONFIRM
                    const uint32_t tv[] = {5, 10, 15, 20, 30, 45, 60, 120};
                    const uint32_t pv[] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000, 2500, 3000};
                    bombTimerMs       = tv[bsTimerIdx] * 60000UL;
                    cooldownMs        = tv[bsCooldownIdx] * 60000UL;
                    bombPointsExplode = pv[bsExpPtsIdx];
                    bombPointsDefuse  = pv[bsDefPtsIdx];
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 2) {    // RESPAWN RULES
            if (btnIndex == 2) {
                rsIndex = (rsIndex + 1) % 7;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if      (rsIndex == 0) rsTimeIdx    = (rsTimeIdx + 1) % 12;
                else if (rsIndex == 1) rsPenaltyIdx = (rsPenaltyIdx + 1) % 7;
                else if (rsIndex >= 2 && rsIndex <= 5)
                    rsLimitIdx[rsIndex - 2] = (rsLimitIdx[rsIndex - 2] + 1) % 11;
                else if (rsIndex == 6) {          // CONFIRM
                    const uint32_t ts[] = {10, 30, 60, 120, 180, 240, 300, 600, 900, 1200, 1500, 1800};
                    const uint16_t pp[] = {0, 5, 10, 25, 50, 75, 100};
                    const uint16_t lm[] = {0, 10, 25, 50, 75, 100, 200, 300, 400, 500, 1000};
                    respawnTimeMs        = ts[rsTimeIdx] * 1000UL;
                    respawnPenaltyPoints = pp[rsPenaltyIdx];
                    for (uint8_t i = 0; i < 4; i++) teamMaxRespawns[i] = lm[rsLimitIdx[i]];
                    adminSavedTime = millis();
                    tone(PIN_BUZZER, 1500, 300);
                    currentState = STATE_ADMIN_SAVED;
                }
                needsDisplayUpdate = true;
            }

        } else if (adminSelectedPage == 3) {    // TAG WRITER
            if (btnIndex == 2) {
                twIndex = (twIndex + 1) % 2;
                needsDisplayUpdate = true;
            } else if (btnIndex == 3) {
                if (twIndex == 0) {
                    twOptionIdx = (twOptionIdx + 1) % 11;
                    needsDisplayUpdate = true;
                } else {
                    // WRITE — pornim scrierea pe card
                    tagStatusMsg = 0;
                    tagWaitStart = millis();
                    currentState = STATE_ADMIN_TAG_WRITE;
                    tone(PIN_BUZZER, 1000, 50);
                    needsDisplayUpdate = true;
                }
            }
        }
    }
}

void onLongPress(uint8_t btnIndex) {
    resetActivity();
    // Actiunile sunt permise doar pe pagina 1, in STATE_PAGES
    if (currentState != STATE_PAGES || isTimeOut) return;
    if (isGamePaused) { tone(PIN_BUZZER, 200, 200); return; }
    if (!isGameTimerRunning) { tone(PIN_BUZZER, 200, 200); return; }

    Team btnTeam = (Team)(btnIndex + 1);

    if (selectedMode == 0) {  // SECTOR
        if (myRow().status == SEC_NEUTRAL) {
            currentAction = ACT_CAPTURE;
        } else if (btnTeam == myRow().team) {
            tone(PIN_BUZZER, 200, 200);   // echipa proprie nu-si neutralizeaza sectorul
            return;
        } else {
            currentAction = ACT_NEUTRALIZE;
        }
        actingTeam = btnIndex;
        actionStartTime = millis();
        currentState = STATE_ACTION;
        needsDisplayUpdate = true;
        tone(PIN_BUZZER, 1000, 100);
        Serial.println(currentAction == ACT_CAPTURE ? "[SECTOR] Start CAPTURE" : "[SECTOR] Start NEUTRALIZE");

    } else if (selectedMode == 1) {  // BOMB
        if (myRow().status == BOMB_COOLDOWN) { tone(PIN_BUZZER, 200, 200); return; }
        if (myRow().status == BOMB_IDLE) {
            currentAction = ACT_ARM;
        } else {  // BOMB_ARMED
            if (btnTeam == myRow().team) { tone(PIN_BUZZER, 200, 200); return; }  // cine a plantat nu poate dezamorsa
            currentAction = ACT_DEFUSE;
        }
        actingTeam = btnIndex;
        actionStartTime = millis();
        currentState = STATE_ACTION;
        needsDisplayUpdate = true;
        tone(PIN_BUZZER, 1000, 100);
        Serial.println(currentAction == ACT_ARM ? "[BOMB] Start ARM" : "[BOMB] Start DEFUSE");
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
