#include "ime.h"
#include <imm.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int mk_wcwidth(unsigned int);
struct Input { std::wstring sent; std::vector<int> chunks; };
static void send(void *context, const wchar_t *text, int length) {
    auto &input = *static_cast<Input *>(context);
    input.sent.append(text, length); input.chunks.push_back(length);
}
int main() {
    int checks = 0;
    auto require = [&](bool passed, const char *message) {
        if (!passed) throw std::runtime_error(message);
        ++checks;
    };
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"WShellImeTest"; RegisterClassW(&cls);
        HWND parent = CreateWindowW(cls.lpszClassName, L"IME test", WS_OVERLAPPEDWINDOW,
            0, 0, 480, 260, nullptr, nullptr, cls.hInstance, nullptr);
        require(parent != nullptr, "Cannot create isolated test window");
        Input input;
        WsIme *ime = wsImeCreate(parent, send, mk_wcwidth, &input);
        require(ime != nullptr, "Cannot create IME preview");
        HWND overlay = FindWindowExW(parent, nullptr, L"WShellImeComposition", nullptr);
        require(overlay != nullptr, "Preview must be a child of its own terminal");
        auto visible = [&] { return (GetWindowLongPtrW(overlay, GWL_STYLE) & WS_VISIBLE) != 0; };
        auto bounds = [&] { RECT r{}; GetWindowRect(overlay, &r); MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(&r), 2); return r; };
        auto font = static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
        wsImePosition(ime, 30, 40, 9, 20, font, RGB(206,205,195), RGB(16,15,15));
        wsImeStart(ime);
        for (auto text : {L"ㅎ", L"하", L"한", L"하"}) wsImeApply(ime, nullptr, 0, text, 1, 1);
        require(input.sent.empty(), "Preedit updates and backspace must not reach the backend");
        require(visible(), "Composition must be visible");
        auto r = bounds();
        require(r.left == 30 && r.top == 40 && r.right-r.left == 18, "Hangul must occupy two terminal cells at the caret");
        require(SendMessageW(overlay, WM_NCHITTEST, 0, 0) == HTTRANSPARENT, "Preview must not steal mouse input");
        wsImeApply(ime, L"한", 1, L"ㄱ", 1, 0);
        wsImeApply(ime, nullptr, 0, L"글", 1, 1);
        require(input.sent == L"한", "A result and next syllable in the same update must commit only the result");
        wsImeApply(ime, L"글\U0001f642", 3, nullptr, 0, 0);
        require(input.sent == L"한글\U0001f642", "Committed Unicode must stay exact");
        require(input.chunks == std::vector<int>({1,1,2}), "A surrogate pair must be sent together");
        require(!visible(), "Commit must remove the preview");
        wsImeStart(ime); wsImeApply(ime, nullptr, 0, L"취소", 2, 2); wsImeClear(ime);
        require(input.sent == L"한글\U0001f642" && !visible(), "Cancel/end must neither send nor retain the draft");
        wsImeStart(ime); wsImeApply(ime, nullptr, 0, L"한글", 2, 2);
        RECT client{}; GetClientRect(parent, &client);
        wsImePosition(ime, client.right-1, client.bottom-1, 15, 32, font, 0, 0);
        r = bounds();
        require(r.left >= 0 && r.top >= 0 && r.right <= client.right && r.bottom <= client.bottom,
            "Preview must stay inside the resized terminal");
        require(r.right-r.left == 60 && r.bottom-r.top == 32, "DPI/font changes must update composition metrics");
        HIMC context = ImmGetContext(parent);
        require(context != nullptr, "Test window must have an IMM context");
        CANDIDATEFORM candidates{};
        require(ImmGetCandidateWindow(context, 0, &candidates) && candidates.dwStyle == CFS_EXCLUDE &&
                EqualRect(&candidates.rcArea, &r), "OS candidates must exclude the actual composition row");
        ImmReleaseContext(parent, context);
        wsImeApply(ime, nullptr, 0, nullptr, 0, 0);
        require(!visible(), "Deleting all preedit must hide the preview");
        std::wstring longText(4096, L'한');
        wsImeApply(ime, nullptr, 0, longText.data(), static_cast<int>(longText.size()), 4096);
        r = bounds(); require(r.right <= client.right, "Oversized IME text must remain bounded");
        wsImeFinish(ime);
        require(!visible() && input.sent == L"한글\U0001f642", "Focus loss must not manufacture a commit from the preview");
        wsImeDestroy(ime); require(!IsWindow(overlay), "Destroy must remove preview resources");
        DestroyWindow(parent);
        std::cout << checks << " IME checks passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
