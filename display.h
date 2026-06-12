#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "config.h"

// ============================================================
// display.h — Afisaj OLED (NOVA Units)
// ============================================================

// Obiectul display — definit in display.cpp, folosit peste tot
extern Adafruit_SSD1306 display;

// ============================================================
// PageContext — toate datele de care display-ul are nevoie.
// Populat de buildContext() (game.cpp), pasat la drawPages().
// ============================================================
struct PageContext {
    // General
    uint8_t batteryPercent;
    uint8_t currentPage;
    int8_t  selectedMode;
    bool    isTimeOut;
    Team    conquestWinner;
    WinCondition winCondition;
    bool    isGameTimerRunning;
    uint32_t gameTimeLeftSeconds;
    int32_t appliedPenalties[4];
    bool    isGamePaused;
    uint32_t pauseStartTime;
    uint32_t gameOverTime;
    // Sector
    Team    sectorOwner;
    uint32_t captureStartTime;
    uint32_t currentCapturePoints;
    uint32_t bonusIntervalMinutes;
    // Bomb
    bool    isBombArmed;
    bool    isCooldownActive;
    uint32_t bombPlantTime;
    uint32_t bombTimerMs;
    uint32_t cooldownStartTime;
    uint32_t cooldownMs;
    // Respawn
    Team    respawnTeam;
    uint8_t queueCount;
    uint8_t queueHead;
    uint32_t respawnQueue[1];
    uint16_t respawnPenaltyPoints;
    uint16_t teamMaxRespawns[4];
    uint16_t teamKills[4];
    // Scoruri
    int32_t  liveScore[4];
    uint16_t globalKills[12][4];
    // Retea
    uint8_t  globalUnitMode[12];
    Team     globalUnitStatus[12];
    uint32_t lastSeenTime[12];
    uint32_t globalEventTime[12];
    uint8_t  globalBattery[12];
    // Scroll
    uint8_t page4ScrollIndex;
    uint8_t page5ScrollIndex;
};

// ------------------------------------------------------------
// Initializare
// ------------------------------------------------------------
void displayInit();

// Anti-drift imagine SSD1309 — apelata periodic din loop() (la 3s)
void displayRefreshRegisters();

// ------------------------------------------------------------
// Ecran BOOT — returneaza TRUE cand cele 3 secunde s-au scurs.
// ------------------------------------------------------------
bool handleBoot();

// ------------------------------------------------------------
// Meniu + selectare mod + Loading/Ready
// ------------------------------------------------------------
void drawMenu(uint8_t menuIndex);
void drawRespawnSetup();
void drawLoadingScreen(uint32_t elapsed, uint32_t totalMs);
void drawReadyScreen(int8_t selectedMode);

// ------------------------------------------------------------
// Cele 6 pagini
// ------------------------------------------------------------
void drawPageHeader(uint8_t currentPage, uint8_t batteryPercent);
void drawScrollbar(uint8_t totalItems, uint8_t visibleItems, uint8_t scrollIndex, uint8_t yStart, uint8_t barHeight);
void drawPages(const PageContext& ctx);

// ============================================================
// ADMIN — meniu + sub-pagini de setari
// ============================================================
void drawAdminMenu(uint8_t menuIndex, uint8_t scrollIndex, int8_t selectedMode);

// AdminContext — datele celor 4 sub-pagini (0=Game,1=Bomb,2=Respawn,3=TagWriter)
struct AdminContext {
    uint8_t selectedPage;
    // Game Settings
    uint8_t gsIndex;
    uint8_t gsWinCond;
    uint8_t gsTimeLimit;
    uint8_t gsBonus;
    uint8_t gsActionIdx;
    // Bomb Settings
    uint8_t bsIndex;
    uint8_t bsTimerIdx;
    uint8_t bsCooldownIdx;
    uint8_t bsExpPtsIdx;
    uint8_t bsDefPtsIdx;
    // Respawn Settings
    uint8_t rsIndex;
    uint8_t rsTimeIdx;
    uint8_t rsPenaltyIdx;
    uint8_t rsLimitIdx[4];
    // Tag Writer
    uint8_t twIndex;
    uint8_t twOptionIdx;
};
void drawAdminPages(const AdminContext& ac);

// Ecran "SAVED" dupa confirmarea setarilor
void drawAdminSaved();

// Ecran "Turning Off ..."
void drawPowerOffScreen();

// Ecran Tag Writer (status scriere card)
void drawTagWriter(uint8_t statusMsg);

// Ecrane gameplay
void drawActionScreen(ActionType actionType, uint8_t teamIndex, uint32_t elapsed, uint32_t totalMs);
void drawSuccessScreen();
void drawBoomScreen();
void drawWaitAdminTag();
void setBrightness(uint8_t level);

// Ecrane Kill Reset
void drawKillResetAdminScreen();
void drawKillResetConfirmScreen();
void drawKillResetWinnerScreen();
void drawKillResetDoneScreen(uint16_t points, uint8_t teamIndex, bool hasPoints);

void drawBonusScreen(uint16_t points, uint8_t teamIndex);

// Ecrane Sync (LoRa)
void drawSyncWarningScreen();
void drawModeWarningScreen();
void drawExpImpMenu(uint8_t index);
void drawExpImpWait();
void drawExportWait();
void drawExportDone(const char* l1, const char* l2);
void drawImportWait();
void drawSyncingScreen();
void drawSyncedScreen(uint8_t fromUnitId);
void drawSyncDoneScreen();
void drawTimeAlertScreen(uint8_t action);
void drawBlockedScreen();
void drawRespawnDupScreen();
