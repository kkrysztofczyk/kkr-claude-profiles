# Kontekst projektu

Notatka robocza dla osoby (lub agenta) wracającej do tego kodu. Opis dla użytkownika
jest w [README.md](README.md), a udokumentowane znaleziska w [FINDINGS.md](FINDINGS.md).

## Co to jest

Natywny launcher Win32, który uruchamia Claude Desktop na osobnych profilach, żeby
kilka kont mogło być zalogowanych jednocześnie. Bez zależności, statyczny CRT,
kompilacja z `/W4 /WX /permissive-` — ostrzeżenia są traktowane jak błędy, więc build
albo przechodzi czysto, albo wcale.

## Polecenia

```bat
build.cmd      :: kompilacja -> build\claude-profiles.exe
install.cmd    :: instalacja do %LOCALAPPDATA%\KKr\ClaudeProfiles + skrót na pulpicie
uninstall.cmd  :: usunięcie programu i skrótów (profile kont zostają)
start.cmd      :: build + install + uruchomienie, jednym poleceniem
```

Ikonę generuje `tools\make-icon.ps1`; uruchamiać tylko przy zmianie wyglądu, wynik
jest w repozytorium.

## Mapa plików

| Plik | Rola |
|------|------|
| `src/main.cpp` | ustalanie ścieżki pakietu, wykrywanie profili, strategie uruchamiania, log, `wWinMain` |
| `src/chooser.cpp` | okno wyboru konta rysowane samodzielnie (GDI+), motyw ciemny, DPI |
| `src/chooser.h` | `ChooserItem`, `ShowChooser`, deklaracja `WriteLog` |
| `res/launcher.rc` | ikona, manifest, metadane wersji |
| `res/app.manifest` | comctl32 v6, `PerMonitorV2`, UTF-8, `asInvoker` |

## Pułapki, o których trzeba wiedzieć zawczasu

**Claude działa w kontenerze MSIX i przekierowuje `%APPDATA%`.** Katalog profilu nie
powstaje tam, gdzie wskazuje `--user-data-dir`, tylko pod
`%LOCALAPPDATA%\Packages\<pakiet>\LocalCache\Roaming\<profil>`. Dlatego
`ProfileDirExists()` sprawdza obie lokalizacje, a `EnumerateProfiles()` przeszukuje
obie. Nie „upraszczaj" tego do jednej ścieżki — to był źródłowy błąd, przez który
launcher zgłaszał awarię przy każdym poprawnym uruchomieniu.

**Sesja agenta bywa w tym samym kontenerze.** Wtedy `%APPDATA%`, `%LOCALAPPDATA%`
i `HKCU\Software\Classes` widziane przez narzędzia są zwirtualizowane, a testy
niewiarygodne: instalacja trafia do katalogu kontenera i nie aktualizuje pliku
uruchamianego przez skrót. Żeby sprawdzić coś naprawdę, utwórz proces poza bieżącym
drzewem przez WMI:

```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
  CommandLine = 'cmd.exe /c <polecenie> > "<plik-wyniku>" 2>&1'
}
```

Zwykłe `Start-Process` nie wystarczy — dziedziczy kontener po procesie nadrzędnym.

**Tytuł okna zawiera datę builda.** Jeśli objaw nie pasuje do kodu, najpierw sprawdź,
czy uruchamiana wersja to na pewno ostatnia kompilacja.

**Log jest pierwszym miejscem do zajrzenia:**
`%LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log`. Notuje tryb, ustaloną ścieżkę
pakietu, wybraną kartę, każdą próbę uruchomienia z kodem błędu i wynik weryfikacji.
Powstał po to, żeby diagnoza nie wymagała odtwarzania awarii ani pytania użytkownika,
co pokazało okno błędu.

## Jak uruchamiane są konta

Konto 1 (profil domyślny) idzie przez `ActivateApplication` — tylko aktywacja pakietu
zachowuje tożsamość MSIX, od której zależą powiadomienia i linki `claude://`.

Pozostałe profile wymagają `--user-data-dir`, więc idą przez `CreateProcess`, w dwóch
próbach: najpierw z atrybutem `PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME`, potem zwykły
`CreateProcess`. Pierwsza próba na maszynie bez tożsamości pakietu zawsze kończy się
błędem 575 i realnie działa druga — jest zostawiona świadomie, bo jest tania.

Po uruchomieniu launcher czeka 5 s. Jeśli proces żyje, to sukces. Jeśli zakończył się
od razu (co dla pakietu MSIX jest normalne), odpytuje przez ~5 s o katalog profilu
w obu lokalizacjach.

## Sprawdzone i odrzucone — nie próbuj ponownie

- `ActivateApplication` z argumentem `--user-data-dir` — parametr `arguments` należy do
  kontraktu WinRT `Windows.Launch`, którego Electron nie czyta. Zwraca `S_OK` i po cichu
  uruchamia profil domyślny.
- `PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME` o wartości 23 — to inny atrybut, daje
  błąd 24 (`ERROR_BAD_LENGTH`). Poprawna wartość to 8.
- Ścieżka do `Claude.exe` z rejestracji protokołu `claude://` jako źródło główne — ten
  klucz istnieje wyłącznie wewnątrz kontenera. Został jako źródło awaryjne.
- Enumeracja `WindowsApps\Claude_*` — ACL blokuje listowanie katalogu.
- Traktowanie natychmiastowego zakończenia procesu jako awarii — dla pakietu MSIX to
  normalne zachowanie.

## Konwencje

Komentarze w kodzie po polsku, dokumentacja użytkownika po angielsku. Komentarz ma
tłumaczyć **dlaczego**, a nie co robi linijka — szczególnie tam, gdzie rozwiązanie
wygląda na przekombinowane, bo zwykle jest wynikiem konkretnej pułapki opisanej wyżej.

Nazwa rodziny pakietu (`Claude_pzs8sxrjxfjjc`) jest zaszyta w `src/main.cpp`. Gdyby
Anthropic zmienił tożsamość pakietu, to jedyne miejsce do poprawienia.

## Co jest niedokończone

- Atrybut o wartości 8 potwierdzony tylko pośrednio i na jednej maszynie z Windows 11.
- Log rośnie bez ograniczeń, nie ma rotacji.
- `IsValidProfileName()` nie odrzuca zarezerwowanych nazw Windows (`CON`, `NUL`,
  `COM1`). Nie jest to dziura — separatory ścieżek są blokowane — ale `--profile NUL`
  zachowa się dziwnie.
- Plik nie jest podpisany cyfrowo, więc SmartScreen ostrzega po przeniesieniu na inną
  maszynę.
