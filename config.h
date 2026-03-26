// ============================================================
// config.h — Configurare Centrala Proiect Airsoft ESP32
// ============================================================
#pragma once

// ============================================================
// IDENTITATE UNITATE
// ============================================================
#define UNIT_ID       4
#define NETWORK_ID    "QO5"
#define MAX_UNITS     12

// ============================================================
// PINI HARDWARE
// ============================================================

// --- Releu & Buzzer ---
#define PIN_RELAY     16
#define PIN_BUZZER    2

// --- RFID (MFRC522 via SPI) ---
#define PIN_RFID_RST  17
#define PIN_RFID_MISO 19
#define PIN_RFID_MOSI 23
#define PIN_RFID_SCK  18
#define PIN_RFID_SDA  5

// --- LoRa (UART Hardware Serial1) ---
#define PIN_LORA_RX   35
#define PIN_LORA_TX   25
#define PIN_LORA_AUX  34

// --- Baterie (ADC) ---
#define PIN_BATTERY   36

// --- LED-uri (Rosu, Albastru, Verde, Galben) ---
const uint8_t PIN_LEDS[4] = { 32, 33, 15, 4 };

// --- Butoane (Rosu, Albastru, Verde, Galben) ---
const uint8_t PIN_BTNS[4] = { 13, 14, 26, 27 };

// ============================================================
// CONFIGURARE LORA
// ============================================================
#define LORA_BAUD_RATE  9600

// ============================================================
// DISPLAY OLED
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// ============================================================
// TIMING & GAMEPLAY
// ============================================================
#define DEBOUNCE_DELAY_MS       50
#define LONG_PRESS_MS           1000
#define ACTION_TIME_MS          5000
#define RELAY_DURATION_MS       20000

// ============================================================
// RFID — Chei de Securitate
// ============================================================
// Cheia de fabrica pentru carduri noi (FF FF FF FF FF FF)
#define RFID_DEFAULT_KEY  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
// Cheia secreta custom (securizeaza cardurile impotriva telefoanelor)
#define RFID_CUSTOM_KEY   { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6 }
#define RFID_MAGIC_BYTE   0x4E
#define RFID_BLOCK_ADDR   4

// ============================================================
// ECHIPE & UNITATI
// ============================================================
enum Team : uint8_t {
    TEAM_NEUTRAL = 0,
    TEAM_RED     = 1,
    TEAM_BLUE    = 2,
    TEAM_GREEN   = 3,
    TEAM_YELLOW  = 4,
    TEAM_PLANTED = 99   // Stare speciala pentru bomba plantata
};

const char* const TEAM_NAMES[4] = {
    "Phantom", "Sentinel", "Falcon", "Nemesis"
};

const char* const UNIT_NAMES[MAX_UNITS] = {
    "Alpha", "Bravo", "Charlie", "Delta",
    "Echo",  "Foxtrot", "Golf",  "Hotel",
    "India", "Juliett", "Kilo",  "Lima"
};

// ============================================================
// STARI PRINCIPALE ALE MASINII DE STARI
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
    STATE_TIME_STARTED,
    STATE_ADMIN_MENU,
    STATE_ADMIN_PAGES,
    STATE_ADMIN_SAVED,
    STATE_ADMIN_TAG_WRITE,
    STATE_ADMIN_SYNC_WARN,
    STATE_SYNC_RECEIVED,
    STATE_WAIT_ADMIN_TAG,
    STATE_LOADING,
    STATE_READY,
    STATE_SYNCED_RECEIVED
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
    ACT_NEUTRALIZE = 2,
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
