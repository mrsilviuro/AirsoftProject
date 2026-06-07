#pragma once
#include <Arduino.h>

#include "config.h"
#include "display.h"  // pentru PageContext

// ============================================================
// game.h — MODELUL JOCULUI (tabelul celor 12 unitati)
// ============================================================

// --- Status per mod (interpretat in functie de "mode") ---
enum SectorStatus : uint8_t { SEC_NEUTRAL = 0, SEC_CAPTURED = 1 };
enum BombStatus   : uint8_t { BOMB_IDLE   = 0, BOMB_ARMED   = 1, BOMB_COOLDOWN = 2 };

// --- Un rand din tabel = o unitate ---
struct UnitRow {
    uint8_t  mode;            // 0=none, 1=sector, 2=bomb, 3=respawn
    uint8_t  status;          // sector: SEC_*; bomb: BOMB_*; respawn: 0
    Team     team;            // cucerit / plantat / detinut de (TEAM_NEUTRAL daca nimeni)
    uint32_t actionTime;      // millis ultimei actiuni (ceas LOCAL pentru randul propriu)
    int32_t  savedPoints[4];  // puncte COMMITED per echipa A,B,C,D
    uint16_t kills[4];        // kill-uri per echipa
};

extern UnitRow unitTable[MAX_UNITS];   // tabelul complet
UnitRow& myRow();                      // randul unitatii curente = unitTable[UNIT_ID-1]
Team checkConquest();

// Acumulatorul de puncte LIVE al sectorului local (inca necomise in savedPoints)
extern int32_t  liveCapture[MAX_UNITS];   // puncte estimate ale cuceririi curente, per unitate
extern uint32_t lastPointTick[MAX_UNITS];  // tick 10s per unitate cucerita

// Scor prin INSUMARE peste tabel (inlocuieste max()-ul din vechi)
int32_t  teamScore(uint8_t team);       // suma savedPoints + live local (fara penalizari)
uint16_t teamKillTotal(uint8_t team);

// --- Identitate rol unitate ---
extern int8_t selectedMode;   // -1=nesetat, 0=Sector, 1=Bomb, 2=Respawn

// --- Penalizari ---
extern int32_t appliedPenalties[4];

// --- Setari Bomba ---
extern uint32_t bombTimerMs;
extern uint32_t cooldownMs;
extern uint32_t bombPointsExplode;
extern uint32_t bombPointsDefuse;

// --- Setari Respawn + coada ---
extern uint32_t respawnTimeMs;
extern uint16_t respawnPenaltyPoints;
extern uint16_t teamMaxRespawns[4];
extern uint8_t  queueCount, queueHead, queueTail;
extern uint32_t respawnQueue[100];

// --- Flux joc ---
extern uint32_t     gameTimeLeftSeconds;
extern uint32_t     gameTimeLimitSeconds;
extern bool         isGameTimerRunning;
extern bool         isTimeOut;
extern Team         conquestWinner;
extern WinCondition currentWinCondition;
extern uint32_t     bonusIntervalMinutes;
extern uint32_t     actionTimeMs;
extern bool         isGamePaused;
extern uint32_t     pauseStartTime;
extern uint32_t     gameOverTime;

// --- Comunicatii (meta, nu fac parte din tabelul de joc) ---
extern uint8_t  globalBattery[MAX_UNITS];
extern uint32_t lastSeenTime[MAX_UNITS];

// Construieste PageContext-ul din tabel.
void buildContext(PageContext& ctx, uint8_t currentPage, uint8_t batteryPercent,
                  uint8_t page4Scroll, uint8_t page5Scroll);
