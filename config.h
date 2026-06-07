// ============================================================
// config.h — Configurare Centrala NOVA Units (ESP32)
// ============================================================
#pragma once
#include <Arduino.h>

// ============================================================
// IDENTITATE UNITATE  — se schimba per unitate inainte de upload
// ============================================================
#define UNIT_ID 4
#define NETWORK_ID 0x4E      // 1 octet, marker+filtru retea (schimba per lot de unitati)
#define MAX_UNITS 12

// ============================================================
// PINI HARDWARE
// ============================================================

// --- Releu, Buzzer & Power latch ---
#define PIN_RELAY  16
#define PIN_BUZZER 2
#define PIN_LATCH  17   // tinere alimentare (power-off)

// --- RFID (PN532 via SPI) ---
#define PIN_RFID_MISO 19
#define PIN_RFID_MOSI 23
#define PIN_RFID_SCK  18
#define PIN_RFID_SDA  5   // CS/SS

// --- LoRa (UART Serial1) ---
#define PIN_LORA_RX  35
#define PIN_LORA_TX  25
#define PIN_LORA_AUX 34

// --- Baterie (ADC) ---
#define PIN_BATTERY 36

// --- LED-uri (Rosu, Albastru, Verde, Galben) ---
const uint8_t PIN_LEDS[4] = { 32, 33, 15, 4 };

// --- Butoane (Rosu, Albastru, Verde, Galben) ---
const uint8_t PIN_BTNS[4] = { 13, 14, 26, 27 };

// ============================================================
// LORA
// ============================================================
#define LORA_BAUD_RATE 9600

// --- Tipuri pachete LoRa ---
#define PKT_SYNC 0x01
#define PKT_RESTART 0x02
#define PKT_MODE 0x03
#define PKT_TIME_START  0x04
#define PKT_TIME_PAUSE  0x05
#define PKT_TIME_RESUME 0x06
#define PKT_TIME_RESET  0x07
#define PKT_CAPTURE     0x08
#define PKT_NEUTRALIZE  0x09
#define PKT_RESPAWN     0x0A
#define PKT_BOMB_PLANT  0x0B
#define PKT_BOMB_DEFUSE 0x0C
#define PKT_KILLRESET   0x0D
#define PKT_HEARTBEAT   0x0E

// ============================================================
// DISPLAY OLED (2.42" SSD1309 condus de libraria SSD1306)
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// ============================================================
// TIMING & GAMEPLAY
// ============================================================
#define DEBOUNCE_DELAY_MS 50
#define LONG_PRESS_MS     1000
#define RELAY_DURATION_MS 20000

// ============================================================
// RFID — Chei de Securitate
// ============================================================
#define RFID_DEFAULT_KEY \
  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#define RFID_CUSTOM_KEY \
  { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6 }
#define RFID_MAGIC_BYTE 0x4E
#define RFID_BLOCK_ADDR 4

// ============================================================
// ECHIPE & UNITATI
// ============================================================
enum Team : uint8_t {
  TEAM_NEUTRAL = 0,
  TEAM_RED     = 1,
  TEAM_BLUE    = 2,
  TEAM_GREEN   = 3,
  TEAM_YELLOW  = 4,
  TEAM_PLANTED = 99  // stare speciala: bomba plantata
};

const char* const TEAM_NAMES[4] = {
  "Phantom", "Sentinel", "Falcon", "Nemesis"
};

const char* const UNIT_NAMES[MAX_UNITS] = {
  "Alpha", "Bravo", "Charlie", "Delta",
  "Echo", "Foxtrot", "Golf", "Hotel",
  "India", "Juliett", "Kilo", "Lima"
};

// ============================================================
// STARI PRINCIPALE ALE MASINII DE STARI
// (pastram setul interfetei; starile legate de retea le finalizam in Faza 2)
// ============================================================
enum GameState : uint8_t {
  STATE_BOOT,
  STATE_MENU,
  STATE_PAGES,
  STATE_ACTION,
  STATE_SUCCESS,
  STATE_BOOM,
  STATE_RESPAWN_SETUP,
  STATE_BONUS_SCREEN,
  STATE_ADMIN_MENU,
  STATE_ADMIN_PAGES,
  STATE_ADMIN_SAVED,
  STATE_ADMIN_TAG_WRITE,
  STATE_WAIT_ADMIN_TAG,
  STATE_LOADING,
  STATE_READY,
  STATE_KILL_RESET_ADMIN,
  STATE_KILL_RESET_CONFIRM,
  STATE_KILL_RESET_WINNER,
  STATE_KILL_RESET_DONE,
  STATE_TIME_RESET_ADMIN,
  STATE_SYNC_WARNING,
  STATE_SYNCING,
  STATE_SYNCED,
  STATE_SYNC_DONE,
  STATE_TIME_ALERT,
  STATE_ADMIN_BLOCKED,
  STATE_POWER_OFF
};

// ============================================================
// MODURI UNITATE
// ============================================================
enum UnitMode : uint8_t {
  MODE_UNSET   = 0,
  MODE_SECTOR  = 1,
  MODE_BOMB    = 2,
  MODE_RESPAWN = 3
};

// ============================================================
// CONDITII DE VICTORIE
// ============================================================
enum WinCondition : uint8_t {
  WIN_BY_POINTS   = 0,
  WIN_BY_CONQUEST = 1,
  WIN_BY_ANY      = 2
};

// ============================================================
// TIPURI DE ACTIUNI (Sector / Bomba)
// ============================================================
enum ActionType : uint8_t {
  ACT_NONE      = 0,
  ACT_CAPTURE   = 1,
  ACT_NEUTRALIZE= 2,
  ACT_ARM       = 3,
  ACT_DEFUSE    = 4
};

// ============================================================
// TIPURI DE CARDURI RFID
// ============================================================
enum TagType : uint8_t {
  TAG_UNKNOWN = 0,
  TAG_POINTS  = 1,
  TAG_ADMIN   = 2
};
