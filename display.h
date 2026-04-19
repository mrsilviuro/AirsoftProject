#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "config.h"

// ============================================================
// Obiectul display — definit in display.cpp, folosit peste tot
// ============================================================
extern Adafruit_SSD1306 display;

// ============================================================
// PageContext — toate datele de care display-ul are nevoie
// Populat din .ino (sau game.h mai tarziu) si pasat la drawPages()
// ============================================================
struct PageContext {
    // --- General ---
    uint8_t batteryPercent;
    uint8_t currentPage;
    int8_t selectedMode;  // -1=nesetat, 0=Sector, 1=Bomb, 2=Respawn
    bool isTimeOut;
    Team conquestWinner;
    WinCondition winCondition;
    bool isGameTimerRunning;
    uint32_t gameTimeLeftSeconds;
    int32_t appliedPenalties[4];
    bool isGamePaused;
    uint32_t pauseStartTime;

    // --- Sector ---
    Team sectorOwner;
    uint32_t captureStartTime;
    uint32_t currentCapturePoints;
    uint32_t bonusIntervalMinutes;

    // --- Bomb ---
    bool isBombArmed;
    bool isCooldownActive;
    uint32_t bombPlantTime;
    uint32_t bombTimerMs;
    uint32_t cooldownStartTime;
    uint32_t cooldownMs;

    // --- Respawn ---
    Team respawnTeam;
    uint8_t queueCount;
    uint8_t queueHead;
    uint32_t respawnQueue[1];
    uint16_t respawnPenaltyPoints;
    uint16_t teamMaxRespawns[4];
    uint16_t teamKills[4];

    // --- Scoruri ---
    int32_t liveScore[4];
    uint16_t globalKills[12][4];

    // --- Retea ---
    uint8_t globalUnitMode[12];
    Team globalUnitStatus[12];
    uint32_t lastSeenTime[12];
    uint32_t globalEventTime[12];
    uint8_t globalBattery[12];

    // --- Scroll ---
    uint8_t page4ScrollIndex;
    uint8_t page5ScrollIndex;
};

// ============================================================
// Initializare
// ============================================================
void displayInit();
void drawLoadingScreen(uint32_t elapsed, uint32_t totalMs);
void drawReadyScreen(int8_t selectedMode);

// ============================================================
// Ecran BOOT
// ============================================================
// Returneaza TRUE cand cele 3 secunde s-au scurs.
bool handleBoot();

// ============================================================
// Ecran MENIU
// ============================================================
void drawMenu(uint8_t menuIndex);

// ============================================================
// Cele 6 Pagini
// ============================================================
// Deseneaza header-ul comun (navigare + baterie + linie separator).
// Apelata intern de drawPages(), nu direct din .ino.
void drawPageHeader(uint8_t currentPage, uint8_t batteryPercent);

// Functia principala — apelata din loop() cand needsDisplayUpdate == true.
// Deseneaza header-ul + continutul paginii curente din ctx.
void drawPages(const PageContext& ctx);

// ============================================================
// Ecran ACTION (bara de progres in timpul cuceririi/neutralizarii)
// ============================================================
// actionType : ACT_CAPTURE sau ACT_NEUTRALIZE
// teamIndex  : 0-3 (index buton = index echipa)
// elapsed    : millis scursi de la inceputul actiunii
// totalMs    : durata totala a actiunii in ms
void drawActionScreen(ActionType actionType, uint8_t teamIndex, uint32_t elapsed, uint32_t totalMs);

// ============================================================
// Ecran SUCCESS (afisat 3 secunde dupa o actiune reusita)
// ============================================================
void drawSuccessScreen();
void drawSyncedScreen(uint8_t fromUnitId);

// ============================================================
// Ecran BOOOM! (afisat 3 secunde dupa o actiune reusita)
// ============================================================
void drawBoomScreen();

void drawBonusScreen(uint16_t points);
void drawWaitAdminTag();

// ============================================================
// Ecran "SELECT TEAM"
// ============================================================
void drawRespawnSetup();

// Admin Menu
void drawAdminMenu(uint8_t menuIndex, uint8_t scrollIndex, int8_t selectedMode);

// Admin Sub-Pagini (0=Game, 1=Bomb, 2=Respawn, 3=TagWriter)
struct AdminContext {
    uint8_t selectedPage;

    // Game Settings
    uint8_t gsIndex;
    uint8_t gsWinCond;
    uint8_t gsTimeLimit;
    uint8_t gsBonus;

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

// Ecran "SAVED" dupa confirmare setari
void drawAdminSaved();

// Ecran TAG Writer — asteptare card
// statusMsg: 0=asteapta, 1=ok_new, 2=ok_overwrite, 3=ok_admin,
//            4=ok_revoked, 5=denied, 6=timeout, 7=error
void drawTagWriter(uint8_t statusMsg);

// ============================================================
// Helper — Scrollbar vertical (folosit pe paginile 4 si 5)
// ============================================================
void drawScrollbar(uint8_t totalItems, uint8_t visibleItems, uint8_t scrollIndex, uint8_t yStart, uint8_t barHeight);

void drawSyncWarningScreen();
void drawSyncingScreen();

void displayRefreshRegisters();
