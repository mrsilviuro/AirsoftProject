#include "game.h"
#include <string.h>

// ============================================================
// TABELUL + valori initiale (totul in RAM, zero flash)
// ============================================================
UnitRow unitTable[MAX_UNITS] = {};
UnitRow& myRow() { return unitTable[UNIT_ID - 1]; }

int32_t  liveCapture[MAX_UNITS]  = {0};
uint32_t lastPointTick[MAX_UNITS] = {0};

int8_t  selectedMode        = -1;
int32_t appliedPenalties[4] = {0, 0, 0, 0};

// Setari Bomba
uint32_t bombTimerMs       = 15 * 60000UL;
uint32_t cooldownMs        = 20 * 60000UL;
uint32_t bombPointsExplode = 600;
uint32_t bombPointsDefuse  = 300;

// Setari Respawn + coada
uint32_t respawnTimeMs        = 300000;
uint16_t respawnPenaltyPoints = 25;
uint16_t teamMaxRespawns[4]   = {0, 0, 0, 0};
uint8_t  queueCount = 0, queueHead = 0, queueTail = 0;
uint32_t respawnQueue[100]    = {0};

// Flux joc
uint32_t     gameTimeLeftSeconds = 0;
uint32_t     gameTimeLimitSeconds = 0;
bool         isGameTimerRunning  = false;
bool         isTimeOut           = false;
Team         conquestWinner      = TEAM_NEUTRAL;
WinCondition currentWinCondition = WIN_BY_POINTS;
uint32_t     bonusIntervalMinutes= 30;
uint32_t     actionTimeMs        = 15000;
bool         isGamePaused        = false;
uint32_t     gameOverTime        = 0;
uint32_t     pauseStartTime      = 0;

// Comunicatii
uint8_t  globalBattery[MAX_UNITS] = {0};
uint32_t lastSeenTime[MAX_UNITS]  = {0};

// ============================================================
// Scor prin INSUMARE peste tabel
// ============================================================
// Returneaza echipa care a cucerit TOATE sectoarele din tabel, sau TEAM_NEUTRAL
Team checkConquest() {
    Team owner = TEAM_NEUTRAL;
    bool anySector = false;
    for (uint8_t u = 0; u < MAX_UNITS; u++) {
        if (unitTable[u].mode == 1) {            // unitate sector
            anySector = true;
            if (unitTable[u].status != SEC_CAPTURED) return TEAM_NEUTRAL;
            if (owner == TEAM_NEUTRAL)      owner = unitTable[u].team;
            else if (owner != unitTable[u].team) return TEAM_NEUTRAL;
        }
    }
    return anySector ? owner : TEAM_NEUTRAL;
}

int32_t teamScore(uint8_t team) {
    int32_t total = 0;
    for (uint8_t u = 0; u < MAX_UNITS; u++) total += unitTable[u].savedPoints[team];
    return total;
}

uint16_t teamKillTotal(uint8_t team) {
    uint32_t total = 0;
    for (uint8_t u = 0; u < MAX_UNITS; u++) total += unitTable[u].kills[team];
    return (total > 65535) ? 65535 : (uint16_t)total;
}

// ============================================================
// buildContext() — totul din tabel
// ============================================================
void buildContext(PageContext& ctx, uint8_t currentPage, uint8_t batteryPercent,
                  uint8_t page4Scroll, uint8_t page5Scroll) {
    UnitRow& r = myRow();

    ctx.batteryPercent     = batteryPercent;
    ctx.currentPage        = currentPage;
    ctx.selectedMode       = selectedMode;
    ctx.isTimeOut          = isTimeOut;
    ctx.conquestWinner     = conquestWinner;
    ctx.winCondition       = currentWinCondition;
    ctx.isGameTimerRunning = isGameTimerRunning;
    ctx.gameTimeLeftSeconds= gameTimeLeftSeconds;
    ctx.isGamePaused       = isGamePaused;
    ctx.pauseStartTime     = pauseStartTime;
    ctx.gameOverTime       = gameOverTime;

    // Sector (din randul propriu)
    ctx.sectorOwner          = (r.mode == 1 && r.status == SEC_CAPTURED) ? r.team : TEAM_NEUTRAL;
    ctx.captureStartTime     = r.actionTime;
    ctx.currentCapturePoints = (uint32_t)liveCapture[UNIT_ID - 1];
    ctx.bonusIntervalMinutes = bonusIntervalMinutes;

    // Bomb (din randul propriu)
    ctx.isBombArmed      = (r.mode == 2 && r.status == BOMB_ARMED);
    ctx.isCooldownActive = (r.mode == 2 && r.status == BOMB_COOLDOWN);
    ctx.bombPlantTime    = r.actionTime;
    ctx.bombTimerMs      = bombTimerMs;
    ctx.cooldownStartTime= r.actionTime;
    ctx.cooldownMs       = cooldownMs;

    // Respawn (din randul propriu)
    ctx.respawnTeam          = (r.mode == 3) ? r.team : TEAM_NEUTRAL;
    ctx.queueCount           = queueCount;
    ctx.queueHead            = queueHead;
    ctx.respawnPenaltyPoints = respawnPenaltyPoints;
    ctx.respawnQueue[0]      = respawnQueue[queueHead];
    memcpy(ctx.teamMaxRespawns, teamMaxRespawns, sizeof(teamMaxRespawns));

    // Scoruri si kill-uri (INSUMARE peste tabel)
    for (uint8_t t = 0; t < 4; t++) {
        ctx.liveScore[t]        = teamScore(t);
        ctx.appliedPenalties[t] = appliedPenalties[t];
        ctx.teamKills[t]        = teamKillTotal(t);
    }
    memset(ctx.globalKills, 0, sizeof(ctx.globalKills));
    for (uint8_t t = 0; t < 4; t++) ctx.globalKills[0][t] = teamKillTotal(t);

    // Meta unitate locala
    uint8_t localBars = 0;
    if (batteryPercent >= 80)      localBars = 4;
    else if (batteryPercent >= 60) localBars = 3;
    else if (batteryPercent >= 40) localBars = 2;
    else if (batteryPercent >= 20) localBars = 1;
    globalBattery[UNIT_ID - 1] = localBars;
    // lastSeenTime[UNIT_ID-1] NU se mai seteaza aici: pe pag.5 arata acum timpul de la ultima TRANSMISIE
    // (paginile 4/5 trateaza unitatea locala separat prin i==UNIT_ID-1, deci ramane mereu vizibila/online)

    // Datele tuturor unitatilor pentru paginile 4/5 (din tabel)
    for (uint8_t u = 0; u < MAX_UNITS; u++) {
        UnitRow& ur = unitTable[u];
        ctx.globalUnitMode[u]  = ur.mode;
        ctx.globalEventTime[u] = ur.actionTime;
        ctx.globalBattery[u]   = globalBattery[u];
        ctx.lastSeenTime[u]    = lastSeenTime[u];
        // status -> Team (compatibil cu randarea paginilor de unitati)
        if (ur.mode == 1)
            ctx.globalUnitStatus[u] = (ur.status == SEC_CAPTURED) ? ur.team : TEAM_NEUTRAL;
        else if (ur.mode == 2)
            ctx.globalUnitStatus[u] = (ur.status == BOMB_ARMED) ? TEAM_PLANTED : TEAM_NEUTRAL;
        else if (ur.mode == 3)
            ctx.globalUnitStatus[u] = ur.team;
        else
            ctx.globalUnitStatus[u] = TEAM_NEUTRAL;
    }

    ctx.page4ScrollIndex = page4Scroll;
    ctx.page5ScrollIndex = page5Scroll;
}

// ============================================================
// Ecrane SYNC (LoRa)
// ============================================================
void drawSyncWarningScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    uint8_t x;
    const char* l1 = "--- WARNING ---";
    x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
    display.setCursor(x, 0); display.print(l1);
    const char* l2 = "All active units";
    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
    display.setCursor(x, 12); display.print(l2);
    const char* l3 = "will sync data to";
    x = (SCREEN_WIDTH - (strlen(l3) * 6)) / 2;
    display.setCursor(x, 22); display.print(l3);
    const char* l4 = "match this unit.";
    x = (SCREEN_WIDTH - (strlen(l4) * 6)) / 2;
    display.setCursor(x, 32); display.print(l4);
    const char* l5 = "Continue?";
    x = (SCREEN_WIDTH - (strlen(l5) * 6)) / 2;
    display.setCursor(x, 42); display.print(l5);
    const char* l6 = "RED: No     BLUE: Yes";
    x = (SCREEN_WIDTH - (strlen(l6) * 6)) / 2;
    display.setCursor(x, 56); display.print(l6);
    display.display();
}

void drawSyncingScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* l1 = "SYNCING";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 15);
    display.print(l1);
    display.setTextSize(1);
    const char* l2 = "Please wait ...";
    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
    display.setCursor(x, 45);
    display.print(l2);
    display.display();
}

void drawSyncedScreen(uint8_t fromUnitId) {
    display.clearDisplay();
    display.setTextSize(2);
    const char* l1 = "SYNCED!";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 14);
    display.print(l1);
    display.setTextSize(1);
    char buf[25];
    snprintf(buf, sizeof(buf), "by unit %s", UNIT_NAMES[fromUnitId - 1]);
    x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
    display.setCursor(x, 44);
    display.print(buf);
    display.display();
}

void drawSyncDoneScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* l1 = "DONE";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 14);
    display.print(l1);
    display.setTextSize(1);
    const char* l2 = "All active units";
    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
    display.setCursor(x, 40);
    display.print(l2);
    const char* l3 = "are synced";
    x = (SCREEN_WIDTH - (strlen(l3) * 6)) / 2;
    display.setCursor(x, 52);
    display.print(l3);
    display.display();
}

void drawTimeAlertScreen(uint8_t action) {
    const char* l1; const char* l2;
    switch (action) {
        case 0:  l1 = "GAME";     l2 = "STARTED"; break;
        case 1:  l1 = "GAME";     l2 = "PAUSED";  break;
        case 2:  l1 = "GAME";     l2 = "RESUMED"; break;
        default: l1 = "TIME WAS"; l2 = "RESET"; break;
    }
    display.clearDisplay();
    display.setTextSize(2);
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 15);
    display.print(l1);
    x = (SCREEN_WIDTH - (strlen(l2) * 12)) / 2;
    display.setCursor(x, 37);
    display.print(l2);
    display.display();
}

void drawBlockedScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    const char* l1 = "Can't do that";
    const char* l2 = "while playing ...";
    uint8_t x = (SCREEN_WIDTH - strlen(l1) * 6) / 2;
    display.setCursor(x, 26);
    display.print(l1);
    x = (SCREEN_WIDTH - strlen(l2) * 6) / 2;
    display.setCursor(x, 38);
    display.print(l2);
    display.display();
}
