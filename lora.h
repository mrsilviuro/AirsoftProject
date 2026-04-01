#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
// lora.h — Comunicatie LoRa (UART, modul E32 sau similar)
// ============================================================

// ============================================================
// Tipuri de pachete
// ============================================================
#define PKT_SYNC        0x01
#define PKT_START       0x02
#define PKT_HEARTBEAT   0x03
#define PKT_URGENT      0x04
#define PKT_EPOCH_SYNC  0x05
#define PKT_RESTART     0x06
#define PKT_CONQUEST    0x07

// ============================================================
// Tipuri de evenimente urgente (piggybacking inclus)
// ============================================================
#define EVT_NONE            0
#define EVT_SECTOR_CAPTURED 1
#define EVT_SECTOR_NEUTRAL  2
#define EVT_BOMB_ARMED      3
#define EVT_BOMB_DEFUSED    4
#define EVT_BOMB_EXPLODED   5
#define EVT_MODE_SECTOR     6
#define EVT_MODE_BOMB       7
#define EVT_MODE_RESPAWN    8
#define EVT_MODE_UNSET      9
#define EVT_GAME_PAUSED     10
#define EVT_GAME_RESUMED    11

// ============================================================
// Stare retea — accesibila din .ino
// ============================================================
extern char    currentSyncID[4];   // Parola sesiunii curente
extern bool    isNetworkSynced;    // True dupa primul sync
extern bool    isMasterNode;       // True daca aceasta unitate a facut sync-ul

extern bool    loraSyncJustReceived;
extern uint8_t loraSyncFromUnit;

// TDMA
extern uint32_t syncEpochSeconds;  // Secunda curenta in ciclul de 60s
extern uint32_t lastEpochTick;     // Millis la ultima bataie a ceasului
extern uint32_t syncReceivedTime;  // Millis cand am primit/trimis sync-ul
extern bool     hasTransmittedThisMinute;
extern bool     finalHeartbeatSent;

// Date retea (populate din heartbeat-uri)
extern int32_t  loraRxScores[4];
extern uint16_t loraRxKills[4];
extern int32_t  loraRxPenalties[4];
extern uint8_t  globalUnitMode[MAX_UNITS];
extern Team     globalUnitStatus[MAX_UNITS];
extern uint32_t lastSeenTime[MAX_UNITS];
extern uint32_t globalEventTime[MAX_UNITS];
extern uint8_t  globalBattery[MAX_UNITS];

// Piggybacking — alerta urgenta in asteptare
extern uint8_t pendingEventType;
extern uint8_t pendingEventTeam;

extern bool    loraSettingsReceived;
extern uint8_t rx_gsTimeLimit, rx_gsBonus, rx_gsWinCond;
extern uint8_t rx_bsTimerIdx, rx_bsCooldownIdx;
extern uint8_t rx_bsExpPtsIdx, rx_bsDefPtsIdx;
extern uint8_t rx_rsTimeIdx, rx_rsPenaltyIdx;
extern uint8_t rx_rsLimitIdx[4];

extern bool loraStartJustSent;

extern bool loraPauseJustSent;
extern bool loraResumeJustSent;

// ============================================================
// Initializare
// ============================================================
void loraInit();

// ============================================================
// Update — apelata in fiecare loop()
// Gestioneaza: receptia pachetelor, TDMA, EpochSync periodic
// ============================================================
void loraUpdate(
    int32_t  liveScore[4],
    uint16_t teamKills[4],
    int32_t  appliedPenalties[4],  // ← nou
    uint8_t  selectedMode,
    Team     sectorOwner,
    bool     isBombArmed,
    Team     respawnTeam,
    uint8_t  batteryPercent,
    bool&    isTimeOut,
    bool&    isGameTimerRunning,
    uint32_t& gameTimeLeftSeconds
);

// ============================================================
// Transmisii
// ============================================================

// Sync complet — trimite toate setarile + puncte + killuri
// Daca isNetworkSynced == false, trimite imediat
// Daca isNetworkSynced == true, aplica jitter 1000-3000ms
void loraSendSync(
    uint8_t  gsTimeLimit, uint8_t gsBonus, uint8_t gsWinCond,
    uint8_t  bsTimerIdx,  uint8_t bsCooldownIdx,
    uint8_t  bsExpPtsIdx, uint8_t bsDefPtsIdx,
    uint8_t  rsTimeIdx,   uint8_t rsPenaltyIdx,
    uint8_t  rsLimitIdx[4],
    bool     isRunning,   bool isTimeOut,
    uint32_t gameTimeLeft,
    int32_t  scores[4],   uint16_t kills[4], int32_t penalties[4],
    uint32_t lastTimerTick  // ← adauga doar asta
);

// Returneaza true daca am primit comanda de restart global
// .ino trebuie sa verifice si sa execute ESP.restart()
bool loraRestartPending();

// Definita in .ino — afiseaza timing TDMA in serial monitor
extern void loraPrintTiming(const char* label);

// Versiune blocanta pentru restart — singura exceptie acceptata
void loraSendRestartBlocking();

// Start joc — trimite comanda de start + timp ramas
void loraSendStart(uint32_t gameTimeLeftSeconds);

// Alerta urgenta — respecta fereastra TDMA
// Returneaza false daca aerul e ocupat (alerta salvata pentru piggybacking)
bool loraSendUrgent(uint8_t eventType, uint8_t teamId);

// Restart global — fara SyncID in CRC
void loraSendRestart();

// Conquest victory
void loraSendConquest(uint8_t winnerTeam);

extern int32_t loraRxPenalties[4];
extern bool loraSyncTimerReset;
extern uint32_t loraStartGameTimeLeft;
extern uint32_t loraRxTimerTick;
extern uint32_t loraMasterTimerTick;
