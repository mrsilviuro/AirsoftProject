// ============================================================
// lora.h — Strat transport LoRa (DX-LR03) + ceas comun
// Faza 2. Acum: doar Sync Units (setari + LocalTime).
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// --- Ceas comun "localTime" (secunde) — ruleaza din boot ---
extern uint32_t localTime;
extern uint32_t lastLocalTick;
extern bool     localTimePaused;

// --- Stare retea ---
// isSynced == false => OFFLINE: nu transmite nicio alerta si ignora
// alertele de joc primite (dar asculta pachetele SYNC ca sa poata fi adusa online).
extern bool     isSynced;
extern uint8_t  syncedByUnit;   // ce unitate ne-a sincronizat ultima data

// --- Indecsi setari (DEFINITI in .ino) — necesari pentru pachetul SYNC ---
extern uint8_t gsWinCond, gsTimeLimit, gsBonus, gsActionIdx;
extern uint8_t bsTimerIdx, bsCooldownIdx, bsExpPtsIdx, bsDefPtsIdx;
extern uint8_t rsTimeIdx, rsPenaltyIdx;
extern uint8_t rsLimitIdx[4];

// Evenimente intoarse de loraPoll()
enum LoraEvent : uint8_t { LORA_EVT_NONE = 0, LORA_EVT_SYNC = 1, LORA_EVT_RESTART = 2, LORA_EVT_TIME = 3, LORA_EVT_CAPTURE = 4, LORA_EVT_NEUTRALIZE = 5, LORA_EVT_RESPAWN = 6, LORA_EVT_BOMB_PLANT = 7, LORA_EVT_BOMB_DEFUSE = 8, LORA_EVT_KILLRESET = 9 };
extern uint8_t loraEvtUnit;     // unitatea sursa (1..12) pt CAPTURE/NEUTRALIZE
extern uint8_t loraEvtTeam;     // echipa (1..4)
extern int32_t loraEvtPoints;   // puncte exacte (NEUTRALIZE)
extern uint8_t loraTimeAction;  // 0=start,1=pause,2=resume,3=reset (setat de loraPoll la LORA_EVT_TIME)
extern uint16_t loraResumeTime;  // secunda primita in alerta de RESUME (setata de loraPoll)

void loraInit();
void loraTick();                                          // incrementeaza localTime la fiecare secunda
void loraTxUpdate();                                      // pompeaza coada TX (non-blocant); apelata in loop()
void loraSendSyncBlocking();                              // trimite pachetul SYNC (blocant; pauza/reia ceasul pe AUX)
void loraSendRestart();                                   // pune in coada alerta RESTART (background, doar daca synced)
void loraSendTime(uint8_t pktType, uint16_t timeVal);     // alerta timp (background); aplici cand loraTxIdle()
void loraSendCapture(uint8_t team);                       // alerta cucerire sector (background)
void loraSendNeutralize(uint8_t team, int32_t points);    // alerta neutralizare sector (background)
void loraSendRespawn(uint8_t team, uint16_t totalKills);  // alerta respawn (total curent kill-uri, background)
void loraSendBombPlant(uint8_t team);                     // alerta plantare bomba (background)
void loraSendBombDefuse(uint8_t team);                    // alerta dezamorsare bomba (background)
void loraSendKillReset(uint8_t winnerTeam, uint16_t pts); // alerta reset kill-uri (background)
bool loraTxIdle();                                        // true cand coada TX e goala (AUX LOW dupa transmisie)
void loraSendMode(uint8_t mode, uint8_t team);            // pune in coada alerta MODE (background)
LoraEvent loraPoll();                                     // citeste UART; intoarce evenimentul primit (deja aplicat)
