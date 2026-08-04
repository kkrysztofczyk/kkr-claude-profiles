// Okno wyboru konta.
//
// Swiadomie nie uzywamy TaskDialog: z ikona systemowa i ukladem komunikatu
// wyglada jak blad Windows. To jest ekran startowy, wiec rysujemy go sami -
// karty do klikniecia, kolory z motywu systemowego, zaokraglone rogi na Win11.
//
// Liczba kart jest znana dopiero w czasie uruchomienia (launcher wykrywa istniejace
// profile), wiec wysokosc okna liczymy z liczby pozycji.

#define WIN32_LEAN_AND_MEAN
#include "chooser.h"

#include <objidl.h>
#include <windowsx.h>

#include <dwmapi.h>
#include <gdiplus.h>

#include <cwchar>
#include <string>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

// __DATE__/__TIME__ sa literalami waskimi; L##x wymaga posredniego makra, zeby x
// (czyli juz rozwiniete __DATE__) zdazylo sie rozwinac przed sklejeniem z L.
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

namespace {

constexpr wchar_t kWindowClass[] = L"KKrClaudeProfilesChooser";
// Nazwa wyswietlana: rzeczownik generyczny jest glowa, znak towarowy Anthropic
// wystepuje jako okreslenie zgodnosci. Odwrotny szyk ("Claude Desktop Profiles")
// czytalby sie jak nazwa funkcji Anthropic, a nie cudzego narzedzia.
constexpr wchar_t kWindowTitle[] = L"Profiles for Claude Desktop";

// Wymiary w jednostkach niezaleznych od DPI; skalowane przez Scale().
constexpr int kWidth        = 470;
constexpr int kMargin       = 24;
constexpr int kIconSize     = 40;
constexpr int kCardHeight   = 68;
constexpr int kCardGap      = 10;
constexpr int kCardsTop     = 92;
constexpr int kFooterSpace  = 62;  // miejsce pod ostatnia karta na stopke
constexpr int kCornerRadius = 8;
constexpr int kBadgeRadius  = 7;

// Wysokosc okna zalezy od liczby kart - stalej kHeight juz nie ma.
int ContentHeight(size_t cardCount) {
    const int cards = static_cast<int>(cardCount);
    return kCardsTop + cards * kCardHeight + (cards - 1) * kCardGap + kFooterSpace;
}

struct Palette {
    Gdiplus::Color background;
    Gdiplus::Color card;
    Gdiplus::Color cardHover;
    Gdiplus::Color border;
    Gdiplus::Color borderFocus;
    Gdiplus::Color badge;
    COLORREF       text;
    COLORREF       textSecondary;
    COLORREF       badgeText;
};

// Kolor wiodacy zgodny z identyfikacja Claude; reszta dobrana pod motyw systemu.
Palette MakePalette(bool dark) {
    if (dark) {
        return {Gdiplus::Color(255, 31, 30, 29),
                Gdiplus::Color(255, 43, 42, 40),
                Gdiplus::Color(255, 55, 53, 47),
                Gdiplus::Color(255, 61, 58, 53),
                Gdiplus::Color(255, 217, 119, 87),
                Gdiplus::Color(64, 217, 119, 87),
                RGB(245, 244, 242), RGB(166, 160, 153), RGB(226, 145, 114)};
    }
    return {Gdiplus::Color(255, 250, 249, 247),
            Gdiplus::Color(255, 255, 255, 255),
            Gdiplus::Color(255, 244, 241, 236),
            Gdiplus::Color(255, 227, 222, 214),
            Gdiplus::Color(255, 217, 119, 87),
            Gdiplus::Color(38, 217, 119, 87),
            RGB(31, 30, 29), RGB(107, 101, 96), RGB(180, 84, 54)};
}

bool SystemUsesDarkMode() {
    DWORD value = 1;  // brak wpisu traktujemy jak motyw jasny
    DWORD size = sizeof(value);
    const LONG rc = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (rc != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;
}

struct Card {
    RECT         bounds = {};
    RECT         badge  = {};  // pusty, gdy karta nie ma odznaki
    std::wstring title;
    std::wstring detail;
    bool         isDefault = false;
};

struct State {
    int               result = kNoChoice;
    Palette           palette;
    UINT              dpi = 96;
    int               height = 0;  // wysokosc obszaru klienta w jednostkach logicznych
    int               hovered = -1;
    int               focused = 0;
    bool              tracking = false;
    HICON             icon = nullptr;
    HFONT             fontTitle = nullptr;
    HFONT             fontCardTitle = nullptr;
    HFONT             fontBody = nullptr;
    HFONT             fontBadge = nullptr;
    std::vector<Card> cards;
};

constexpr wchar_t kBadgeText[] = L"DOMYŚLNY";

int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

HFONT MakeFont(int pointSize, int weight, UINT dpi) {
    LOGFONTW font = {};
    font.lfHeight  = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    font.lfWeight  = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&font);
}

void ReleaseFonts(State* state) {
    if (state->fontTitle) DeleteObject(state->fontTitle);
    if (state->fontCardTitle) DeleteObject(state->fontCardTitle);
    if (state->fontBody) DeleteObject(state->fontBody);
    if (state->fontBadge) DeleteObject(state->fontBadge);
    state->fontTitle = state->fontCardTitle = state->fontBody = state->fontBadge = nullptr;
}

void RebuildForDpi(State* state) {
    ReleaseFonts(state);
    state->fontTitle     = MakeFont(15, FW_SEMIBOLD, state->dpi);
    state->fontCardTitle = MakeFont(11, FW_SEMIBOLD, state->dpi);
    state->fontBody      = MakeFont(9, FW_NORMAL, state->dpi);
    state->fontBadge     = MakeFont(7, FW_SEMIBOLD, state->dpi);

    const int left   = Scale(kMargin, state->dpi);
    const int right  = Scale(kWidth - kMargin, state->dpi);
    const int height = Scale(kCardHeight, state->dpi);
    int top = Scale(kCardsTop, state->dpi);

    for (Card& card : state->cards) {
        card.bounds = {left, top, right, top + height};
        top += height + Scale(kCardGap, state->dpi);
    }
}

int CardAtPoint(const State* state, POINT point) {
    for (size_t i = 0; i < state->cards.size(); ++i) {
        if (PtInRect(&state->cards[i].bounds, point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SIZE MeasureText(HDC dc, PCWSTR text, HFONT font) {
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    SelectObject(dc, previous);
    return size;
}

void FillRoundedRect(Gdiplus::Graphics* graphics, const RECT& rect, int radius,
                     const Gdiplus::Color& fill, const Gdiplus::Color* stroke) {
    const int diameter = radius * 2;

    Gdiplus::GraphicsPath path;
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(fill);
    graphics->FillPath(&brush, &path);

    if (stroke != nullptr) {
        Gdiplus::Pen pen(*stroke, 1.0f);
        graphics->DrawPath(&pen, &path);
    }
}

// Nazwa DrawText jest zajeta przez makro z windows.h, stad DrawLabel.
void DrawLabel(HDC dc, PCWSTR text, RECT rect, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, previous);
}

// Odznaka trafia obok tytulu karty, wiec jej polozenie zalezy od szerokosci tekstu.
void LayoutBadges(HDC dc, State* state) {
    for (Card& card : state->cards) {
        card.badge = {};
        if (!card.isDefault) {
            continue;
        }
        const SIZE title = MeasureText(dc, card.title.c_str(), state->fontCardTitle);
        const SIZE badge = MeasureText(dc, kBadgeText, state->fontBadge);

        const int padding = Scale(7, state->dpi);
        const int left    = card.bounds.left + Scale(16, state->dpi) + title.cx + Scale(10, state->dpi);
        const int top     = card.bounds.top + Scale(13, state->dpi);

        card.badge = {left, top, left + badge.cx + padding * 2, top + badge.cy + Scale(3, state->dpi)};
    }
}

void Paint(HWND window, State* state) {
    PAINTSTRUCT paint = {};
    HDC screenDc = BeginPaint(window, &paint);

    RECT client = {};
    GetClientRect(window, &client);

    // Rysujemy do bufora w pamieci - inaczej karty migocza przy najechaniu mysza.
    HDC dc = CreateCompatibleDC(screenDc);
    HBITMAP buffer = CreateCompatibleBitmap(screenDc, client.right, client.bottom);
    HGDIOBJ previousBitmap = SelectObject(dc, buffer);

    LayoutBadges(dc, state);

    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::SolidBrush background(state->palette.background);
        graphics.FillRectangle(&background, 0, 0, client.right, client.bottom);

        for (size_t i = 0; i < state->cards.size(); ++i) {
            const Card& card = state->cards[i];
            const bool hovered = (state->hovered == static_cast<int>(i));
            const bool focused = (state->focused == static_cast<int>(i));

            FillRoundedRect(&graphics, card.bounds, Scale(kCornerRadius, state->dpi),
                            hovered ? state->palette.cardHover : state->palette.card,
                            focused ? &state->palette.borderFocus : &state->palette.border);

            if (card.isDefault) {
                FillRoundedRect(&graphics, card.badge, Scale(kBadgeRadius, state->dpi),
                                state->palette.badge, nullptr);
            }
        }
    }

    const int margin   = Scale(kMargin, state->dpi);
    const int iconSize = Scale(kIconSize, state->dpi);

    if (state->icon) {
        DrawIconEx(dc, margin, margin, state->icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    const int textLeft  = margin + iconSize + Scale(14, state->dpi);
    const int textRight = Scale(kWidth - kMargin, state->dpi);

    RECT titleRect = {textLeft, margin - Scale(2, state->dpi), textRight, margin + Scale(24, state->dpi)};
    DrawLabel(dc, kWindowTitle, titleRect, state->fontTitle, state->palette.text,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    RECT subtitleRect = {textLeft, margin + Scale(22, state->dpi), textRight, margin + Scale(44, state->dpi)};
    DrawLabel(dc, L"Wybierz konto do uruchomienia", subtitleRect, state->fontBody,
              state->palette.textSecondary, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    for (const Card& card : state->cards) {
        RECT cardTitle = card.bounds;
        cardTitle.left += Scale(16, state->dpi);
        cardTitle.top  += Scale(11, state->dpi);
        DrawLabel(dc, card.title.c_str(), cardTitle, state->fontCardTitle, state->palette.text,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        if (card.isDefault) {
            DrawLabel(dc, kBadgeText, card.badge, state->fontBadge, state->palette.badgeText,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        RECT cardDetail = card.bounds;
        cardDetail.left  += Scale(16, state->dpi);
        cardDetail.right -= Scale(14, state->dpi);
        cardDetail.top   += Scale(33, state->dpi);
        DrawLabel(dc, card.detail.c_str(), cardDetail, state->fontBody, state->palette.textSecondary,
                  DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }

    RECT footer = {margin, Scale(state->height - 44, state->dpi), textRight,
                   Scale(state->height - 8, state->dpi)};
    DrawLabel(dc,
              L"Każde konto ma własny profil, więc okna mogą działać równocześnie.",
              footer, state->fontBody, state->palette.textSecondary,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    BitBlt(screenDc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);

    SelectObject(dc, previousBitmap);
    DeleteObject(buffer);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

void Commit(HWND window, State* state, int index) {
    state->result = index;
    WriteLog(L"chooser: wybrano kafelek " + std::to_wstring(index) + L" (" +
             state->cards[static_cast<size_t>(index)].title + L")");
    DestroyWindow(window);
}

// Strzalki i Tab chodza po kartach w kolo. Przy dwoch kartach kazdy kierunek
// sprowadza sie do przelaczenia, przy wiekszej liczbie ma znaczenie zwrot.
void MoveFocus(HWND window, State* state, int delta) {
    const int count = static_cast<int>(state->cards.size());
    if (count <= 1) {
        return;
    }
    state->focused = ((state->focused + delta) % count + count) % count;
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return DefWindowProcW(window, message, wParam, lParam);
        }

        case WM_ERASEBKGND:
            return 1;  // tlo rysuje WM_PAINT do bufora

        case WM_PAINT:
            Paint(window, state);
            return 0;

        case WM_SETCURSOR:
            // Bez tego kursor wracalby do strzalki przy kazdym ruchu myszy.
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, state->hovered >= 0 ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE: {
            if (!state->tracking) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                state->tracking = true;
            }
            const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int hovered = CardAtPoint(state, point);
            if (hovered != state->hovered) {
                state->hovered = hovered;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            state->tracking = false;
            if (state->hovered != -1) {
                state->hovered = -1;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONUP: {
            const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int index = CardAtPoint(state, point);
            if (index >= 0) {
                Commit(window, state, index);
            }
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:
                    DestroyWindow(window);
                    return 0;
                case VK_RETURN:
                case VK_SPACE:
                    Commit(window, state, state->focused);
                    return 0;
                case VK_TAB:
                    MoveFocus(window, state, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                    return 0;
                case VK_DOWN:
                case VK_RIGHT:
                    MoveFocus(window, state, 1);
                    return 0;
                case VK_UP:
                case VK_LEFT:
                    MoveFocus(window, state, -1);
                    return 0;
                default:
                    break;
            }
            return 0;

        case WM_DPICHANGED: {
            state->dpi = HIWORD(wParam);
            RebuildForDpi(state);
            const RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CenterOnCursorMonitor(HWND window, int width, int height) {
    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Kazda z tych awarii konczyla sie wczesniej cichym wyjsciem z kodem 0 - z punktu
// widzenia uzytkownika program po prostu nie reagowal na klikniecie skrotu.
// "code" jest podawany przez wywolujacego, a nie czytany tu przez GetLastError():
// GdiplusStartup zwraca Gdiplus::Status, ktory nie ma nic wspolnego z Win32
// last-error, wiec czytanie go w tej funkcji pokazywaloby przypadkowy kod.
void ReportStartupFailure(PCWSTR step, DWORD code) {
    WriteLog(L"chooser: awaria startu w kroku " + std::wstring(step) + L", kod=" + std::to_wstring(code));
    const std::wstring text = std::wstring(L"Nie udało się otworzyć okna wyboru konta.\n\n")
                              + step + L" (kod " + std::to_wstring(code) + L").\n\n"
                              L"Uruchom launcher z numerem konta, aby pominąć to okno.";
    MessageBoxW(nullptr, text.c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
}

}  // namespace

int ShowChooser(HINSTANCE instance, const std::vector<ChooserItem>& items) {
    if (items.empty()) {
        return kNoChoice;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    const Gdiplus::Status gdiplusStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    if (gdiplusStatus != Gdiplus::Ok) {
        ReportStartupFailure(L"GdiplusStartup", static_cast<DWORD>(gdiplusStatus));
        return kNoChoice;
    }

    const bool dark = SystemUsesDarkMode();

    State state;
    state.palette = MakePalette(dark);
    state.icon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    state.height = ContentHeight(items.size());

    state.cards.reserve(items.size());
    for (const ChooserItem& item : items) {
        Card card;
        card.title     = item.title;
        card.detail    = item.detail;
        card.isDefault = item.isDefault;
        state.cards.push_back(card);
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = WindowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIcon         = state.icon;
    if (RegisterClassExW(&windowClass) == 0) {
        ReportStartupFailure(L"RegisterClassEx", GetLastError());
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return kNoChoice;
    }

    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

    // Data/godzina builda w tytule okna - jedyny latwy sposob zeby przy debugowaniu
    // rozpoznac golym okiem, czy zainstalowana wersja to faktycznie ostatni build,
    // a nie stara kopia zostawiona przez przerwana instalacje.
    const std::wstring windowTitle = std::wstring(kWindowTitle) + L"  (build "
                                     + WIDEN(__DATE__) L" " WIDEN(__TIME__) L")";

    HWND window = CreateWindowExW(0, kWindowClass, windowTitle.c_str(), kStyle,
                                  CW_USEDEFAULT, CW_USEDEFAULT, kWidth, state.height,
                                  nullptr, nullptr, instance, &state);
    if (window == nullptr) {
        ReportStartupFailure(L"CreateWindowEx", GetLastError());
        UnregisterClassW(kWindowClass, instance);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return kNoChoice;
    }

    state.dpi = GetDpiForWindow(window);
    RebuildForDpi(&state);

    // Pasek tytulu w kolorze motywu i zaokraglone rogi - oba wywolania sa
    // nieszkodliwe na starszych buildach, DWM po prostu je ignoruje.
    const BOOL darkTitleBar = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitleBar, sizeof(darkTitleBar));
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // Rozmiary sa podane w jednostkach logicznych, wiec obszar klienta trzeba
    // przeliczyc na biezace DPI i doliczyc ramke okna.
    RECT desired = {0, 0, Scale(kWidth, state.dpi), Scale(state.height, state.dpi)};
    AdjustWindowRectExForDpi(&desired, kStyle, FALSE, 0, state.dpi);
    const int windowWidth  = desired.right - desired.left;
    const int windowHeight = desired.bottom - desired.top;
    SetWindowPos(window, nullptr, 0, 0, windowWidth, windowHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CenterOnCursorMonitor(window, windowWidth, windowHeight);

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    ReleaseFonts(&state);
    UnregisterClassW(kWindowClass, instance);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return state.result;
}
