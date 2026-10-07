/* Raw SSH subsystem bridge. Compiled into the same executable as the GUI. */
#include "wshell-prompt.h"
extern bool wookSftp;
static bool wookSftpReady;
static void wookSftpStarted(Seat *seat) { (void)seat; wookSftpReady = true; }
static void wookSftpPump(WinGuiSeat *wgs) {
    if (!wgs || !wgs->backend || !wookSftpReady || !backend_sendok(wgs->backend) || backend_sendbuffer(wgs->backend) > 262144) return;
    DWORD available, count; char buffer[65536];
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL)) { PostQuitMessage(0); return; }
    if (!available) return;
    if (!ReadFile(input, buffer, min(available, sizeof(buffer)), &count, NULL) || !count) { PostQuitMessage(0); return; }
    backend_send(wgs->backend, buffer, count);
}
static size_t wookSftpOutput(SeatOutputType type, const void *data, size_t length) {
    if (!wookSftpReady || type != SEAT_OUTPUT_STDOUT) return 0;
    const char *p = data;
    while (length) {
        DWORD count;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), p, (DWORD)min(length, 65536), &count, NULL) || !count) { PostQuitMessage(1); break; }
        p += count; length -= count;
    }
    return 0;
}
static SeatPromptResult wookSftpPrompt(prompts_t *p) {
    if (p->n_prompts > 16) return SPR_USER_ABORT;
    for (size_t i = 0; i < p->n_prompts; ++i) {
        char value[16385] = {0};
        char *label = dupprintf("%.2000s%s%.2000s", p->instruction ? p->instruction : "", p->instruction ? "\n\n" : "", p->prompts[i]->prompt);
        int ok = wsPrompt(wookParent, "wShell · SFTP authentication", label, !p->prompts[i]->echo, value, sizeof(value));
        sfree(label);
        if (ok) prompt_set_result(p->prompts[i], value);
        smemclr(value, sizeof(value));
        if (!ok) return SPR_USER_ABORT;
    }
    return SPR_OK;
}
