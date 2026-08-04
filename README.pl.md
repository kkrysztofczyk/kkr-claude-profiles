# Profiles for Claude Desktop

[![CI](https://github.com/kkrysztofczyk/kkr-claude-profiles/actions/workflows/ci.yml/badge.svg)](https://github.com/kkrysztofczyk/kkr-claude-profiles/actions/workflows/ci.yml)
[![Licencja: MIT](https://img.shields.io/badge/Licencja-MIT-blue.svg)](LICENSE)
[![Windows](https://img.shields.io/badge/platforma-Windows%2010%20%7C%2011-0078D4)](#wymagania)

Uruchamia Claude Desktop na osobnych profilach, dzięki czemu dwa konta lub więcej mogą
być zalogowane jednocześnie, każde we własnym oknie.

*[English version of this document →](README.md)*

> **Projekt nieoficjalny.** Nie powstał w Anthropic, nie jest przez Anthropic firmowany
> ani sprawdzany. „Claude" i „Anthropic" są znakami towarowymi Anthropic PBC, użytymi tu
> opisowo — żeby powiedzieć, z czym ten program współpracuje. Repozytorium nie zawiera
> kodu ani grafik Anthropic; ikona jest oryginalna. Licencja [MIT](LICENSE).

Claude Desktop jest dystrybuowany jako pakiet MSIX i dopuszcza jedną instancję na jeden
katalog danych użytkownika. Ten launcher uruchamia go z osobnym `--user-data-dir` dla
każdego konta, więc subskrypcja prywatna i firmowa mogą działać obok siebie.

## Szybki start

1. Pobierz **`claude-profiles.exe`** z [najnowszego wydania](https://github.com/kkrysztofczyk/kkr-claude-profiles/releases/latest)
2. Połóż gdziekolwiek — pulpit w zupełności wystarczy
3. Kliknij dwa razy

To cała instalacja. Jeden samodzielny plik, bez instalatora, bez środowiska
uruchomieniowego, bez zależności, bez wpisów w rejestrze. Otworzy się okno wyboru —
wskaż konto.

Prawy przycisk → **Przypnij do paska zadań**, jeśli ma być pod ręką. Żeby usunąć
program, skasuj plik.

> Windows pokaże ostrzeżenie SmartScreen przy świeżo pobranej kopii, ponieważ plik nie
> jest podpisany cyfrowo. Wybierz **Więcej informacji → Uruchom mimo to** albo zbuduj
> projekt samodzielnie — patrz [Budowanie](#budowanie).

## Sprawdź to sam

Nie musisz wierzyć binarce na słowo. Każde wydanie jest budowane przez GitHub Actions
z otagowanego commita, więc jest odtwarzalne z publicznego kodu, a suma kontrolna jest
opublikowana obok:

```powershell
Get-FileHash claude-profiles.exe -Algorithm SHA256
```

Wynik porównaj z plikiem `SHA256SUMS.txt` dołączonym do tego samego wydania.

Program to około 1200 linii C++ w trzech plikach, więc przeczytanie go w całości jest
realne. Jeśli wolisz, żeby zrobiła to maszyna, wskaż repozytorium asystentowi AI
i poproś mniej więcej tak:

```
Przejrzyj https://github.com/kkrysztofczyk/kkr-claude-profiles

Program twierdzi, że tylko uruchamia Claude Desktop z osobnym --user-data-dir dla
każdego konta. Zweryfikuj to. Sprawdź w szczególności: połączenia sieciowe, telemetrię,
dostęp do poświadczeń, zapisy poza własnym plikiem logu oraz cokolwiek, co dotyka
katalogów profili poza ich utworzeniem. Zgłoś wszystko, czego README nie ujawnia.
```

Uczciwa odpowiedź, do której powinien dojść: program ustala ścieżkę instalacji Claude
Desktop, uruchamia aplikację z przełącznikiem `--user-data-dir`, dopisuje wiersz do
własnego logu i nic poza tym. Nie nawiązuje żadnych połączeń sieciowych.

## Użycie

```
claude-profiles.exe              okno wyboru konta
claude-profiles.exe 1            konto 1 - profil domyślny
claude-profiles.exe 2            konto 2 - profil Claude-konto2
claude-profiles.exe 3            konto 3 - profil Claude-konto3
claude-profiles.exe --profile X  dowolny profil o nazwie X
```

Uruchomiony bez argumentów otwiera własne okno wyboru, dzięki czemu jeden skrót na
pulpicie obsługuje wszystkie konta. Okno pokazuje profil domyślny, każdy profil znaleziony
na dysku oraz pozycję tworzącą kolejny — nie ma pliku konfiguracyjnego do edycji.

## Wymagania

- Windows 10 lub 11
- Claude Desktop zainstalowany z Microsoft Store (pakiet MSIX)
- Visual Studio z workloadem **Desktop development with C++** — tylko do budowania

## Budowanie

```bat
build.cmd
```

Wynik: `build\claude-profiles.exe` — x64, statycznie linkowany CRT, bez zależności
uruchomieniowych. Build traktuje ostrzeżenia jak błędy (`/W4 /WX /permissive-`), więc albo
przechodzi czysto, albo wcale.

Skrypt znajduje MSVC, sondując znane ścieżki instalacji Visual Studio; nie wymaga
`vswhere` ani wiersza poleceń dewelopera. Niektóre wersje Visual Studio wypisują na stderr
`'vswhere.exe' is not recognized` z własnego `vcvars64.bat` — to szum ze skryptów
Microsoftu, nie z tego projektu, a kompilacja i tak się udaje.

Ikonę generuje `tools\make-icon.ps1` do `res\launcher.ico`. Uruchamiaj tylko przy zmianie
wyglądu ikony; wynik jest w repozytorium.

## Instalacja ze źródeł

```bat
start.cmd
```

Buduje, instaluje i uruchamia jednym poleceniem. Poszczególne kroki:

```bat
build.cmd      :: kompilacja
install.cmd    :: kopia do %LOCALAPPDATA%\KKr\ClaudeProfiles + skrót na pulpicie
uninstall.cmd  :: usunięcie programu i skrótów
```

Odinstalowanie **nie rusza profili kont** — siedzą w nich zalogowane sesje, więc ich
skasowanie oznaczałoby ponowne logowanie. Usuń je ręcznie, jeśli naprawdę chcesz się ich
pozbyć.

## Kody wyjścia

| Kod | Znaczenie |
|-----|-----------|
| 0   | uruchomiono albo zamknięto okno wyboru |
| 1   | błąd — szczegóły w oknie dialogowym |
| 2   | nieprawidłowe argumenty |

## Log

Każde uruchomienie dopisuje się do `%LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log`
(UTF-8, jeden znaczony czasem wiersz na zdarzenie): argumenty i tryb, ustalona ścieżka
pakietu, która strategia uruchomienia zadziałała albo z jakim kodem błędu padła, oraz czy
proces przeżył i czy katalog profilu się pojawił.

```bat
type %LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.log
```

Powstał po to, żeby diagnoza nieudanego uruchomienia nigdy nie wymagała odtworzenia
awarii. Zapis do logu nie może zablokować uruchomienia — każdy jego błąd jest po cichu
ignorowany. Plik rośnie bez ograniczeń, ale jedno uruchomienie kosztuje kilka krótkich
wierszy; skasowanie go w dowolnym momencie jest bezpieczne, a `uninstall.cmd` usuwa go
razem z resztą.

Tytuł okna wyboru zawiera datę i godzinę builda, więc od razu widać, czy zainstalowana
kopia to na pewno najnowsza kompilacja.

## Decyzje projektowe

**Ścieżka do `Claude.exe` jest ustalana w czasie uruchomienia.** Claude Desktop to pakiet
MSIX, więc jego katalog instalacyjny zawiera numer wersji
(`C:\Program Files\WindowsApps\Claude_<wersja>_x64__<hash>`) i zmienia się przy każdej
aktualizacji. Zapisanie jej na sztywno zepsułoby skrót po pierwszym update.

Źródłem podstawowym jest model pakietów (`GetPackagesByPackageFamily` /
`GetPackagePathByFullName` z `appmodel.h`), który działa niezależnie od tego, czy proces
działa wewnątrz kontenera MSIX. Rejestracja protokołu `claude://` jest tylko źródłem
awaryjnym: ten klucz rejestru istnieje *wyłącznie* wewnątrz kontenera aplikacji, więc
launcher uruchomiony ze skrótu na pulpicie go nie widzi. Rozważone i odrzucone:

- `FindPackagesByPackageFamilyName` — nie jest zadeklarowana w nagłówkach Windows SDK
  10.0.26100, mimo że istnieje jako eksport `kernel32`.
- Wzorzec `WindowsApps\Claude_*` — ACL na `WindowsApps` blokuje listowanie katalogu,
  `FindFirstFile` zwraca odmowę dostępu.
- Klucze rejestru `AppModel\Repository` — nie istnieją na maszynie testowej.

**Konto 1 startuje inaczej niż pozostałe.** Profil domyślny idzie przez
`IApplicationActivationManager::ActivateApplication`, a nie `CreateProcess`, bo tylko
aktywacja pakietu zachowuje tożsamość MSIX — od której zależą powiadomienia systemowe
i linki `claude://`. Dodatkowe profile muszą iść przez `CreateProcess`, bo tylko tak da
się przekazać `--user-data-dir`. W [FINDINGS.md](FINDINGS.md) jest opisane, dlaczego
oczywista alternatywa (przekazanie argumentów do `ActivateApplication`) nie działa.

**Brak własnej synchronizacji instancji.** Electron trzyma blokadę pojedynczej instancji
kluczowaną po katalogu danych, więc powtórne uruchomienie tego samego profilu tylko
przywołuje istniejące okno. Osobny mutex niczego by nie dodał.

**Okno wyboru rysujemy sami, okna błędów nie.** `TaskDialog` z ikoną systemową czyta się
jak komunikat o błędzie, co dla ekranu startowego jest mylące — stąd własne okno z
kartami, obsługą motywu ciemnego i zaokrąglonymi rogami Windows 11. Prawdziwe błędy nadal
używają `TaskDialog`, gdzie wygląd komunikatu systemowego jest na miejscu. Gdyby
comctl32 v6 był niedostępny, każde okno błędu spada do `MessageBox`.

**Ikona celowo nie przypomina znaku Claude.** To znak towarowy Anthropic, a łudząco
podobna ikona sugerowałaby powiązanie, którego nie ma. Ważniejszy jest jednak powód
praktyczny: ikona podobna do oryginału byłaby nie do odróżnienia od samej aplikacji na
pasku zadań. Glif przedstawia dwa nachodzące okna — czyli to, co program faktycznie robi.

## Ograniczenia

**Linki `claude://` zawsze otwierają konto 1.** Protokół jest zarejestrowany w systemie na
gołą aplikację, bez `--user-data-dir`. Wszystkie deep linki i callbacki OAuth trafiają
więc do profilu domyślnego. Praktyczny skutek: logowanie przez „Continue with Google" nie
zadziała w drugiej instancji — trzeba użyć maila i kodu jednorazowego, co odbywa się w
całości w oknie aplikacji. To ograniczenie systemowe: dwa procesy nie mogą współdzielić
jednej rejestracji protokołu.

**Katalogi profili nie są tam, gdzie wskazuje `--user-data-dir`.** Claude działa w
kontenerze MSIX, więc jego zapisy do `%APPDATA%` są przekierowywane do LocalCache pakietu.
Profil podany jako `%APPDATA%\Claude-konto2` faktycznie ląduje w
`%LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalCache\Roaming\Claude-konto2`. Launcher
sprawdza obie lokalizacje — patrz [FINDINGS.md](FINDINGS.md).

**Konta nie współdzielą historii rozmów.** Ta jest po stronie serwera, przypisana do
konta. Lokalnie wspólny pozostaje `%USERPROFILE%\.claude` (sesje Claude Code, pamięć,
projekty), bo leży poza katalogami danych Electrona.

**Plik nie jest podpisany cyfrowo**, więc SmartScreen ostrzega przy świeżo pobranej kopii.
Zbudowanie samodzielnie omija to ostrzeżenie.

**Związany z jedną nazwą rodziny pakietu.** Launcher celuje w `Claude_pzs8sxrjxfjjc`;
gdyby Anthropic wydał aplikację pod inną tożsamością, trzeba poprawić stałą w
[src/main.cpp](src/main.cpp).

## Znaleziska

[FINDINGS.md](FINDINGS.md) dokumentuje to, co wyszło przy budowaniu tego programu —
wirtualizację ścieżek MSIX, nieudokumentowaną wartość
`PROC_THREAD_ATTRIBUTE_PACKAGE_FULL_NAME` i powód, dla którego argumenty
`ActivateApplication` nigdy nie docierają do aplikacji Electrona. Jeśli próbujesz
uruchomić drugą instancję dowolnej aplikacji Electrona ze Store, ten plik jest zapewne
bardziej przydatny niż sam launcher.

## O kodzie

Komentarze w kodzie są po polsku, dokumentacja po angielsku i polsku. Komentarze
tłumaczą *dlaczego* podjęto daną decyzję, a nie co robi linijka — powstawały razem
z debugowaniem, które do nich doprowadziło.

---

Jedno z narzędzi `kkr-*` autorstwa [@kkrysztofczyk](https://github.com/kkrysztofczyk).
