#include <Arduino.h>

#include "display.h"
// ============================================================
// Obiectul display (definit aici, exportat prin display.h)
// ============================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// ============================================================
// BOOT — Date interne (static = invizibile in afara acestui fisier)
// ============================================================
static const uint16_t BOOT_NOTES[] = {523, 659, 784, 1046, 0};
static const uint16_t NOTE_DURATIONS[] = {150, 150, 150, 300, 100};
static const uint8_t TOTAL_NOTES = 5;
static uint32_t bootStartTime = 0;
static uint8_t lastCountdown = 255;  // 255 = valoare imposibila, forteaza primul draw
static bool melodyPlaying = true;
static uint8_t currentNote = 0;
static uint32_t noteStartTime = 0;
// ============================================================
// displayInit()
// ============================================================
void displayInit() {
    display.begin(SSD1306_EXTERNALVCC, 0x3C);
    Wire.setClock(100000);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(175);  // 80%
    // Pornim ceasul de boot si melodia
    bootStartTime = millis();
    noteStartTime = millis();
    // Prima nota si primul LED pornite direct, fara sa asteptam loop()
    tone(PIN_BUZZER, BOOT_NOTES[0]);
    for (uint8_t i = 0; i < 4; i++) {
        digitalWrite(PIN_LEDS[i], (i == 0) ? HIGH : LOW);
    }
}

void displayRefreshRegisters() {
    display.ssd1306_command(0x2E);  // Deactivate scroll
    display.ssd1306_command(0x40);  // Display start line = 0
    display.ssd1306_command(0xD3);  // Set display offset
    display.ssd1306_command(0x00);  // Offset = 0
}
// ============================================================
// handleBoot()
// ============================================================
bool handleBoot() {
    uint32_t now = millis();
    uint32_t elapsed = now - bootStartTime;
    // --- Verificam daca au trecut 3 secunde ---
    if (elapsed >= 3000) {
        // Curatenie: oprim sunetul si stingem LED-urile
        noTone(PIN_BUZZER);
        for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], LOW);
        return true;  // Semnal catre .ino: treci la STATE_MENU
    }
    // --- Ecran: redesenam DOAR la schimbarea secundei (nu in fiecare loop) ---
    uint8_t countdown = 3 - (elapsed / 1000);  // 3 -> 2 -> 1
    if (countdown != lastCountdown) {
        display.clearDisplay();
        // Titlu MARE (Size 2)
        display.setTextSize(2);
        const char* title = "NOVA UNITS";
        uint8_t x = (SCREEN_WIDTH - (strlen(title) * 12)) / 2;
        display.setCursor(x, 10);
        display.print(title);
        // Countdown (Size 1)
        display.setTextSize(1);
        char buf[25];
        snprintf(buf, sizeof(buf), "Initialization %u ...", countdown);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 35);
        display.print(buf);
        // Numele clubului
        const char* club = "Airsoft Club Roman";
        x = (SCREEN_WIDTH - (strlen(club) * 6)) / 2;
        display.setCursor(x, 48);
        display.print(club);
        display.display();
        lastCountdown = countdown;
    }
    // --- Melodie non-blocking ---
    if (melodyPlaying) {
        if (now - noteStartTime >= NOTE_DURATIONS[currentNote]) {
            currentNote++;
            noteStartTime = now;
            if (currentNote >= TOTAL_NOTES) {
                // Melodia s-a terminat
                melodyPlaying = false;
                noTone(PIN_BUZZER);
                for (uint8_t i = 0; i < 4; i++) digitalWrite(PIN_LEDS[i], LOW);
            } else {
                // Urmatoarea nota
                if (BOOT_NOTES[currentNote] > 0)
                    tone(PIN_BUZZER, BOOT_NOTES[currentNote]);
                else
                    noTone(PIN_BUZZER);
                // LED-uri in cascada: unul aprins, restul stinse
                for (uint8_t i = 0; i < 4; i++) {
                    digitalWrite(PIN_LEDS[i], (i == (currentNote % 4)) ? HIGH : LOW);
                }
            }
        }
    }
    return false;  // Boot inca in desfasurare
}
// ============================================================
// Iconite stocate in Flash (PROGMEM)
// ============================================================
static const unsigned char POINT_BMP[] PROGMEM = {0x38, 0x7C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38};
static const unsigned char SKULL_BMP[] PROGMEM = {0x10, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x10};
static const unsigned char ARROW_LEFT[] PROGMEM = {
    0x08,  // 00001000
    0x18,  // 00011000
    0x38,  // 00111000
    0x78,  // 01111000
    0x38,  // 00111000
    0x18,  // 00011000
    0x08   // 00001000
};

static const unsigned char ARROW_RIGHT[] PROGMEM = {
    0x80,  // 10000000
    0xC0,  // 11000000
    0xE0,  // 11100000
    0xF0,  // 11110000
    0xE0,  // 11100000
    0xC0,  // 11000000
    0x80   // 10000000
};
// ============================================================
// drawMenu()
// ============================================================
void drawMenu(uint8_t menuIndex) {
    display.clearDisplay();
    display.setTextSize(1);
    // Header: "Bravo Unit" — generat din UNIT_ID si UNIT_NAMES (config.h)
    char header[20];
    snprintf(header, sizeof(header), "%s Unit", UNIT_NAMES[UNIT_ID - 1]);
    uint8_t x = (SCREEN_WIDTH - (strlen(header) * 6)) / 2;
    display.setCursor(x, 0);
    display.print(header);
    // Linie separator
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
    // Cele 3 optiuni
    const char* const items[3] = {"Sector Unit", "Bomb Unit", "Respawn Unit"};
    for (uint8_t i = 0; i < 3; i++) {
        display.setCursor(0, 14 + (i * 10));
        if (i == menuIndex) {
            display.drawBitmap(0, 14 + (i * 10), ARROW_RIGHT, 5, 7, SSD1306_WHITE);
            display.setCursor(12, 14 + (i * 10));
        } else {
            display.print("  ");
        }
        display.print(items[i]);
    }
    // Footer cu instructiuni
    display.setCursor(0, 46);
    display.print("GREEN to scroll");
    display.setCursor(0, 56);
    display.print("YELLOW to confirm");
    display.display();
}
void drawBoomScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* msg = "BOOOOOM!";
    uint8_t x = (SCREEN_WIDTH - (strlen(msg) * 12)) / 2;
    display.setCursor(x, 24);
    display.print(msg);
    display.display();
}
void drawRespawnSetup() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* l1 = "SELECT";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 15);
    display.print(l1);
    const char* l2 = "TEAM";
    x = (SCREEN_WIDTH - (strlen(l2) * 12)) / 2;
    display.setCursor(x, 35);
    display.print(l2);
    display.setTextSize(1);
    display.display();
}
// ============================================================
// drawActionScreen()
// ============================================================
void drawActionScreen(ActionType actionType, uint8_t teamIndex, uint32_t elapsed, uint32_t totalMs) {
    display.clearDisplay();
    display.setTextSize(1);
    // Linia 1 — ce actiune se face
    const char* actText = "";
    if (actionType == ACT_CAPTURE)
        actText = "CAPTURING SECTOR...";
    else if (actionType == ACT_NEUTRALIZE)
        actText = "NEUTRALIZING...";
    else if (actionType == ACT_ARM)
        actText = "ARMING BOMB...";
    else if (actionType == ACT_DEFUSE)
        actText = "DEFUSING BOMB...";
    uint8_t x = (SCREEN_WIDTH - (strlen(actText) * 6)) / 2;
    display.setCursor(x, 9);
    display.print(actText);
    // Linia 2 — numele echipei care actioneaza
    const char* teamName = TEAM_NAMES[teamIndex];
    x = (SCREEN_WIDTH - (strlen(teamName) * 6)) / 2;
    display.setCursor(x, 21);
    display.print(teamName);
    // Bara de progres
    // Contur: x=14, y=40, w=100, h=12
    display.drawRect(14, 35, 100, 12, SSD1306_WHITE);
    // Umplere: mapam elapsed -> 0..96 pixeli (interior = 96px latime)
    uint8_t barW = (uint8_t)((uint32_t)96 * elapsed / totalMs);
    if (barW > 96) barW = 96;
    if (barW > 0) display.fillRect(16, 37, barW, 8, SSD1306_WHITE);
    // Timer numeric sub bara (cat timp a ramas)
    uint32_t remaining = (elapsed < totalMs) ? (totalMs - elapsed) / 1000 + 1 : 0;
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%us", remaining);
    x = (SCREEN_WIDTH - (strlen(timeBuf) * 6)) / 2;
    display.setCursor(x, 51);
    display.print(timeBuf);
    display.display();
}
// ============================================================
// drawSuccessScreen()
// ============================================================
void drawSuccessScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* msg = "SUCCESS!";
    uint8_t x = (SCREEN_WIDTH - (strlen(msg) * 12)) / 2;
    display.setCursor(x, 24);
    display.print(msg);
    display.display();
}
// ============================================================
// drawScrollbar()
// ============================================================
void drawScrollbar(uint8_t totalItems, uint8_t visibleItems, uint8_t scrollIndex, uint8_t yStart, uint8_t barHeight) {
    if (totalItems <= visibleItems || totalItems == 0) return;
    uint8_t thumbH = (barHeight * visibleItems) / totalItems;
    if (thumbH < 4) thumbH = 4;
    uint8_t maxScroll = totalItems - visibleItems;
    uint8_t thumbY = yStart + ((scrollIndex * (barHeight - thumbH)) / maxScroll);
    display.fillRect(SCREEN_WIDTH - 1, thumbY, 1, thumbH, SSD1306_WHITE);
}
// ============================================================
// drawPageHeader()
// ============================================================
void drawPageHeader(uint8_t currentPage, uint8_t batteryPercent) {
    display.setTextSize(1);

    // Sageata stanga (5x7px), centrata vertical in header (y=1)
    display.drawBitmap(0, 1, ARROW_LEFT, 5, 7, SSD1306_WHITE);

    // "Red" dupa sageata
    display.setCursor(10, 1);
    display.print("Red");

    // Numarul paginii, centrat
    char pageNum[3];
    snprintf(pageNum, sizeof(pageNum), "%u", currentPage + 1);
    uint8_t x = (SCREEN_WIDTH - 31 - (strlen(pageNum) * 6)) / 2;
    display.setCursor(x, 1);
    display.print(pageNum);

    // "Blue" inainte de sageata dreapta
    // "Blue" = 4 chars * 6px = 24px, sageata = 5px, spatiu = 2px
    // => startX = 128 - 5 - 2 - 24 = 97
    display.setCursor(97 - 29, 1);
    display.print("Blue");

    // Sageata dreapta (5x7px)
    display.drawBitmap(SCREEN_WIDTH - 32, 1, ARROW_RIGHT, 5, 7, SSD1306_WHITE);

    // Baterie
    bool drawBat = true;
    uint8_t bars = 0;
    if (batteryPercent >= 80)
        bars = 4;
    else if (batteryPercent >= 60)
        bars = 3;
    else if (batteryPercent >= 40)
        bars = 2;
    else if (batteryPercent >= 20)
        bars = 1;
    else if (batteryPercent >= 10)
        bars = 0;
    else {
        bars = 0;
        if ((millis() / 500) % 2 == 0) drawBat = false;
    }

    if (drawBat) {
        display.fillRect(107, 2, 2, 5, SSD1306_WHITE);
        display.drawRect(109, 0, 19, 9, SSD1306_WHITE);
        for (uint8_t b = 0; b < bars; b++) {
            uint8_t barX = 123 - (b * 4);
            display.fillRect(barX, 2, 3, 5, SSD1306_WHITE);
        }
    }

    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
}
// ============================================================
// drawPages()
// ============================================================
void drawPages(const PageContext& ctx) {
    display.clearDisplay();
    drawPageHeader(ctx.currentPage, ctx.batteryPercent);
    switch (ctx.currentPage) {
        // ====================================================
        // PAGINA 1 — GAMEPLAY (se adapteaza dupa mod)
        // ====================================================
        case 0: {
            // --- GAME OVER ---
            if (ctx.isTimeOut) {
                if (ctx.conquestWinner != TEAM_NEUTRAL) {
                    display.setTextSize(2);
                    const char* t1 = TEAM_NAMES[ctx.conquestWinner - 1];
                    uint8_t x = (SCREEN_WIDTH - (strlen(t1) * 12)) / 2;
                    display.setCursor(x, 15);
                    display.print(t1);
                    const char* t2 = "WINS!";
                    x = (SCREEN_WIDTH - (strlen(t2) * 12)) / 2;
                    display.setCursor(x, 33);
                    display.print(t2);
                    display.setTextSize(1);
                    const char* t3 = "by Conquest";
                    x = (SCREEN_WIDTH - (strlen(t3) * 6)) / 2;
                    display.setCursor(x, 52);
                    display.print(t3);
                } else {
                    display.setTextSize(2);
                    const char* t1 = "TIME OUT";
                    uint8_t x = (SCREEN_WIDTH - (strlen(t1) * 12)) / 2;
                    display.setCursor(x, 20);
                    display.print(t1);
                    display.setTextSize(1);
                    const char* t2 = "Press BLUE to";
                    x = (SCREEN_WIDTH - (strlen(t2) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(t2);
                    const char* t3 = "check the score ...";
                    x = (SCREEN_WIDTH - (strlen(t3) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(t3);
                }
                break;
            }
            // --- SECTOR UNIT ---
            if (ctx.selectedMode == 0) {
                char buf[25];
                snprintf(buf, sizeof(buf), "Sector %s", UNIT_NAMES[UNIT_ID - 1]);
                uint8_t x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
                display.setCursor(x, 15);
                display.print(buf);
                if (ctx.sectorOwner == TEAM_NEUTRAL) {
                    const char* s = "NEUTRAL";
                    x = (SCREEN_WIDTH - (strlen(s) * 6)) / 2;
                    display.setCursor(x, 27);
                    display.print(s);
                    const char* l3 = "Hold any team button";
                    x = (SCREEN_WIDTH - (strlen(l3) * 6)) / 2;
                    display.setCursor(x, 41);
                    display.print(l3);
                    const char* l4 = "to capture sector!";
                    x = (SCREEN_WIDTH - (strlen(l4) * 6)) / 2;
                    display.setCursor(x, 51);
                    display.print(l4);
                } else {
                    char held[25];
                    snprintf(held, sizeof(held), "Held by: %s", TEAM_NAMES[ctx.sectorOwner - 1]);
                    x = (SCREEN_WIDTH - (strlen(held) * 6)) / 2;
                    display.setCursor(x, 27);
                    display.print(held);
                    uint32_t el = millis() - ctx.captureStartTime;
                    uint8_t h = el / 3600000;
                    uint8_t m = (el % 3600000) / 60000;
                    uint8_t s = (el % 60000) / 1000;
                    char timeBuf[20];
                    if (h > 0)
                        snprintf(timeBuf, sizeof(timeBuf), "%uh %02umin %02usec", h, m, s);
                    else if (m > 0)
                        snprintf(timeBuf, sizeof(timeBuf), "%umin %02usec", m, s);
                    else
                        snprintf(timeBuf, sizeof(timeBuf), "%u seconds", s);
                    x = (SCREEN_WIDTH - (strlen(timeBuf) * 6)) / 2;
                    display.setCursor(x, 41);
                    display.print(timeBuf);
                    if (ctx.winCondition == WIN_BY_CONQUEST) {
                        const char* ct = "Capture all sectors!";
                        x = (SCREEN_WIDTH - (strlen(ct) * 6)) / 2;
                        display.setCursor(x, 51);
                        display.print(ct);
                    } else {
                        uint32_t totalMin = el / 60000;
                        uint32_t bonus = (ctx.bonusIntervalMinutes > 0) ? (totalMin / ctx.bonusIntervalMinutes) : 0;
                        char ptsStr[10];
                        snprintf(ptsStr, sizeof(ptsStr), "%u", ctx.currentCapturePoints);
                        uint8_t pw = strlen(ptsStr) * 6;
                        char bonusStr[20] = "";
                        if (ctx.bonusIntervalMinutes > 0) snprintf(bonusStr, sizeof(bonusStr), " / Bonus +%u", bonus);
                        uint8_t bw = strlen(bonusStr) * 6;
                        uint8_t totalW = pw + 2 + 7 + bw;
                        uint8_t sx = (SCREEN_WIDTH - totalW) / 2;
                        display.setCursor(sx, 53);
                        display.print(ptsStr);
                        display.drawBitmap(sx + pw + 2, 53, POINT_BMP, 7, 7, SSD1306_WHITE);
                        if (bw > 0) {
                            display.setCursor(sx + pw + 9, 53);
                            display.print(bonusStr);
                        }
                    }
                }
            }
            // --- BOMB UNIT ---
            else if (ctx.selectedMode == 1) {
                if (ctx.isCooldownActive) {
                    uint32_t el = millis() - ctx.cooldownStartTime;
                    uint32_t rem = ctx.cooldownMs - el;
                    uint8_t m = rem / 60000;
                    uint8_t s = (rem % 60000) / 1000;
                    char timeBuf[10];
                    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", m, s);
                    display.setTextSize(2);
                    uint8_t x = (SCREEN_WIDTH - (strlen(timeBuf) * 12)) / 2;
                    display.setCursor(x, 20);
                    display.print(timeBuf);
                    display.setTextSize(1);
                    const char* l1 = "Bomb is cooling down";
                    x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(l1);
                    const char* l2 = "Come back later ...";
                    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(l2);
                } else if (ctx.isBombArmed) {
                    uint32_t el = millis() - ctx.bombPlantTime;
                    uint32_t rem = ctx.bombTimerMs - el;
                    uint8_t m = rem / 60000;
                    uint8_t s = (rem % 60000) / 1000;
                    uint8_t ms = (rem % 1000) / 10;
                    char timeBuf[12];
                    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", m, s, ms);
                    display.setTextSize(2);
                    uint8_t x = (SCREEN_WIDTH - (strlen(timeBuf) * 12)) / 2;
                    display.setCursor(x, 20);
                    display.print(timeBuf);
                    display.setTextSize(1);
                    const char* l1 = "Hold any button to";
                    x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(l1);
                    const char* l2 = "defuse the BOMB!";
                    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(l2);
                } else {
                    display.setTextSize(2);
                    const char* s = "UNARMED";
                    uint8_t x = (SCREEN_WIDTH - (strlen(s) * 12)) / 2;
                    display.setCursor(x, 20);
                    display.print(s);
                    display.setTextSize(1);
                    const char* l1 = "Hold any team button";
                    x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(l1);
                    const char* l2 = "to plant the bomb!";
                    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(l2);
                }
            }
            // --- RESPAWN UNIT ---
            else if (ctx.selectedMode == 2) {
                bool limitReached = (ctx.teamMaxRespawns[ctx.respawnTeam - 1] > 0 && ctx.teamKills[ctx.respawnTeam - 1] >= ctx.teamMaxRespawns[ctx.respawnTeam - 1]);
                if (ctx.queueCount > 0) {
                    display.setTextSize(2);
                    uint32_t rem = 0;
                    if (ctx.respawnQueue[0] > millis()) rem = (ctx.respawnQueue[0] - millis()) / 1000;
                    char timeBuf[10];
                    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", rem / 60, rem % 60);
                    uint8_t x = (SCREEN_WIDTH - (strlen(timeBuf) * 12)) / 2;
                    display.setCursor(x, 18);
                    display.print(timeBuf);
                    display.setTextSize(1);
                    char qBuf[20];
                    if (ctx.queueCount == 1)
                        strcpy(qBuf, "Queue: 1 player");
                    else
                        snprintf(qBuf, sizeof(qBuf), "Queue: %u players", ctx.queueCount);
                    x = (SCREEN_WIDTH - (strlen(qBuf) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(qBuf);
                    char pBuf[25];
                    if (ctx.respawnPenaltyPoints > 0)
                        snprintf(pBuf, sizeof(pBuf), "-%u points / death", ctx.respawnPenaltyPoints);
                    else
                        strcpy(pBuf, "Without penalties!");
                    x = (SCREEN_WIDTH - (strlen(pBuf) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(pBuf);
                } else if (limitReached) {
                    display.setTextSize(2);
                    const char* tn = TEAM_NAMES[ctx.respawnTeam - 1];
                    uint8_t x = (SCREEN_WIDTH - (strlen(tn) * 12)) / 2;
                    display.setCursor(x, 18);
                    display.print(tn);
                    display.setTextSize(1);
                    const char* l1 = "LIMIT REACHED!";
                    x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(l1);
                    const char* l2 = "No more respawns ...";
                    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(l2);
                } else {
                    display.setTextSize(2);
                    const char* tn = TEAM_NAMES[ctx.respawnTeam - 1];
                    uint8_t x = (SCREEN_WIDTH - (strlen(tn) * 12)) / 2;
                    display.setCursor(x, 18);
                    display.print(tn);
                    display.setTextSize(1);
                    const char* qb = "Queue: 0 players";
                    x = (SCREEN_WIDTH - (strlen(qb) * 6)) / 2;
                    display.setCursor(x, 42);
                    display.print(qb);
                    char pBuf[25];
                    if (ctx.respawnPenaltyPoints > 0)
                        snprintf(pBuf, sizeof(pBuf), "-%u points / death", ctx.respawnPenaltyPoints);
                    else
                        strcpy(pBuf, "Without penalties!");
                    x = (SCREEN_WIDTH - (strlen(pBuf) * 6)) / 2;
                    display.setCursor(x, 54);
                    display.print(pBuf);
                }
            }
            break;
        }
        // ====================================================
        // PAGINA 2 — SCORURI
        // ====================================================
        case 1: {
            Team localOwner = TEAM_NEUTRAL;
            if (ctx.selectedMode == 0)
                localOwner = ctx.sectorOwner;
            else if (ctx.selectedMode == 1 && ctx.isBombArmed)
                localOwner = ctx.sectorOwner;
            else if (ctx.selectedMode == 2)
                localOwner = ctx.respawnTeam;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t y = 15 + (i * 14);
                display.setCursor(0, y);
                if (localOwner == (Team)(i + 1)) {
                    display.drawBitmap(0, y, ARROW_RIGHT, 5, 7, SSD1306_WHITE);
                    display.setCursor(12, y);
                } else {
                    display.print("- ");
                }
                display.print(TEAM_NAMES[i]);
                int32_t displayed = ctx.liveScore[i] - ctx.appliedPenalties[i];
                if (displayed < 0) displayed = 0;
                char ptsBuf[15];
                snprintf(ptsBuf, sizeof(ptsBuf), "%ld", displayed);
                uint8_t tw = strlen(ptsBuf) * 6;
                uint8_t x = SCREEN_WIDTH - tw - 2 - 7;
                display.setCursor(x, y);
                display.print(ptsBuf);
                display.drawBitmap(x + tw + 2, y, POINT_BMP, 7, 7, SSD1306_WHITE);
            }
            break;
        }
        // ====================================================
        // PAGINA 3 — KILL-URI
        // ====================================================
        case 2: {
            Team localOwner = TEAM_NEUTRAL;
            if (ctx.selectedMode == 0)
                localOwner = ctx.sectorOwner;
            else if (ctx.selectedMode == 1 && ctx.isBombArmed)
                localOwner = ctx.sectorOwner;
            else if (ctx.selectedMode == 2)
                localOwner = ctx.respawnTeam;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t y = 15 + (i * 14);
                display.setCursor(0, y);
                if (localOwner == (Team)(i + 1)) {
                    display.drawBitmap(0, y, ARROW_RIGHT, 5, 7, SSD1306_WHITE);
                    display.setCursor(12, y);
                } else {
                    display.print("- ");
                }
                display.print(TEAM_NAMES[i]);
                uint32_t total = ctx.globalKills[0][i];
                char kBuf[15];
                if (ctx.teamMaxRespawns[i] > 0)
                    snprintf(kBuf, sizeof(kBuf), "%u/%u", total, ctx.teamMaxRespawns[i]);
                else
                    snprintf(kBuf, sizeof(kBuf), "%u", total);
                uint8_t tw = strlen(kBuf) * 6;
                uint8_t x = SCREEN_WIDTH - tw - 2 - 7;
                display.setCursor(x, y);
                display.print(kBuf);
                display.drawBitmap(x + tw + 2, y, SKULL_BMP, 7, 7, SSD1306_WHITE);
            }
            break;
        }
        // ====================================================
        // PAGINA 4 — STATUS UNITATI
        // ====================================================
        case 3: {
            uint32_t now = millis();
            bool showTime = ((now / 3000) % 2 == 1);
            // Struct temporar pentru sortare
            struct UnitRow {
                uint8_t id;
                uint8_t sortMode;
                char status[15];
            } rows[MAX_UNITS];
            uint8_t count = 0;
            for (uint8_t i = 0; i < MAX_UNITS; i++) {
                uint8_t m = ctx.globalUnitMode[i];
                Team t = ctx.globalUnitStatus[i];
                bool online = (ctx.lastSeenTime[i] > 0 && (now - ctx.lastSeenTime[i] <= 600000)) || (i == UNIT_ID - 1);
                if (!online) continue;
                rows[count].id = i;
                rows[count].sortMode = (m == 0) ? 4 : m;
                // Calcul timp pentru afisare
                char timeStr[15] = "";
                bool hasTime = false;
                if (ctx.globalEventTime[i] > 0) {
                    uint32_t el = (now - ctx.globalEventTime[i]) / 1000;
                    uint32_t tgt = 0;
                    bool active = true;
                    if (m == 1 && t != TEAM_NEUTRAL) {
                        tgt = el;  // Sector — timp scurs
                    } else if (m == 2 && t == TEAM_PLANTED) {
                        uint32_t tot = ctx.bombTimerMs / 1000;
                        if (el < tot)
                            tgt = tot - el;
                        else
                            active = false;
                    } else if (m == 2 && t == TEAM_NEUTRAL) {
                        uint32_t tot = ctx.cooldownMs / 1000;
                        if (el < tot)
                            tgt = tot - el;
                        else
                            active = false;
                    }
                    if (active) {
                        hasTime = true;
                        uint32_t disp = (m == 2) ? tgt + 59 : tgt;
                        uint32_t mm = disp / 60, hh = mm / 60;
                        mm = mm % 60;
                        if (tgt < 60)
                            strcpy(timeStr, "<1min");
                        else if (hh > 0)
                            snprintf(timeStr, sizeof(timeStr), "%uh %umin", hh, mm);
                        else
                            snprintf(timeStr, sizeof(timeStr), "%umin", mm);
                    }
                }
                // Text status final
                if (m == 1) {
                    if (t == TEAM_NEUTRAL)
                        strcpy(rows[count].status, "Neutral");
                    else if (showTime && hasTime)
                        strcpy(rows[count].status, timeStr);
                    else
                        strcpy(rows[count].status, TEAM_NAMES[t - 1]);
                } else if (m == 2) {
                    if (t == TEAM_PLANTED) {
                        if (showTime && hasTime)
                            strcpy(rows[count].status, timeStr);
                        else
                            strcpy(rows[count].status, "Planted");
                    } else {
                        if (hasTime)
                            strcpy(rows[count].status, showTime ? timeStr : "Cooldown");
                        else
                            strcpy(rows[count].status, "Bomb");
                    }
                } else if (m == 3) {
                    if (t == TEAM_NEUTRAL)
                        strcpy(rows[count].status, "Neutral");
                    else
                        snprintf(rows[count].status, 15, "R:%s", TEAM_NAMES[t - 1]);
                } else {
                    strcpy(rows[count].status, "Unknown");
                }
                count++;
            }
            // Bubble sort dupa mod, apoi ID
            for (uint8_t i = 0; i < count; i++)
                for (uint8_t j = i + 1; j < count; j++)
                    if (rows[i].sortMode > rows[j].sortMode || (rows[i].sortMode == rows[j].sortMode && rows[i].id > rows[j].id)) {
                        UnitRow tmp = rows[i];
                        rows[i] = rows[j];
                        rows[j] = tmp;
                    }
                    uint8_t scroll = ctx.page4ScrollIndex;
                if (count > 0 && scroll > count - 1) scroll = 0;
                if (count == 0) {
                    display.setCursor(10, 30);
                    display.print("No active units ...");
                } else {
                    uint8_t rightMargin = (count > 4) ? 3 : 0;
                    uint8_t shown = 0;
                    for (uint8_t i = scroll; i < count && shown < 4; i++) {
                        uint8_t y = 15 + (shown * 14);
                        display.setCursor(0, y);
                        if (rows[i].id == UNIT_ID - 1) {
                            display.drawBitmap(0, y, ARROW_RIGHT, 5, 7, SSD1306_WHITE);
                            display.setCursor(12, y);
                        } else {
                            display.print("- ");
                        }
                        display.print(UNIT_NAMES[rows[i].id]);
                        uint8_t tw = strlen(rows[i].status) * 6;
                        display.setCursor(SCREEN_WIDTH - tw - rightMargin, y);
                        display.print(rows[i].status);
                        shown++;
                    }
                    drawScrollbar(count, 4, scroll, 13, 51);
                }
                break;
        }
        // ====================================================
        // PAGINA 5 — PING RADAR
        // ====================================================
        case 4: {
            uint32_t now = millis();
            bool showBat = ((now / 3000) % 2 == 1);
            uint8_t activeUnits[MAX_UNITS];
            uint8_t count = 0;
            for (uint8_t i = 0; i < MAX_UNITS; i++) {
                bool online = (ctx.lastSeenTime[i] > 0 && (now - ctx.lastSeenTime[i] <= 600000)) || (i == UNIT_ID - 1);
                if (online) activeUnits[count++] = i;
            }
            uint8_t scroll = ctx.page5ScrollIndex;
            if (count > 0 && scroll > count - 1) scroll = 0;
            if (count == 0) {
                display.setCursor(10, 30);
                display.print("No active units ...");
            } else {
                uint8_t rightMargin = (count > 4) ? 3 : 0;
                uint8_t shown = 0;
                for (uint8_t i = scroll; i < count && shown < 4; i++) {
                    uint8_t uid = activeUnits[i];
                    uint8_t y = 15 + (shown * 14);
                    display.setCursor(0, y);
                    if (uid == UNIT_ID - 1) {
                        display.drawBitmap(0, y, ARROW_RIGHT, 5, 7, SSD1306_WHITE);
                        display.setCursor(12, y);
                    } else {
                        display.print("- ");
                    }
                    display.print(UNIT_NAMES[uid]);
                    // Calcul text sync
                    char syncText[15];
                    bool isOffline = false;
                    if (ctx.lastSeenTime[uid] == 0) {
                        strcpy(syncText, "--");
                        isOffline = true;
                    } else {
                        uint32_t el = (now - ctx.lastSeenTime[uid]) / 1000;
                        if (el >= 70) {
                            strcpy(syncText, "OFFLINE");
                            isOffline = true;
                        } else
                            snprintf(syncText, sizeof(syncText), "%u sec", el);
                    }
                    if (showBat) {
                        // Bateria se afiseaza mereu la randul ei, online sau offline
                        uint8_t lv = ctx.globalBattery[uid];
                        uint8_t batX = SCREEN_WIDTH - 21 - rightMargin;
                        uint8_t batY = y - 1;
                        display.fillRect(batX, batY + 2, 2, 5, SSD1306_WHITE);
                        display.drawRect(batX + 2, batY, 19, 9, SSD1306_WHITE);
                        for (uint8_t b = 0; b < lv; b++) {
                            uint8_t bx = (batX + 16) - (b * 4);
                            display.fillRect(bx, batY + 2, 3, 5, SSD1306_WHITE);
                        }
                    } else {
                        // Textul: secundele de la ultimul pachet sau OFFLINE
                        uint8_t tw = strlen(syncText) * 6;
                        display.setCursor(SCREEN_WIDTH - tw - rightMargin, y);
                        display.print(syncText);
                    }
                    shown++;
                }
                drawScrollbar(count, 4, scroll, 13, 51);
            }
            break;
        }
        // ====================================================
        // PAGINA 6 — INFO JOC
        // ====================================================
        case 5: {
            const char* line1 = "Winning Condition";
            const char* line2;
            if (ctx.winCondition == WIN_BY_POINTS)
                line2 = "By Points";
            else if (ctx.winCondition == WIN_BY_CONQUEST)
                line2 = "By Conquest";
            else
                line2 = "By Any";
            const char* line3 = "Time Left";
            char line4[25];
            if (ctx.isTimeOut) {
                strcpy(line4, "GAME OVER!");
            } else if (ctx.winCondition == WIN_BY_CONQUEST) {
                strcpy(line4, "No time limit!");
            } else {
                if (!ctx.isGameTimerRunning && ctx.gameTimeLeftSeconds == 0) {
                    strcpy(line4, "No time limit!");
                } else if (!ctx.isGameTimerRunning && ctx.gameTimeLeftSeconds > 0) {
                    strcpy(line4, "YELLOW to START");
                } else {
                    uint8_t h = ctx.gameTimeLeftSeconds / 3600;
                    uint8_t m = (ctx.gameTimeLeftSeconds % 3600) / 60;
                    uint8_t s = ctx.gameTimeLeftSeconds % 60;
                    if (h > 0)
                        snprintf(line4, sizeof(line4), "%uh %02umin %02usec", h, m, s);
                    else if (m > 0)
                        snprintf(line4, sizeof(line4), "%umin %02usec", m, s);
                    else
                        snprintf(line4, sizeof(line4), "%usec", s);
                }
            }
            uint8_t x;
            x = (SCREEN_WIDTH - (strlen(line1) * 6)) / 2;
            display.setCursor(x, 15);
            display.print(line1);
            x = (SCREEN_WIDTH - (strlen(line2) * 6)) / 2;
            display.setCursor(x, 27);
            display.print(line2);
            x = (SCREEN_WIDTH - (strlen(line3) * 6)) / 2;
            display.setCursor(x, 41);
            display.print(line3);
            x = (SCREEN_WIDTH - (strlen(line4) * 6)) / 2;
            display.setCursor(x, 53);
            display.print(line4);
            break;
        }
    }
    display.display();
}
void drawAdminMenu(uint8_t menuIndex, uint8_t scrollIndex, int8_t selectedMode) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(32, 0);
    display.print("Admin Mode");
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);

    const char* const items[7] = {"Game Settings", "Bomb Parameters", "Respawn Rules", "Sync Units", "TAG Writer", "Change Mode", "System Restart"};

    uint8_t shown = 0;
    for (uint8_t i = scrollIndex; i < 7; i++) {
        if (shown >= 5) break;

        // Ascundem Change Mode (index 5) daca nu avem mod selectat
        if (i == 5 && selectedMode == -1) continue;

        display.setCursor(0, 14 + (shown * 10));

        if (i == menuIndex) {
            display.drawBitmap(0, 14 + (shown * 10), ARROW_RIGHT, 5, 7, SSD1306_WHITE);
            display.setCursor(12, 14 + (shown * 10));
        } else {
            display.print("  ");
        }

        display.print(items[i]);
        shown++;
    }

    uint8_t total = (selectedMode == -1) ? 6 : 7;
    drawScrollbar(total, 5, scrollIndex, 13, 51);
    display.display();
}
void drawAdminPages(const AdminContext& ac) {
    display.clearDisplay();
    display.setTextSize(1);
    const char* const items[7] = {"Game Settings", "Bomb Parameters", "Respawn Rules", "Sync Units", "TAG Writer", "Change Mode", "System Restart"};
    if (ac.selectedPage == 0) {
        // --- GAME SETTINGS ---
        const char* const wcT[] = {"By Points", "By Conquest", "By Any"};
        const char* const tlT[] = {"No limit", "10 sec", "1 Hour", "2 Hours", "3 Hours", "4 Hours", "5 Hours", "6 Hours", "7 Hours", "8 Hours", "9 Hours", "10 Hours", "11 Hours", "12 Hours", "24 Hours"};
        const char* const bT[] = {"No bonus", "15 min", "30 min", "1 Hour", "2 Hours", "3 Hours", "4 Hours"};
        char buf[30];
        uint8_t x;
        int yOff = (ac.gsIndex >= 2) ? -27 * (ac.gsIndex - 1) : 0;
        x = (SCREEN_WIDTH - (17 * 6)) / 2;
        display.setCursor(x, 14 + yOff);
        display.print("Winning Condition");
        snprintf(buf, sizeof(buf), ac.gsIndex == 0 ? "> %s <" : "%s", wcT[ac.gsWinCond]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 24 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (10 * 6)) / 2;
        display.setCursor(x, 41 + yOff);
        display.print("Time Limit");
        snprintf(buf, sizeof(buf), ac.gsIndex == 1 ? "> %s <" : "%s", tlT[ac.gsTimeLimit]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 51 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (14 * 6)) / 2;
        display.setCursor(x, 68 + yOff);
        display.print("Bonus Interval");
        snprintf(buf, sizeof(buf), ac.gsIndex == 2 ? "> %s <" : "%s", bT[ac.gsBonus]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 78 + yOff);
        display.print(buf);
        snprintf(buf, sizeof(buf), ac.gsIndex == 3 ? "> CONFIRM <" : "CONFIRM");
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 95 + yOff);
        display.print(buf);
        drawScrollbar(4, 1, ac.gsIndex, 13, 51);
    } else if (ac.selectedPage == 1) {
        // --- BOMB SETTINGS ---
        const char* const tT[] = {"5 min", "10 min", "15 min", "20 min", "30 min", "45 min", "1 hour", "2 hours"};
        const char* const pT[] = {"50 pts", "100 pts", "200 pts", "300 pts", "400 pts", "500 pts", "600 pts", "700 pts", "800 pts", "900 pts", "1000 pts", "1500 pts", "2000 pts", "2500 pts", "3000 pts"};
        char buf[30];
        uint8_t x;
        int yOff = (ac.bsIndex >= 2) ? -27 * (ac.bsIndex - 1) : 0;
        x = (SCREEN_WIDTH - (10 * 6)) / 2;
        display.setCursor(x, 14 + yOff);
        display.print("Bomb Timer");
        snprintf(buf, sizeof(buf), ac.bsIndex == 0 ? "> %s <" : "%s", tT[ac.bsTimerIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 24 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (13 * 6)) / 2;
        display.setCursor(x, 41 + yOff);
        display.print("Bomb Cooldown");
        snprintf(buf, sizeof(buf), ac.bsIndex == 1 ? "> %s <" : "%s", tT[ac.bsCooldownIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 51 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (16 * 6)) / 2;
        display.setCursor(x, 68 + yOff);
        display.print("Explosion Points");
        snprintf(buf, sizeof(buf), ac.bsIndex == 2 ? "> %s <" : "%s", pT[ac.bsExpPtsIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 78 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (15 * 6)) / 2;
        display.setCursor(x, 95 + yOff);
        display.print("Defusing Points");
        snprintf(buf, sizeof(buf), ac.bsIndex == 3 ? "> %s <" : "%s", pT[ac.bsDefPtsIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 105 + yOff);
        display.print(buf);
        snprintf(buf, sizeof(buf), ac.bsIndex == 4 ? "> CONFIRM <" : "CONFIRM");
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 122 + yOff);
        display.print(buf);
        drawScrollbar(5, 1, ac.bsIndex, 13, 51);
    } else if (ac.selectedPage == 2) {
        // --- RESPAWN SETTINGS ---
        const char* const tT[] = {"10 sec", "30 sec", "1 min", "2 min", "3 min", "4 min", "5 min", "10 min", "15 min", "20 min", "25 min", "30 min"};
        const char* const pT[] = {"No penalty", "5 pts", "10 pts", "25 pts", "50 pts", "75 pts", "100 pts"};
        const char* const rT[] = {"Unlimited", "10", "25", "50", "75", "100", "200", "300", "400", "500", "1000"};
        char buf[30];
        uint8_t x;
        int yOff = (ac.rsIndex >= 2) ? -27 * (ac.rsIndex - 1) : 0;
        x = (SCREEN_WIDTH - (12 * 6)) / 2;
        display.setCursor(x, 14 + yOff);
        display.print("Respawn Time");
        snprintf(buf, sizeof(buf), ac.rsIndex == 0 ? "> %s <" : "%s", tT[ac.rsTimeIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 24 + yOff);
        display.print(buf);
        x = (SCREEN_WIDTH - (14 * 6)) / 2;
        display.setCursor(x, 41 + yOff);
        display.print("Penalty Points");
        snprintf(buf, sizeof(buf), ac.rsIndex == 1 ? "> %s <" : "%s", pT[ac.rsPenaltyIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 51 + yOff);
        display.print(buf);
        for (uint8_t i = 0; i < 4; i++) {
            char label[20];
            snprintf(label, sizeof(label), "%s resp.", TEAM_NAMES[i]);
            x = (SCREEN_WIDTH - (strlen(label) * 6)) / 2;
            display.setCursor(x, 68 + (i * 27) + yOff);
            display.print(label);
            snprintf(buf, sizeof(buf), ac.rsIndex == i + 2 ? "> %s <" : "%s", rT[ac.rsLimitIdx[i]]);
            x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
            display.setCursor(x, 78 + (i * 27) + yOff);
            display.print(buf);
        }
        snprintf(buf, sizeof(buf), ac.rsIndex == 6 ? "> CONFIRM <" : "CONFIRM");
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 176 + yOff);
        display.print(buf);
        drawScrollbar(7, 1, ac.rsIndex, 13, 51);
    } else if (ac.selectedPage == 3) {
        // --- TAG WRITER ---
        const char* const optT[] = {"+50 pts", "+100 pts", "+150 pts", "+200 pts", "+250 pts", "+500 pts", "+750 pts", "+1000 pts", "+1500 pts", "+2000 pts", "Admin Tag"};
        char buf[25];
        uint8_t x;
        x = (SCREEN_WIDTH - (13 * 6)) / 2;
        display.setCursor(x, 20);
        display.print("Select Option");
        snprintf(buf, sizeof(buf), ac.twIndex == 0 ? "> %s <" : "%s", optT[ac.twOptionIdx]);
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 32);
        display.print(buf);
        snprintf(buf, sizeof(buf), ac.twIndex == 1 ? "> WRITE <" : "WRITE");
        x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
        display.setCursor(x, 50);
        display.print(buf);
    }
    // Header peste continut (tehnica din codul vechi)
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_BLACK);
    display.setCursor(0, 0);
    uint8_t hx = (SCREEN_WIDTH - (strlen(items[ac.selectedPage]) * 6)) / 2;
    display.setCursor(hx, 0);
    display.print(items[ac.selectedPage]);
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.display();
}
void drawAdminSaved() {
    display.clearDisplay();
    display.setTextSize(2);
    const char* msg = "SAVED";
    uint8_t x = (SCREEN_WIDTH - (strlen(msg) * 12)) / 2;
    display.setCursor(x, 24);
    display.print(msg);
    display.display();
}
void drawTagWriter(uint8_t statusMsg) {
    display.clearDisplay();
    display.setTextSize(1);
    if (statusMsg == 0) {
        // Asteapta card
        const char* l1 = "READY TO WRITE";
        uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
        display.setCursor(x, 24);
        display.print(l1);
        const char* l2 = "Tap the card...";
        x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
        display.setCursor(x, 36);
        display.print(l2);
    } else {
        const char* big = "";
        const char* small = "";
        if (statusMsg == 1) {
            big = "SUCCESS!";
            small = "Card saved";
        } else if (statusMsg == 2) {
            big = "SUCCESS!";
            small = "Card overwritten";
        } else if (statusMsg == 3) {
            big = "SUCCESS!";
            small = "Admin tag saved";
        } else if (statusMsg == 4) {
            big = "REVOKED";
            small = "Card is now empty";
        } else if (statusMsg == 5) {
            big = "DENIED";
            small = "Card is Admin tag!";
        } else if (statusMsg == 6) {
            big = "TIMEOUT";
            small = "No card detected";
        } else if (statusMsg == 7) {
            big = "ERROR!";
            small = "Read/Write failed";
        }
        display.setTextSize(2);
        uint8_t x = (SCREEN_WIDTH - (strlen(big) * 12)) / 2;
        display.setCursor(x, 16);
        display.print(big);
        display.setTextSize(1);
        x = (SCREEN_WIDTH - (strlen(small) * 6)) / 2;
        display.setCursor(x, 40);
        display.print(small);
    }
    display.display();
}
void drawBonusScreen(uint16_t points) {
    display.clearDisplay();
    display.setTextSize(2);
    const char* l1 = "BONUS!";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 12)) / 2;
    display.setCursor(x, 16);
    display.print(l1);
    display.setTextSize(1);
    char buf[20];
    snprintf(buf, sizeof(buf), "+%u points", points);
    x = (SCREEN_WIDTH - (strlen(buf) * 6)) / 2;
    display.setCursor(x, 40);
    display.print(buf);
    const char* l3 = "Congratulations!";
    x = (SCREEN_WIDTH - (strlen(l3) * 6)) / 2;
    display.setCursor(x, 52);
    display.print(l3);
    display.display();
}
void drawWaitAdminTag() {
    display.clearDisplay();
    display.setTextSize(1);

    const char* l1 = "Please present";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
    display.setCursor(x, 24);
    display.print(l1);

    const char* l2 = "Admin tag ...";
    x = (SCREEN_WIDTH - (strlen(l2) * 6)) / 2;
    display.setCursor(x, 36);
    display.print(l2);

    display.display();
}

void drawLoadingScreen(uint32_t elapsed, uint32_t totalMs) {
    display.clearDisplay();

    // "LOADING ..." centrat vertical in jumatatea de sus
    display.setTextSize(1);
    const char* msg = "LOADING ...";
    uint8_t x = (SCREEN_WIDTH - (strlen(msg) * 6)) / 2;
    display.setCursor(x, 22);
    display.print(msg);

    // Bara de progres — contur
    display.drawRect(14, 35, 100, 10, SSD1306_WHITE);

    // Umplere fluida
    uint8_t barW = (uint8_t)((uint32_t)96 * elapsed / totalMs);
    if (barW > 96) barW = 96;
    if (barW > 0) display.fillRect(16, 37, barW, 6, SSD1306_WHITE);

    display.display();
}

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

void drawReadyScreen(int8_t selectedMode) {
    display.clearDisplay();

    const char* l1 = "System Ready ...";
    uint8_t x = (SCREEN_WIDTH - (strlen(l1) * 6)) / 2;
    display.setTextSize(1);
    display.setCursor(x, 10);
    display.print(l1);

    const char* l2 = "GOOD LUCK!";
    display.setTextSize(2);
    x = (SCREEN_WIDTH - (strlen(l2) * 12)) / 2;
    display.setCursor(x, 24);
    display.print(l2);

    const char* l3 = selectedMode == 0 ? "Sector Unit" :
    selectedMode == 1 ? "Bomb Unit"   :
    "Respawn Unit";
    display.setTextSize(1);
    x = (SCREEN_WIDTH - (strlen(l3) * 6)) / 2;
    display.setCursor(x, 50);
    display.print(l3);

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
