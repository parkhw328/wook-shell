"""Apply small, checked integration patches to the pinned upstream archive."""
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / ".deps/putty"


def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f"Upstream patch context changed: {old[:90]!r}")
    return text.replace(old, new)


with zipfile.ZipFile(ROOT / ".tools/downloads/putty-src.zip") as archive:
    def original(name):
        return archive.read(name).decode("utf-8").replace("\r\n", "\n")

    window = original("windows/window.c")
    window = replace(window, "int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)",
                     "int WINAPI wshellTerminalMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)")
    window = replace(window, "HINSTANCE hinst;", '#include "wook-window.h"\n\nHINSTANCE hinst;')
    window = replace(window, "    SetWindowLongPtr(wgs->term_hwnd, GWLP_USERDATA, (LONG_PTR)wgs);",
                     "    SetWindowLongPtr(wgs->term_hwnd, GWLP_USERDATA, (LONG_PTR)wgs);\n    wgs->wshell_ime = wsImeCreate(wgs->term_hwnd, wookImeSend, mk_wcwidth, wgs);\n    SetWindowLongPtr(wgs->term_hwnd, GWL_STYLE, GetWindowLongPtr(wgs->term_hwnd, GWL_STYLE) | WS_CLIPCHILDREN);")
    window = replace(window, "static void wgs_cleanup(WinGuiSeat *wgs)\n{",
                     "static void wgs_cleanup(WinGuiSeat *wgs)\n{\n    wsImeDestroy(wgs->wshell_ime);")
    window = replace(window, "    ImmSetCompositionWindow(hIMC, &cf);",
                     "    ImmSetCompositionWindow(hIMC, &cf);\n    wookImePosition(wgs);")
    # WM_IME_CHAR is UTF-16 in a Unicode window, not a pair of legacy DBCS bytes.
    window = replace(window, "      case WM_IME_CHAR:\n        if (wParam & 0xFF00) {",
                     "      case WM_IME_CHAR:\n        if (unicode_window)\n            return WndProc(hwnd, WM_CHAR, wParam, lParam);\n        if (wParam & 0xFF00) {")
    # Do not keep a consumed high surrogate around for an unrelated later input.
    window = replace(window, "                term_keyinputw(wgs->term, pair, 2);",
                     "                wgs->pending_surrogate = 0;\n                term_keyinputw(wgs->term, pair, 2);")
    window = replace(window, "                term_keyinputw(wgs->term, &c, 1);",
                     "                wgs->pending_surrogate = 0;\n                term_keyinputw(wgs->term, &c, 1);")
    window = replace(window, "    dll_hijacking_protection();", "    dll_hijacking_protection();\n    wookWindowInit();")
    window = replace(window, "    ShowWindow(wgs->term_hwnd, show);\n    SetForegroundWindow(wgs->term_hwnd);",
                     "    if (!wookParent) {\n        ShowWindow(wgs->term_hwnd, show);\n        SetForegroundWindow(wgs->term_hwnd);\n    }")
    window = replace(window, "    gui_terminal_ready(wgs->term_hwnd, &wgs->seat, wgs->backend);",
                     "    wookWindowAttach(wgs->term_hwnd);\n    if (wookParent) reset_window(wgs, 2);\n    gui_terminal_ready(wgs->term_hwnd, &wgs->seat, wgs->backend);")
    window = replace(window, "    gui_term_process_cmdline(wgs->conf, cmdline);",
                     """    gui_term_process_cmdline(wgs->conf, cmdline);
    /* All wShell sessions stay independent of installed agents and sharing services. */
    wookIndependentConf(wgs->conf);
    if (wookParent) conf_set_bool(wgs->conf, CONF_warn_on_close, false);""")
    window = replace(window, "static void start_backend(WinGuiSeat *wgs)\n{",
                     "static void wookIndependentConf(Conf *conf);\nstatic void wookResetPassword(void);\n\nstatic void start_backend(WinGuiSeat *wgs)\n{\n    wookResetPassword();\n    wookIndependentConf(wgs->conf);")
    window = replace(window, "    spr = cmdline_get_passwd_input(p, &wgs->cmdline_get_passwd_state, true);",
                     "    if (wookSavedPassword(wgs->conf, p)) return SPR_OK;\n    spr = cmdline_get_passwd_input(p, &wgs->cmdline_get_passwd_state, true);")
    window = replace(window, '    char *title = dupprintf("%s Fatal Error", appname);\n    show_mouseptr(wgs, true);\n    MessageBox(wgs->term_hwnd, msg, title, MB_ICONERROR | MB_OK);', '''    char *title = dupprintf("%s Connection Error", appname);
    char *auth_help = NULL;
    if (strstr(msg, "No supported authentication methods available")) {
        auth_help = dupprintf("%s\\n\\nCheck the server's allowed methods above. For publickey authentication, select the matching private key in Edit host > Public key authentication.\\n\\nRegister its public .pub key on the server for this username. A .pub file alone cannot sign in. Import PEM/OpenSSH keys in the wShell key manager and save a PPK private key. GSSAPI requires a separately configured organization account.", msg);
    }
    show_mouseptr(wgs, true);
    MessageBox(wgs->term_hwnd, auth_help ? auth_help : msg, title, MB_ICONERROR | MB_OK);
    sfree(auth_help);''')
    window = replace(window, "            conf_cache_data(wgs);",
                     "            wookIndependentConf(wgs->conf);\n            conf_cache_data(wgs);")
    window = replace(window, "    switch (message) {\n      case WM_CREATE:",
                     "    if (wookImeMessage(wgs, message, wParam, &lParam)) return 0;\n    if (wookKey(hwnd, message, wParam, lParam)) return 0;\n    switch (message) {\n      case WM_APP + 60:\n        if (wookParent && wgs) {\n            wsImeClear(wgs->wshell_ime);\n            close_session(wgs);\n            term_pwron(wgs->term, false);\n            start_backend(wgs);\n        }\n        return 0;\n      case WM_CREATE:")
    window = replace(window, "static void clear_full_screen(WinGuiSeat *wgs)\n{",
                     "static void clear_full_screen(WinGuiSeat *wgs)\n{\n    if (wookParent) return;")
    window = replace(window, "static bool is_full_screen(WinGuiSeat *wgs)\n{",
                     "static bool is_full_screen(WinGuiSeat *wgs)\n{\n    if (wookParent) return false;")
    window = replace(window, "static void make_full_screen(WinGuiSeat *wgs)\n{",
                     "static void make_full_screen(WinGuiSeat *wgs)\n{\n    if (wookParent) { PostMessageW(wookParent, WM_APP + 43, 8, (LPARAM)wgs->term_hwnd); return; }")
    window = replace(window, "                if (nflg != flag || nexflag != exflag) {",
                     "                if (wookParent) {\n                    nflg &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU);\n                    nexflag &= ~(WS_EX_CLIENTEDGE | WS_EX_TOPMOST);\n                }\n                if (nflg != flag || nexflag != exflag) {")
    # Route upstream new/duplicate/saved-session menus into managed tabs.
    window = replace(window, "          case IDM_SAVEDSESS: {", '''          case IDM_SAVEDSESS: {
            if (wookParent) {
                if (wParam == IDM_SAVEDSESS) {
                    unsigned int sessno = ((lParam - IDM_SAVED_MIN) / MENU_SAVED_STEP) + 1;
                    if (sessno < (unsigned)sesslist.nsessions) {
                        const char *name = sesslist.sessions[sessno];
                        COPYDATASTRUCT data = {44, (DWORD)strlen(name) + 1, (void *)name};
                        SendMessageW(wookParent, WM_COPYDATA, (WPARAM)hwnd, (LPARAM)&data);
                    }
                } else PostMessageW(wookParent, WM_APP + 43, wParam == IDM_DUPSESS ? 2 : 1, (LPARAM)hwnd);
                break;
            }''')
    (SOURCE / "windows/window.c").write_text(window, encoding="utf-8")
    seat = original("windows/win-gui-seat.h")
    seat = replace(seat, "    HWND term_hwnd;", "    HWND term_hwnd;\n    struct WsIme *wshell_ime;")
    (SOURCE / "windows/win-gui-seat.h").write_text(seat, encoding="utf-8")

    # Explicit, locally assigned authentication metadata: never infer password prompts from server text.
    header = original("putty.h")
    header = replace(header, "struct prompts_t {", '''struct prompts_t {
    /* Borrowed until this prompt is freed; only populated for SSH password authentication. */
    const char *wshell_password_host, *wshell_password_user;
    int wshell_password_port;''')
    (SOURCE / "putty.h").write_text(header, encoding="utf-8")
    prompts = original("utils/prompts.c")
    prompts = replace(prompts, "    prompts_t *p = snew(prompts_t);", '''    prompts_t *p = snew(prompts_t);
    p->wshell_password_host = p->wshell_password_user = NULL;
    p->wshell_password_port = 0;''')
    (SOURCE / "utils/prompts.c").write_text(prompts, encoding="utf-8")
    auth = original("ssh/userauth2-client.c")
    auth = replace(auth, '                s->cur_prompt->name = dupstr("SSH password");', '''                s->cur_prompt->name = dupstr("SSH password");
                s->cur_prompt->wshell_password_host = s->hostname;
                s->cur_prompt->wshell_password_port = s->port;
                s->cur_prompt->wshell_password_user = s->username;''')
    (SOURCE / "ssh/userauth2-client.c").write_text(auth, encoding="utf-8")

    putty = original("windows/putty.c")
    putty = replace(putty, "static strbuf *demo_terminal_data = NULL;", "static strbuf *demo_terminal_data = NULL;\nstatic bool wookPreview = false;")
    putty = replace(putty, "static bool wookPreview = false;", "static bool wookPreview = false;\nstatic bool wookLocal = false;\nextern const BackendVtable conpty_backend;")
    putty = replace(putty, "    bool demo_config_box = false;", "    bool demo_config_box = false;\n    bool wookConfig = false;")
    putty = replace(putty, '            } else if (!strcmp(p, "-cleanup")) {', '''            } else if (!strcmp(p, "--terminal")) {
                /* Internal mode of the single wShell executable. */
            } else if (!strcmp(p, "-wook-config")) {
                wookConfig = true;
            } else if (!strcmp(p, "-wook-local")) {
                wookLocal = true;
            } else if (!strcmp(p, "-wook-preview")) {
                wookPreview = true;
                demo_terminal_data = strbuf_new();
                put_dataz(demo_terminal_data,
                    "\\033[2J\\033[H\\r\\n  \\033[38;2;58;169;159mwShell\\033[0m  /  Terminal preview\\r\\n\\r\\n"
                    "  A little color. A lot of possibility.\\r\\n"
                    "  Flexoki Dark + JetBrains Mono\\r\\n\\r\\n"
                    "  \\033[32mSSH\\033[0m   \\033[34mUTF-8\\033[0m   \\033[35m256 colors\\033[0m   \\033[36mTrue Color\\033[0m\\r\\n\\r\\n"
                    "  \\033[31mred   \\033[32mgreen   \\033[33myellow   \\033[34mblue   \\033[35mmagenta   \\033[36mcyan\\033[0m\\r\\n\\r\\n");
                for (int i = 0; i < 64; ++i)
                    put_fmt(demo_terminal_data, "\\033[48;2;%d;%d;%dm ", 45 + i * 2, 100 + i, 145 - i);
                put_dataz(demo_terminal_data, "\\033[0m\\r\\n\\r\\n  \\033[2mLocal preview only. No server is connected.\\033[0m\\r\\n");
            } else if (!strcmp(p, "-cleanup")) {''')
    putty = replace(putty, "    if (demo_config_box) {", '''    if (wookConfig) {
        if (!do_config(conf)) cleanup_exit(0);
        const char *name = getenv("WOOK_EDIT_SESSION");
        if (name && *name) {
            char *err = save_settings(name, conf);
            if (err) { nonfatal("%s", err); sfree(err); cleanup_exit(1); }
        }
        special_launchable_argument = true;
    }
    if (demo_config_box) {''')
    putty = replace(putty, "    cmdline_run_saved(conf);", '''    cmdline_run_saved(conf);
    if (wookLocal) {
        special_launchable_argument = true;
        conf_set_int(conf, CONF_protocol, -1);
    }''')
    putty = replace(putty, "const struct BackendVtable *backend_vt_from_conf(Conf *conf)\n{", "const struct BackendVtable *backend_vt_from_conf(Conf *conf)\n{\n    if (wookLocal) return &conpty_backend;")
    putty = replace(putty, '        load_open_settings(NULL, conf);\n        conf_set_str(conf, CONF_host, "demo-server.example.com");\n        conf_set_int(conf, CONF_close_on_exit, FORCE_OFF);',
                    '        if (!wookPreview) load_open_settings(NULL, conf);\n        conf_set_str(conf, CONF_host, "demo-server.example.com");\n        conf_set_int(conf, CONF_close_on_exit, FORCE_OFF);')
    putty = replace(putty, "        schedule_timer(TICKSPERSEC, demo_terminal_screenshot, (void *)hwnd);", "        if (!wookPreview) schedule_timer(TICKSPERSEC, demo_terminal_screenshot, (void *)hwnd);")
    putty = replace(putty, 'return L"SimonTatham.PuTTY";', 'return L"WookShell.Terminal";')
    (SOURCE / "windows/putty.c").write_text(putty, encoding="utf-8")
    manifest = original("windows/putty.mft")
    manifest = replace(manifest, "       <dpiAware>true</dpiAware>",
                       '       <dpiAware>true</dpiAware>\n       <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>')
    (SOURCE / "windows/putty.mft").write_text(manifest, encoding="utf-8")
    resource = original("windows/putty.rc")
    resource = resource.replace('#define APPNAME "PuTTY"', '#define APPNAME "wShell Terminal (modified PuTTY)"')
    resource = replace(resource, 'IDI_MAINICON ICON "putty.ico"', 'IDI_MAINICON ICON "wshell.ico"')
    resource = replace(resource, 'IDI_CFGICON ICON "puttycfg.ico"', 'IDI_CFGICON ICON "wshell.ico"')
    (SOURCE / "windows/putty.rc").write_text(resource, encoding="utf-8")
    common = original("windows/putty-common.rc2").replace('#include "version.rc2"', '')
    common = common.replace('FONT 8, "MS Shell Dlg"', 'FONT 9, "JetBrains Mono"')
    (SOURCE / "windows/wshell-common.rc2").write_text(common, encoding="utf-8")
    resource = resource.replace('#include "putty-common.rc2"', '#include "wshell-common.rc2"')
    resource = replace(resource, '1 RT_MANIFEST "putty.mft"', '')
    (SOURCE / "windows/wshell-terminal.rc").write_text(resource, encoding="utf-8")

    # Do not present switches for external agent/sharing features that wShell disables.
    config = original("config.c")
    config = replace(config, '''        if (!midsession) {
            s = ctrl_getset(b, "Connection/SSH", "sharing", "Sharing an SSH connection between PuTTY tools");

            ctrl_checkbox(s, "Share SSH connections if possible", 's',
                          HELPCTX(ssh_share),
                          conf_checkbox_handler,
                          I(CONF_ssh_connection_sharing));

            ctrl_text(s, "Permitted roles in a shared connection:",
                      HELPCTX(ssh_share));
            ctrl_checkbox(s, "Upstream (connecting to the real server)", 'u',
                          HELPCTX(ssh_share),
                          conf_checkbox_handler,
                          I(CONF_ssh_connection_sharing_upstream));
            ctrl_checkbox(s, "Downstream (connecting to the upstream PuTTY)", 'd',
                          HELPCTX(ssh_share),
                          conf_checkbox_handler,
                          I(CONF_ssh_connection_sharing_downstream));
        }

''', '')
    config = replace(config, '''            ctrl_checkbox(s, "Attempt authentication using Pageant", 'p',
                          HELPCTX(ssh_auth_pageant),
                          conf_checkbox_handler,
                          I(CONF_tryagent));
''', '')
    config = replace(config, '''            ctrl_checkbox(s, "Allow agent forwarding", 'f',
                          HELPCTX(ssh_auth_agentfwd),
                          conf_checkbox_handler, I(CONF_agentfwd));
''', '')
    (SOURCE / "config.c").write_text(config, encoding="utf-8")

    dialog = original("windows/dialog.c")
    start = dialog.index("static INT_PTR GenericMainDlgProc(")
    end = dialog.index("\nvoid modal_about_box(", start)
    dialog = replace(dialog, dialog[start:end], '#include "wshell-config.h"\n')
    start = dialog.index("static INT_PTR CAConfigProc(")
    end = dialog.index("\nvoid show_ca_config_box(", start)
    dialog = replace(dialog, dialog[start:end], '''static INT_PTR CAConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, void *ctx)
{
    return GenericMainDlgProc(hwnd, msg, wp, lp, ctx);
}
''')
    dialog = replace(dialog, '''void show_ca_config_box(dlgparam *dp)
{
    PortableDialogStuff *pds = pds_new(1);''', '''void show_ca_config_box(dlgparam *dp)
{
    PortableDialogStuff *pds = pds_new(2);''')
    (SOURCE / "windows/dialog.c").write_text(dialog, encoding="utf-8")
    controls = original("windows/controls.c")
    controls = replace(controls, '#include "dialog.h"', '#include "dialog.h"\n#include "wshell-settings-ui.h"')
    controls = replace(controls, "    if (cp->hwnd) {\n        ctl = CreateWindowEx", '''    if (cp->hwnd) {
        if (GetPropW(cp->hwnd, L"wShell.SettingsPage")) {
            if (!strcmp(wclass, "LISTBOX")) wstyle = (wstyle & ~WS_VSCROLL) | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
            if (!strcmp(wclass, "COMBOBOX")) wstyle |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        }
        ctl = CreateWindowEx''')
    controls = replace(controls, "        SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(true, 0));",
                       "        SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(true, 0));\n        wsSettingsStyleControl(ctl);")
    controls = replace(controls, "    if (name)\n        cp->ypos += STATICHEIGHT;",
                       "    if (name)\n        cp->ypos += STATICHEIGHT + (GetPropW(cp->hwnd, L\"wShell.SettingsPage\") ? 8 : 0);")
    controls = replace(controls, '''          cp->boxtext ? cp->boxtext : "", cp->boxid);
    cp->ypos += GAPYBOX;''', '''          cp->boxtext ? cp->boxtext : "", cp->boxid);
    cp->ypos += GAPYBOX + (GetPropW(cp->hwnd, L"wShell.SettingsPage") ? 4 : 0);''')
    (SOURCE / "windows/controls.c").write_text(controls, encoding="utf-8")

    cmake = original("windows/CMakeLists.txt")
    cmake = replace(cmake, "  storage.c)", "  storage.c wook-store.c)")
    cmake = replace(cmake, "  storage.c\n", "  storage.c\n  wook-store.c\n")
    cmake = replace(cmake, "printing.c jump-list.c sizetip.c", "printing.c no-jump-list.c sizetip.c")
    cmake = replace(cmake, 'message("ConPTY not available; cannot build Windows pterm")',
                    'message(STATUS "ConPTY not available; optional pterm is not built")')
    cmake += '''
if(WSHELL_ROOT)
  add_library(wshell-terminal OBJECT window.c putty.c conpty.c help.c ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
  target_compile_definitions(wshell-terminal PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
  be_list(wshell-terminal wShell SSH SERIAL OTHERBACKENDS)
  add_dependencies(wshell-terminal generated_licence_h)
  add_library(wshell-keys STATIC "${WSHELL_ROOT}/src/keys.c")
  target_include_directories(wshell-keys PRIVATE "${WSHELL_ROOT}/src")
  add_subdirectory("${WSHELL_ROOT}" "${CMAKE_BINARY_DIR}/workspace")
endif()
'''
    (SOURCE / "windows/CMakeLists.txt").write_text(cmake, encoding="utf-8")
    nojump = original("windows/no-jump-list.c")
    nojump += "\nbool set_explicit_app_user_model_id(void) { return true; }\n"
    (SOURCE / "windows/no-jump-list.c").write_text(nojump, encoding="utf-8")

shutil.copyfile(ROOT / "patches/portable-storage.c", SOURCE / "windows/storage.c")
shutil.copyfile(ROOT / "patches/wook-window.h", SOURCE / "windows/wook-window.h")
shutil.copyfile(ROOT / "patches/wshell-config.h", SOURCE / "windows/wshell-config.h")
shutil.copyfile(ROOT / "src/settings_ui.h", SOURCE / "windows/wshell-settings-ui.h")
shutil.copyfile(ROOT / "src/credentials.h", SOURCE / "windows/wshell-credentials.h")
shutil.copyfile(ROOT / "src/ime.h", SOURCE / "windows/wshell-ime.h")
shutil.copyfile(ROOT / "src/store.c", SOURCE / "windows/wook-store.c")
shutil.copyfile(ROOT / "src/store.h", SOURCE / "windows/wook-store.h")
# Shared implementation includes this short local filename.
shutil.copyfile(ROOT / "src/store.h", SOURCE / "windows/store.h")
shutil.copyfile(ROOT / "assets/wshell.ico", SOURCE / "windows/wshell.ico")
print("Applied portable storage, private fonts, preview, and tab-host integration.")
