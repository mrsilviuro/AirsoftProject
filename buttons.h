#pragma once
#include <Arduino.h>

#include "config.h"

extern void refreshLEDs();

// ============================================================
// buttons.h — Gestionare butoane (debounce, short/long press)
// ============================================================

// ============================================================
// Initializare
// ============================================================

// Apelata o singura data in setup(). Seteaza pinii butoanelor.
void buttonsInit();

// ============================================================
// Update — apelata in fiecare loop()
// ============================================================

// Citeste starea butoanelor, aplica debounce si detecteaza
// apasarile scurte si lungi. Apeleaza callback-urile de mai jos.
void handleButtons();

// ============================================================
// Stare butoane — accesibila din afara (ex: handleActionProgress)
// ============================================================

// Returneaza true daca butonul btnIndex este tinut apasat fizic.
bool isButtonHeld(uint8_t btnIndex);

// ============================================================
// Callback-uri — definite in .ino, apelate din buttons.cpp
// ============================================================

// Apelat la o apasare scurta (<1 sec). btnIndex: 0=Rosu, 1=Albastru, 2=Verde, 3=Galben.
void onShortPress(uint8_t btnIndex);

// Apelat O SINGURA DATA cand o apasare depaseste LONG_PRESS_MS.
// btnIndex: butonul tinut. Porneste o actiune de gameplay.
void onLongPress(uint8_t btnIndex);

// Apelat cand combo-ul Admin (Rosu + Albastru >= 3 sec) este detectat.
void onAdminCombo();
