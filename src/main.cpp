// claude-profiles - uruchamia Claude Desktop na wskazanym profilu.
//
// Claude Desktop jest dystrybuowany jako pakiet MSIX, wiec jego katalog instalacyjny
// zawiera numer wersji i zmienia sie po kazdej aktualizacji. Sciezki nie da sie
// zapisac na sztywno, a katalogu C:\Program Files\WindowsApps nie da sie wylistowac
// (ACL blokuje enumeracje). Ustalamy ja wiec przez model pakietow (appmodel.h),
// ktory dziala niezaleznie od tego, czy proces jest w kontenerze MSIX.
//
//   claude-profiles.exe            -> okno wyboru konta
//   claude-profiles.exe 1          -> konto 1 (profil domyslny, aktywacja pakietu)
//   claude-profiles.exe N          -> konto N (profil Claude-kontoN)
//   claude-profiles.exe --profile X-> dowolny profil o nazwie X
//
// Liczba kont nie jest zapisana na sztywno: launcher wykrywa istniejace profile
// i dokłada karte tworzaca kolejny. Uwaga - katalogi profili powstaja pod sciezka
// zwirtualizowana, nie tam, gdzie wskazuje --user-data-dir (patrz FINDINGS.md).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "chooser.h"

#include <appmodel.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kPackageFamilyName[] = L"Claude_pzs8sxrjxfjjc";
constexpr wchar_t kAppUserModelId[]    = L"Claude_pzs8sxrjxfjjc!Claude";
constexpr wchar_t kExeRelativePath[]   = L"app\\Claude.exe";
constexpr wchar_t kProfilePrefix[]     = L"Claude-konto";
constexpr wchar_t kTitle[]             = L"Profiles for Claude Desktop";

// Konto 1 to profil domyslny, wiec nie ma wlasnego katalogu - numeracja katalogow
// zaczyna sie od 2 (Claude-konto2).
constexpr int kFirstProfileNumber = 2;

// --- okna bledow -----------------------------------------------------------
// Tu TaskDialog jest na miejscu: to sa faktyczne komunikaty o bledzie, wiec maja
// wygladac jak komunikaty systemowe. Ekran wyboru konta rysujemy sami (chooser.cpp).
// Gdyby comctl32 v6 byl niedostepny, kazde wywolanie spada do MessageBox - lepiej
// pokazac brzydszy komunikat niz zaden.

std::wstring FormatHResult(HRESULT hr) {
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

void ShowError(PCWSTR heading, const std::wstring& detail) {
    TASKDIALOGCONFIG config = {};
    config.cbSize             = sizeof(config);
    config.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons    = TDCBF_CLOSE_BUTTON;
    config.pszWindowTitle     = kTitle;
    config.pszMainIcon        = TD_ERROR_ICON;
    config.pszMainInstruction = heading;
    config.pszContent         = detail.c_str();

    if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr))) {
        const std::wstring fallback = std::wstring(heading) + L"\n\n" + detail;
        MessageBoxW(nullptr, fallback.c_str(), kTitle, MB_ICONERROR | MB_OK);
    }
}

// --- ustalanie sciezek -----------------------------------------------------

// Rejestracja protokolu claude:// ma postac:  "C:\...\Claude.exe" "%1"
// Wyluskujemy z niej pierwszy argument, czyli sciezke do pliku wykonywalnego.
// diagnostic dostaje powod niepowodzenia - bez tego komunikat o bledzie nie mowi,
// na ktorym kroku sie wywrocilo.
std::wstring ExePathFromProtocolRegistration(std::wstring* diagnostic) {
    wchar_t value[MAX_PATH * 2] = {};
    DWORD size = sizeof(value);
    const LONG rc = RegGetValueW(HKEY_CURRENT_USER,
                                 L"Software\\Classes\\claude\\shell\\open\\command",
                                 nullptr, RRF_RT_REG_SZ, nullptr, value, &size);
    if (rc != ERROR_SUCCESS) {
        *diagnostic = L"Odczyt rejestru nie powiódł się (kod " + std::to_wstring(rc) + L").";
        return L"";
    }

    const std::wstring command(value);
    if (command.empty() || command.front() != L'"') {
        *diagnostic = L"Nieoczekiwany format wpisu w rejestrze:\n" + command;
        return L"";
    }
    const size_t closing = command.find(L'"', 1);
    if (closing == std::wstring::npos) {
        *diagnostic = L"Niedomknięty cudzysłów we wpisie rejestru:\n" + command;
        return L"";
    }
    return command.substr(1, closing - 1);
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Nazwa profilu jest doklejana do %APPDATA%, wiec nie moze z tej sciezki wyprowadzac
// ani zawierac znakow niedozwolonych w nazwie katalogu.
bool IsValidProfileName(const std::wstring& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    if (name == L"." || name == L"..") {
        return false;
    }
    return name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos;
}

struct PackageInfo {
    std::wstring fullName;  // pusty, gdy nie udalo sie odczytac z modelu pakietow
    std::wstring exePath;
};

// Podstawowe zrodlo: model pakietow. Dziala w kazdym procesie i nie zalezy od tego,
// czy aplikacja zdazyla juz zarejestrowac protokol.
PackageInfo PackageFromModel(std::wstring* diagnostic) {
    PackageInfo info;

    UINT32 count = 0;
    UINT32 bufferLength = 0;
    LONG rc = GetPackagesByPackageFamily(kPackageFamilyName, &count, nullptr, &bufferLength, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER || count == 0) {
        *diagnostic = L"nie znaleziono zainstalowanego pakietu (kod " + std::to_wstring(rc) + L")";
        return info;
    }

    std::vector<PWSTR> fullNames(count);
    std::vector<wchar_t> names(bufferLength);
    rc = GetPackagesByPackageFamily(kPackageFamilyName, &count, fullNames.data(),
                                    &bufferLength, names.data());
    if (rc != ERROR_SUCCESS || count == 0) {
        *diagnostic = L"odczyt listy pakietów nie powiódł się (kod " + std::to_wstring(rc) + L")";
        return info;
    }

    UINT32 pathLength = 0;
    rc = GetPackagePathByFullName(fullNames[0], &pathLength, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER) {
        *diagnostic = L"nie udało się ustalić katalogu pakietu (kod " + std::to_wstring(rc) + L")";
        return info;
    }

    std::wstring path(pathLength, L'\0');
    rc = GetPackagePathByFullName(fullNames[0], &pathLength, path.data());
    if (rc != ERROR_SUCCESS) {
        *diagnostic = L"odczyt katalogu pakietu nie powiódł się (kod " + std::to_wstring(rc) + L")";
        return info;
    }
    path.resize(pathLength > 0 ? pathLength - 1 : 0);  // bez koncowego NUL

    info.fullName = fullNames[0];
    info.exePath = path + L"\\" + kExeRelativePath;
    return info;
}

// Kolejnosc ma znaczenie. Rejestracja protokolu claude:// jest widoczna tylko wtedy,
// gdy proces dziala w kontenerze MSIX aplikacji - poza nim tego klucza po prostu nie
// ma. Model pakietow dziala zawsze, wiec jest zrodlem pierwszego wyboru, a rejestr
// zostaje jako awaryjny (i nie daje nazwy pakietu, tylko sciezke).
PackageInfo ResolvePackage(std::wstring* diagnostic) {
    std::wstring packageDiagnostic;
    PackageInfo info = PackageFromModel(&packageDiagnostic);
    if (!info.exePath.empty() && FileExists(info.exePath)) {
        return info;
    }

    std::wstring registryDiagnostic;
    const std::wstring fromRegistry = ExePathFromProtocolRegistration(&registryDiagnostic);
    if (!fromRegistry.empty() && FileExists(fromRegistry)) {
        return {L"", fromRegistry};
    }

    *diagnostic = L"Model pakietów: " + packageDiagnostic +
                  L"\nRejestracja protokołu: " + registryDiagnostic;
    return {};
}

std::wstring KnownFolder(REFKNOWNFOLDERID folder) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &raw))) {
        return L"";
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::wstring RoamingAppData() {
    return KnownFolder(FOLDERID_RoamingAppData);
}

// Claude dziala w kontenerze MSIX, wiec jego zapisy do %APPDATA% sa przekierowywane
// tutaj. Launcher startuje spoza kontenera i widzi sciezke rzeczywista, ktora
// w takim razie NIGDY nie powstaje - dlatego katalogow profili trzeba szukac
// w obu miejscach. Profil konta 2 lezy faktycznie pod
// ...\Packages\<pakiet>\LocalCache\Roaming\Claude-konto2, mimo ze --user-data-dir
// wskazywalo %APPDATA%\Claude-konto2. Szczegoly w FINDINGS.md.
std::wstring VirtualizedRoaming() {
    const std::wstring localAppData = KnownFolder(FOLDERID_LocalAppData);
    if (localAppData.empty()) {
        return L"";
    }
    return localAppData + L"\\Packages\\" + kPackageFamilyName + L"\\LocalCache\\Roaming";
}

bool ProfileDirExists(const std::wstring& profileDir, const std::wstring& profileName) {
    if (GetFileAttributesW(profileDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    const std::wstring roaming = VirtualizedRoaming();
    if (roaming.empty()) {
        return false;
    }
    return GetFileAttributesW((roaming + L"\\" + profileName).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// --- wykrywanie profili ----------------------------------------------------

// Numer z nazwy katalogu profilu; 0, gdy nazwa nie konczy sie sama cyframi.
int ProfileNumber(const std::wstring& name) {
    const size_t prefix = wcslen(kProfilePrefix);
    if (name.size() <= prefix) {
        return 0;
    }
    const std::wstring suffix = name.substr(prefix);
    if (!std::all_of(suffix.begin(), suffix.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return 0;
    }
    return _wtoi(suffix.c_str());
}

void CollectProfiles(const std::wstring& root, std::vector<std::wstring>* out) {
    if (root.empty()) {
        return;
    }
    WIN32_FIND_DATAW data = {};
    const HANDLE find = FindFirstFileW((root + L"\\" + kProfilePrefix + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        const std::wstring name = data.cFileName;
        if (std::find(out->begin(), out->end(), name) == out->end()) {
            out->push_back(name);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

// Lista profili dodatkowych, posortowana po numerze. Szukamy w obu lokalizacjach,
// bo w zaleznosci od tego, jak Claude byl uruchamiany, katalog moze byc w kazdej
// z nich - a uzytkownik ma prawo zobaczyc wszystkie swoje konta.
std::vector<std::wstring> EnumerateProfiles() {
    std::vector<std::wstring> profiles;
    CollectProfiles(RoamingAppData(), &profiles);
    CollectProfiles(VirtualizedRoaming(), &profiles);

    std::sort(profiles.begin(), profiles.end(), [](const std::wstring& a, const std::wstring& b) {
        const int na = ProfileNumber(a);
        const int nb = ProfileNumber(b);
        return (na != nb) ? (na < nb) : (a < b);
    });
    return profiles;
}

// Pierwszy wolny numer profilu - uzywany przez karte "Nowe konto".
std::wstring NextProfileName(const std::vector<std::wstring>& profiles) {
    int highest = kFirstProfileNumber - 1;
    for (const std::wstring& profile : profiles) {
        highest = (std::max)(highest, ProfileNumber(profile));
    }
    return kProfilePrefix + std::to_wstring(highest + 1);
}

bool IsPositiveNumber(const std::wstring& text) {
    return !text.empty() && text.size() <= 3 &&
           std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// Karty ekranu wyboru: profil domyslny, potem po jednej na wykryty profil,
// na koncu "Nowe konto". Kolejnosc odpowiada indeksom obslugiwanym w wWinMain.
std::vector<ChooserItem> BuildChooserItems(const std::vector<std::wstring>& profiles) {
    std::vector<ChooserItem> items;
    items.reserve(profiles.size() + 2);

    // Konto 1 jest profilem domyslnym: to ono startuje przy uruchomieniu Claude
    // z menu Start, z paska zadan i przy otwarciu linku claude://.
    items.push_back({L"Konto 1",
                     L"Startuje z menu Start i paska zadań, obsługuje linki claude://",
                     true});

    for (const std::wstring& profile : profiles) {
        const int number = ProfileNumber(profile);
        const std::wstring title = number > 0 ? L"Konto " + std::to_wstring(number) : profile;
        items.push_back({title, L"Osobny profil w katalogu " + profile, false});
    }

    items.push_back({L"Nowe konto",
                     L"Utworzy profil " + NextProfileName(profiles) +
                         L" i otworzy okno logowania",
                     false});
    return items;
}

// --- uruchamianie ----------------------------------------------------------

// Atrybut nadaje procesowi potomnemu tozsamosc wskazanego pakietu MSIX. Nie ma go
// w publicznej czesci naglowkow SDK (siedzi za _USE_FULL_PROC_THREAD_ATTRIBUTE),
// wiec numer trzeba podac recznie.
//
// Wartosc 8, a nie 23. Publiczny enum PROC_THREAD_ATTRIBUTE_NUM wylicza
// 0,2,3,4,5,6,7,9,11,13 - luka pod 8 to wlasnie ten atrybut. Wczesniejsza wersja
// uzywala 23, co trafialo w zupelnie inny atrybut oczekujacy pola stalej dlugosci:
// UpdateProcThreadAttribute zwracalo wtedy blad 24 (ERROR_BAD_LENGTH), bo dostawalo
// dlugi ciag znakow. Potwierdzone w logu launchera na maszynie uzytkownika.
#ifndef PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME
#define ProcThreadAttributePackageFullName 8
#define PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME \
    ProcThreadAttributeValue(ProcThreadAttributePackageFullName, FALSE, TRUE, FALSE)
#endif

// Uruchamia proces z tozsamoscia wskazanego pakietu MSIX.
bool CreateProcessWithPackageIdentity(const std::wstring& exePath, std::wstring& commandLine,
                                      const std::wstring& packageFullName,
                                      PROCESS_INFORMATION* processInfo) {
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    if (attributeSize == 0) {
        return false;
    }

    std::vector<BYTE> storage(attributeSize);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize)) {
        return false;
    }

    // Bufor z nazwa pakietu musi zyc az do CreateProcess - atrybut trzyma wskaznik.
    std::wstring fullName = packageFullName;
    bool created = false;

    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME,
                                  fullName.data(), (fullName.size() + 1) * sizeof(wchar_t),
                                  nullptr, nullptr)) {
        STARTUPINFOEXW startupInfo = {};
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.lpAttributeList = attributes;

        created = CreateProcessW(exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                                 EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                 &startupInfo.StartupInfo, processInfo) != FALSE;
    }

    DeleteProcThreadAttributeList(attributes);
    return created;
}

// Aktywacja pakietu zachowuje tozsamosc MSIX: powiadomienia systemowe i obsluga
// linkow claude:// dzialaja normalnie. Parametr "arguments" API ActivateApplication
// nalezy do kontraktu aktywacji WinRT (Windows.Launch, patrz dokumentacja
// IApplicationActivationManager::ActivateApplication) - Electron, jako zwykla
// aplikacja Win32, go nie odczytuje. Sprawdzone i odrzucone: wczesniejsza wersja
// probowala przekazac tak --user-data-dir dla konta 2, co konczylo sie cichym
// powrotem do profilu domyslnego (Electron odsylal zadanie do istniejacej
// instancji i konczyl proces - dokladnie objaw zgloszony przez uzytkownika).
// Dlatego ta funkcja sluzy wylacznie do profilu domyslnego, bez argumentow.
HRESULT ActivatePackage(DWORD* processId) {
    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr)) {
        return hr;
    }
    hr = manager->ActivateApplication(kAppUserModelId, nullptr, AO_NONE, processId);
    manager->Release();
    return hr;
}

bool LaunchDefaultProfile() {
    DWORD processId = 0;
    const HRESULT hr = ActivatePackage(&processId);
    if (FAILED(hr)) {
        WriteLog(L"LaunchDefaultProfile: ActivatePackage nie powiodlo sie, HRESULT=" + FormatHResult(hr));
        ShowError(L"Nie udało się uruchomić Claude Desktop",
                  L"Aktywacja pakietu nie powiodła się (HRESULT " + FormatHResult(hr) + L").\n\n"
                  L"AppUserModelId: " + kAppUserModelId + L"\n\n"
                  L"Sprawdź, czy aplikacja jest zainstalowana.");
        return false;
    }
    WriteLog(L"LaunchDefaultProfile: OK, processId=" + std::to_wstring(processId));
    return true;
}

// Kazdy dodatkowy profil to osobny --user-data-dir. Electron trzyma blokade
// pojedynczej instancji per katalog danych, wiec powtorne uruchomienie tego samego
// profilu tylko przywola istniejace okno - wlasnej synchronizacji nie potrzebujemy.
bool LaunchWithProfile(const std::wstring& profileName) {
    WriteLog(L"LaunchWithProfile: profil=" + profileName);

    if (!IsValidProfileName(profileName)) {
        WriteLog(L"LaunchWithProfile: nieprawidlowa nazwa profilu");
        ShowError(L"Nieprawidlowa nazwa profilu",
                  L"Podano: " + profileName + L"\n\n"
                  L"Dozwolona jest nazwa pojedynczego katalogu, bez separatorow sciezki.");
        return false;
    }

    std::wstring diagnostic;
    const PackageInfo package = ResolvePackage(&diagnostic);
    if (package.exePath.empty()) {
        WriteLog(L"LaunchWithProfile: ResolvePackage nie powiodlo sie: " + diagnostic);
        ShowError(L"Nie znaleziono Claude Desktop",
                  L"Pakiet " + std::wstring(kPackageFamilyName) + L" nie został odnaleziony.\n\n"
                  + diagnostic);
        return false;
    }
    WriteLog(L"LaunchWithProfile: exePath=" + package.exePath + L"  fullName=" +
             (package.fullName.empty() ? std::wstring(L"<brak>") : package.fullName));

    const std::wstring appData = RoamingAppData();
    if (appData.empty()) {
        ShowError(L"Nie udalo sie odczytac sciezki %APPDATA%",
                  L"SHGetKnownFolderPath zwrocil blad dla FOLDERID_RoamingAppData.");
        return false;
    }

    const std::wstring profileDir = appData + L"\\" + profileName;
    std::wstring commandLine = L"\"" + package.exePath + L"\" --user-data-dir=\"" + profileDir + L"\"";

    // Dwie strategie w kolejnosci od najpewniejszej. Kazda nieudana zostawia slad,
    // zeby komunikat o bledzie mowil, ktora odpadla i z jakim kodem. Trzecia
    // strategia uzywana wczesniej (aktywacja pakietu z argumentem --user-data-dir)
    // zostala usunieta: "arguments" w ActivateApplication to kontrakt WinRT
    // Windows.Launch, ktorego Electron nie odczytuje - patrz komentarz przy
    // ActivatePackage().
    HANDLE process = nullptr;
    std::wstring strategy;
    std::wstring attempts;

    if (!package.fullName.empty()) {
        PROCESS_INFORMATION processInfo = {};
        if (CreateProcessWithPackageIdentity(package.exePath, commandLine,
                                             package.fullName, &processInfo)) {
            CloseHandle(processInfo.hThread);
            process = processInfo.hProcess;
            strategy = L"CreateProcess z tożsamością pakietu";
            WriteLog(L"LaunchWithProfile: tozsamosc pakietu OK, pid=" + std::to_wstring(processInfo.dwProcessId));
        } else {
            const DWORD error = GetLastError();
            attempts += L"• tożsamość pakietu: błąd " + std::to_wstring(error) + L"\n";
            WriteLog(L"LaunchWithProfile: tozsamosc pakietu nie powiodla sie, blad=" + std::to_wstring(error));
        }
    } else {
        attempts += L"• tożsamość pakietu: pominięta, nieznana pełna nazwa pakietu\n";
        WriteLog(L"LaunchWithProfile: tozsamosc pakietu pominieta - brak fullName");
    }

    if (process == nullptr) {
        // Zwykly CreateProcess wystarcza tylko wtedy, gdy sam launcher dziala juz
        // z tozsamoscia pakietu - proces potomny ja wtedy dziedziczy.
        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        if (CreateProcessW(package.exePath.c_str(), commandLine.data(), nullptr, nullptr,
                           FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo)) {
            CloseHandle(processInfo.hThread);
            process = processInfo.hProcess;
            strategy = L"zwykły CreateProcess";
            WriteLog(L"LaunchWithProfile: zwykly CreateProcess OK, pid=" + std::to_wstring(processInfo.dwProcessId));
        } else {
            const DWORD error = GetLastError();
            attempts += L"• zwykły CreateProcess: błąd " + std::to_wstring(error) + L"\n";
            WriteLog(L"LaunchWithProfile: zwykly CreateProcess nie powiodl sie, blad=" + std::to_wstring(error));
        }
    }

    if (process == nullptr) {
        WriteLog(L"LaunchWithProfile: zadna metoda nie zadzialala");
        ShowError(L"Nie udało się uruchomić Claude Desktop",
                  L"Żadna z metod uruchomienia nie zadziałała:\n\n" + attempts);
        return false;
    }

    // Samo powodzenie CreateProcess niczego nie dowodzi: Claude uruchomiony bez
    // tozsamosci pakietu moglby wystartowac i zniknac, ignorujac --user-data-dir.
    // Czekamy wiec chwile i sprawdzamy, czy proces przezyl.
    const DWORD waitResult = WaitForSingleObject(process, 5000);
    DWORD exitCode = STILL_ACTIVE;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(process, &exitCode);
    }
    CloseHandle(process);

    if (waitResult != WAIT_OBJECT_0) {
        WriteLog(L"LaunchWithProfile: OK (" + strategy + L"), proces nadal zyje po 5s");
        return true;  // proces zyje - to normalny, udany start
    }

    // Proces zakonczyl sie od razu. To jest normalne: Claude.exe z pakietu MSIX
    // przekazuje uruchomienie procesowi w kontenerze i sam konczy sie z kodem 0.
    // Dowodem faktycznego startu jest katalog profilu - ale moze powstac z niewielkim
    // opoznieniem, wiec odpytujemy go przez chwile zamiast sprawdzac raz.
    bool profileDirExists = false;
    for (int attempt = 0; attempt < 10 && !profileDirExists; ++attempt) {
        profileDirExists = ProfileDirExists(profileDir, profileName);
        if (!profileDirExists) {
            Sleep(500);
        }
    }

    WriteLog(L"LaunchWithProfile: proces (" + strategy + L") zakonczyl sie natychmiast, kod=" +
             std::to_wstring(exitCode) + L", katalog profilu istnieje=" + (profileDirExists ? L"tak" : L"nie"));

    if (profileDirExists) {
        return true;
    }

    ShowError(L"Nie udało się otworzyć drugiej instancji",
              L"Metoda: " + strategy + L"\n\n"
              L"Proces Claude zakończył się natychmiast (kod " + std::to_wstring(exitCode) +
              L") i nie utworzył katalogu profilu:\n" + profileDir +
              L"\n\nSzczegóły w logu:\n%LOCALAPPDATA%\\KKr\\ClaudeProfiles\\claude-profiles.log" +
              (attempts.empty() ? L"" : L"\n\nWcześniejsze próby:\n" + attempts));
    return false;
}

}  // namespace

// Dopisuje wiersz ze znacznikiem czasu do
// %LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log.
// Zapisujemy UTF-8 (nie surowe UTF-16), zeby plik dalo sie otworzyc w dowolnym
// edytorze bez zgadywania kodowania. Kazde niepowodzenie po drodze (brak
// %LOCALAPPDATA%, brak dostepu do pliku) jest cicho ignorowane - logowanie nie
// moze samo w sobie wywalic uruchamiania Claude.
//
// Po to, zeby diagnozowanie nie wymagalo pytania uzytkownika, co dokladnie
// pokazalo okno bledu: kazda decyzja i kazdy kod bledu z LaunchWithProfile /
// LaunchDefaultProfile trafia tutaj, wiec wystarczy przeczytac ten plik.
void WriteLog(const std::wstring& message) {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        return;
    }
    const std::wstring kkrDir = std::wstring(localAppData) + L"\\KKr";
    const std::wstring dir    = kkrDir + L"\\ClaudeProfiles";
    CoTaskMemFree(localAppData);
    CreateDirectoryW(kkrDir.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    const HANDLE file = CreateFileW((dir + L"\\claude-profiles.log").c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t timestamp[24] = {};
    swprintf_s(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u  ", time.wYear, time.wMonth, time.wDay,
              time.wHour, time.wMinute, time.wSecond);
    const std::wstring line = std::wstring(timestamp) + message + L"\r\n";

    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                               static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8Length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(),
                       utf8Length, nullptr, nullptr);

    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WriteLog(L"=== start ===  " + std::wstring(GetCommandLineW()));

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        WriteLog(L"CommandLineToArgvW nie powiodlo sie, blad=" + std::to_wstring(GetLastError()));
        ShowError(L"Nie udalo sie odczytac linii polecen",
                  L"CommandLineToArgvW zwrocil blad " + std::to_wstring(GetLastError()) + L".");
        return 1;
    }

    std::wstring profile;  // pusty = profil domyslny
    bool hasTarget = false;
    bool help = false;
    bool invalid = false;

    for (int i = 1; i < argc && !help && !invalid; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--profile" && i + 1 < argc) {
            profile = argv[++i];
            hasTarget = true;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            help = true;
        } else if (IsPositiveNumber(arg)) {
            // "1" to profil domyslny, kazdy kolejny numer to katalog Claude-kontoN.
            const int number = _wtoi(arg.c_str());
            profile = (number <= 1) ? L"" : kProfilePrefix + std::to_wstring(number);
            hasTarget = true;
        } else {
            invalid = true;
        }
    }

    LocalFree(argv);

    WriteLog(help ? L"tryb: --help" : invalid ? L"tryb: nieprawidlowe argumenty"
             : hasTarget ? L"tryb: bezposredni, profil=" + (profile.empty() ? std::wstring(L"<domyslny>") : profile)
             : L"tryb: okno wyboru konta");

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        WriteLog(L"CoInitializeEx nie powiodlo sie");
        ShowError(L"Blad inicjalizacji COM", L"CoInitializeEx nie powiodlo sie.");
        return 1;
    }

    // Bez argumentow pokazujemy okno wyboru - dzieki temu jeden skrot na pulpicie
    // obsluguje wszystkie konta. Numer konta pomija okno i uruchamia od razu.
    int exitCode = 0;
    if (help || invalid || !hasTarget) {
        const std::vector<std::wstring> profiles = EnumerateProfiles();
        WriteLog(L"wykryte profile: " + std::to_wstring(profiles.size()));

        const int choice = ShowChooser(instance, BuildChooserItems(profiles));
        if (choice == kNoChoice) {
            exitCode = invalid ? 2 : 0;
        } else if (choice == 0) {
            exitCode = LaunchDefaultProfile() ? 0 : 1;
        } else if (static_cast<size_t>(choice) <= profiles.size()) {
            exitCode = LaunchWithProfile(profiles[static_cast<size_t>(choice) - 1]) ? 0 : 1;
        } else {
            // Ostatnia karta: nowe konto. Katalog utworzy sam Claude przy starcie.
            exitCode = LaunchWithProfile(NextProfileName(profiles)) ? 0 : 1;
        }
    } else {
        const bool ok = profile.empty() ? LaunchDefaultProfile() : LaunchWithProfile(profile);
        exitCode = ok ? 0 : 1;
    }

    CoUninitialize();
    WriteLog(L"=== koniec ===  kod wyjscia=" + std::to_wstring(exitCode));
    return exitCode;
}
