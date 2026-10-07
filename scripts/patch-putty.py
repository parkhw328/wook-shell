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
                     "static void wookIndependentConf(Conf *conf);\n\nstatic void start_backend(WinGuiSeat *wgs)\n{\n    wookIndependentConf(wgs->conf);")
    window = replace(window, "            conf_cache_data(wgs);",
                     "            wookIndependentConf(wgs->conf);\n            conf_cache_data(wgs);")
    window = replace(window, "    switch (message) {\n      case WM_CREATE:",
                     "    if (wookKey(hwnd, message, wParam, lParam)) return 0;\n    switch (message) {\n      case WM_APP + 60:\n        if (wookParent && wgs) {\n            close_session(wgs);\n            term_pwron(wgs->term, false);\n            start_backend(wgs);\n        }\n        return 0;\n      case WM_CREATE:")
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

    putty = original("windows/putty.c")
    putty = replace(putty, "static strbuf *demo_terminal_data = NULL;", "static strbuf *demo_terminal_data = NULL;\nstatic bool wookPreview = false;")
    putty = replace(putty, "    bool demo_config_box = false;", "    bool demo_config_box = false;\n    bool wookConfig = false;")
    putty = replace(putty, '            } else if (!strcmp(p, "-cleanup")) {', '''            } else if (!strcmp(p, "--terminal")) {
                /* Internal mode of the single wShell executable. */
            } else if (!strcmp(p, "-wook-config")) {
                wookConfig = true;
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

    cmake = original("windows/CMakeLists.txt")
    cmake = replace(cmake, "  storage.c)", "  storage.c wook-store.c)")
    cmake = replace(cmake, "  storage.c\n", "  storage.c\n  wook-store.c\n")
    cmake = replace(cmake, "printing.c jump-list.c sizetip.c", "printing.c no-jump-list.c sizetip.c")
    cmake = replace(cmake, 'message("ConPTY not available; cannot build Windows pterm")',
                    'message(STATUS "ConPTY not available; optional pterm is not built")')
    cmake += '''
if(WSHELL_ROOT)
  add_library(wshell-terminal OBJECT window.c putty.c help.c ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
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
shutil.copyfile(ROOT / "src/store.c", SOURCE / "windows/wook-store.c")
shutil.copyfile(ROOT / "src/store.h", SOURCE / "windows/wook-store.h")
# Shared implementation includes this short local filename.
shutil.copyfile(ROOT / "src/store.h", SOURCE / "windows/store.h")
shutil.copyfile(ROOT / "assets/wshell.ico", SOURCE / "windows/wshell.ico")
print("Applied portable storage, private fonts, preview, and tab-host integration.")
