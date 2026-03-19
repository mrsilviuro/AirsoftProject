#include "buttons.h"

// ============================================================
// State intern (static = invizibil in afara acestui fisier)
// ============================================================
static bool buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
static bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
static uint32_t lastDebounceTime[4] = {0, 0, 0, 0};
static uint32_t buttonPressTime[4] = {0, 0, 0, 0};
static bool longPressHandled[4] = {false, false, false, false};

// --- Combo Admin (Rosu + Albastru tinut 3 secunde) ---
static uint32_t adminComboStartTime = 0;
static bool adminComboActive = false;
static bool adminComboHandled = false;

// ============================================================
// buttonsInit()
// ============================================================
void buttonsInit() {
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(PIN_BTNS[i], INPUT_PULLUP);
    }
}

// ============================================================
// isButtonHeld()
// ============================================================
bool isButtonHeld(uint8_t btnIndex) { return buttonState[btnIndex] == LOW; }

// ============================================================
// handleButtons()
// ============================================================
void handleButtons() {
    uint32_t now = millis();

    // --------------------------------------------------------
    // 1. DETECTARE COMBO ADMIN (Rosu + Albastru tinut 3 sec)
    //    Verificam INAINTE de logica normala ca sa avem prioritate
    // --------------------------------------------------------
    bool redHeld = (digitalRead(PIN_BTNS[0]) == LOW);
    bool blueHeld = (digitalRead(PIN_BTNS[1]) == LOW);

    if (redHeld && blueHeld) {
        if (!adminComboActive) {
            adminComboActive = true;
            adminComboHandled = false;
            adminComboStartTime = now;
        } else if (!adminComboHandled && (now - adminComboStartTime >= 3000)) {
            adminComboHandled = true;
            onAdminCombo();
        }
        // Cat timp combo-ul e activ, nu procesam butoanele individual
        return;
    } else {
        adminComboActive = false;
        adminComboHandled = false;
    }

    // --------------------------------------------------------
    // 2. LOGICA NORMALA PER BUTON
    // --------------------------------------------------------
    for (uint8_t i = 0; i < 4; i++) {
        bool reading = digitalRead(PIN_BTNS[i]);

        // Debounce: resetam timer-ul la orice schimbare de stare
        if (reading != lastButtonState[i]) {
            lastDebounceTime[i] = now;
        }

        if ((now - lastDebounceTime[i]) > DEBOUNCE_DELAY_MS) {
            // Starea s-a stabilizat — vedem daca s-a schimbat
            if (reading != buttonState[i]) {
                buttonState[i] = reading;

                if (buttonState[i] == LOW) {
                    // ---- BUTON APASAT ----
                    buttonPressTime[i] = now;
                    longPressHandled[i] = false;
                    digitalWrite(PIN_LEDS[i], HIGH);

                } else {
                    // ---- BUTON ELIBERAT ----
                    refreshLEDs();

                    uint32_t duration = now - buttonPressTime[i];

                    // Short press: eliberat inainte de pragul long press
                    if (!longPressHandled[i] && duration < LONG_PRESS_MS) {
                        onShortPress(i);
                    }
                }
            }
            // Butonul e tinut apasat — verificam long press
            else if (buttonState[i] == LOW) {
                uint32_t duration = now - buttonPressTime[i];

                if (!longPressHandled[i] && duration >= LONG_PRESS_MS) {
                    longPressHandled[i] = true;  // Declansam O SINGURA DATA
                    onLongPress(i);
                }
            }
        }

        lastButtonState[i] = reading;
    }
}
