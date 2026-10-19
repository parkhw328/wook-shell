/* Raw SSH subsystem bridge. Compiled into the same executable as the GUI. */
#include "wshell-prompt.h"
extern bool wookSftp;
static bool wookSftpReady;
static struct handle *wookSftpInput, *wookSftpStdout;
static size_t wookSftpInputData(struct handle *h, const void *data, size_t length, int error) {
    WinGuiSeat *wgs = handle_get_privdata(h);
    if (error || !length) { PostQuitMessage(error ? 1 : 0); return 0; }
    backend_send(wgs->backend, data, length);
    return backend_sendbuffer(wgs->backend);
}
static void wookSftpOutputSent(struct handle *h, size_t backlog, int error, bool close) {
    WinGuiSeat *wgs = handle_get_privdata(h);
    if (error || close) { PostQuitMessage(error ? 1 : 0); return; }
    if (wgs->backend) backend_unthrottle(wgs->backend, backlog);
}
static void wookSftpSent(Seat *seat, size_t backlog) {
    (void)seat;
    if (wookSftpInput) handle_unthrottle(wookSftpInput, backlog);
}
/* A transport queue can drain after the last Seat.sent notification (notably
 * after rekey). Match Plink's post-loop backlog check so a paused pipe wakes
 * even when no further channel/window or socket notification is generated. */
static void wookSftpResumeInput(WinGuiSeat *wgs) {
    if (wookSftp && wookSftpInput && wgs->backend)
        handle_unthrottle(wookSftpInput, backend_sendbuffer(wgs->backend));
}
static void wookSftpStarted(Seat *seat) {
    if (!wookSftp || wookSftpReady) return;
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);
    /* PuTTY's pipe workers signal the existing event loop. No timer quota,
     * and stdout cannot block SSH window updates while stdin is backlogged. */
    wookSftpStdout = handle_output_new(GetStdHandle(STD_OUTPUT_HANDLE), wookSftpOutputSent, wgs, 0);
    wookSftpReady = true;
    wookSftpInput = handle_input_new(GetStdHandle(STD_INPUT_HANDLE), wookSftpInputData, wgs, 0);
}
static size_t wookSftpOutput(SeatOutputType type, const void *data, size_t length) {
    if (!wookSftpReady || type != SEAT_OUTPUT_STDOUT) return 0;
    return handle_write(wookSftpStdout, data, length);
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
