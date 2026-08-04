#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Jedna pozycja na ekranie wyboru. Launcher buduje te liste w czasie uruchomienia,
// wiec liczba kart nie jest znana w czasie kompilacji.
struct ChooserItem {
    std::wstring title;
    std::wstring detail;
    bool         isDefault = false;  // karta z odznaka "DOMYSLNY"
};

// Pokazuje okno wyboru konta i czeka na decyzje uzytkownika.
// Zwraca indeks wybranej pozycji albo kNoChoice, gdy okno zamknieto bez wyboru.
constexpr int kNoChoice = -1;
int ShowChooser(HINSTANCE instance, const std::vector<ChooserItem>& items);

// Dopisuje znaczony czasem wiersz do
// %LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log.
// Zdefiniowana w main.cpp, uzywana tez z chooser.cpp - dzieki temu diagnoza awarii
// (ktora strategia zawiodla, z jakim kodem) nie wymaga pytania uzytkownika, co
// dokladnie pokazalo okno bledu.
void WriteLog(const std::wstring& message);
