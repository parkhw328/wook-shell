/* wShell's settings view. Keep PuTTY's option model and validation handlers. */
#include "wshell-settings-ui.h"
#include "wshell-key-import.h"

static void wshell_select_page(void *context, const char *path)
{
    PortableDialogStuff *pds = context;
    HWND page = pds->dp->hwnd;
    HWND window = GetAncestor(page, GA_ROOT);
    SendMessage(page, WM_SETREDRAW, false, 0);
    pds->initialised = false;
    struct winctrl *c;
    while ((c = winctrl_findbyindex(&pds->ctrltrees[TREE_PANEL], 0))) {
        for (int i = 0; i < c->num_ids; i++) {
            HWND control = GetDlgItem(page, c->base_id + i);
            if (control) DestroyWindow(control);
        }
        winctrl_rem_shortcuts(pds->dp, c);
        winctrl_remove(&pds->ctrltrees[TREE_PANEL], c);
        sfree(c->data);
        sfree(c);
    }
    pds->dp->focused = pds->dp->lastfocused = NULL;
    struct ctlpos cp;
    ctlposinit(&cp, page, 4, 4, 4);
    int id = IDCX_PANELBASE;
    for (int index = -1; (index = ctrl_find_path(pds->ctrlbox, path, index)) >= 0;) {
        struct controlset *s = pds->ctrlbox->ctrlsets[index];
        /* The wShell header replaces the legacy etched panel title. */
        if (!s->boxname) continue;
        winctrl_layout(pds->dp, &pds->ctrltrees[TREE_PANEL], &cp, s, &id);
    }
    dlg_refresh(NULL, pds->dp);
    wsSettingsPageReady(window, path);
    pds->initialised = true;
    SendMessage(page, WM_SETREDRAW, true, 0);
    RedrawWindow(page, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void wshell_accept_settings(void *context, int accept)
{
    PortableDialogStuff *pds = context;
    bool authorities = ctrl_find_path(pds->ctrlbox, "Main", -1) >= 0;
    if (accept && !authorities) {
        Conf *conf = pds->dp->data;
        Filename *source = conf_get_filename(conf, CONF_keyfile);
        if (conf_get_int(conf, CONF_protocol) == PROT_SSH && !filename_is_null(source)) {
            wchar_t *prepared = wsPreparePrivateKey(GetAncestor(pds->dp->hwnd, GA_ROOT), filename_to_wstr(source));
            if (!prepared) return;
            Filename *key = filename_from_wstr(prepared);
            conf_set_filename(conf, CONF_keyfile, key);
            filename_free(key); free(prepared);
        }
    }
    for (int i = 0; i < pds->ctrlbox->nctrlsets; ++i) {
        struct controlset *s = pds->ctrlbox->ctrlsets[i];
        if (*s->pathname) continue;
        for (int j = 0; j < s->ncontrols; ++j) {
            dlgcontrol *c = s->ctrls[j];
            if (c->type == CTRL_BUTTON && (accept && !authorities ? c->button.isdefault : c->button.iscancel)) {
                c->handler(c, pds->dp, pds->dp->data, EVENT_ACTION);
                if (pds->dp->ended)
                    ShinyEndDialog(GetAncestor(pds->dp->hwnd, GA_ROOT), pds->dp->endresult);
                return;
            }
        }
    }
}

static INT_PTR GenericMainDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, void *context)
{
    PortableDialogStuff *pds = context;
    INT_PTR result = 0;
    if (msg == WM_INITDIALOG) {
        pds_initdialog_start(pds, hwnd);
        enum WsSettingsKind kind = ctrl_find_path(pds->ctrlbox, "Main", -1) >= 0 ? WS_SETTINGS_AUTHORITIES :
            pds->dp->wintitle && strstr(pds->dp->wintitle, "Reconfiguration") ? WS_SETTINGS_SESSION : WS_SETTINGS_CONNECTION;
        pds->dp->hwnd = wsSettingsBegin(hwnd, kind, pds, wshell_select_page, wshell_accept_settings);
        if (!pds->dp->hwnd) { ShinyEndDialog(hwnd, 0); return 0; }
        const char *last = "";
        for (int i = 0; i < pds->ctrlbox->nctrlsets; ++i) {
            struct controlset *s = pds->ctrlbox->ctrlsets[i];
            if (!*s->pathname || !strcmp(last, s->pathname)) continue;
            last = s->pathname;
            strbuf *keywords = strbuf_new();
            for (int j = i; j < pds->ctrlbox->nctrlsets && !strcmp(pds->ctrlbox->ctrlsets[j]->pathname, last); ++j) {
                struct controlset *group = pds->ctrlbox->ctrlsets[j];
                if (group->boxtitle) put_fmt(keywords, " %s", group->boxtitle);
                for (int k = 0; k < group->ncontrols; ++k)
                    if (group->ctrls[k]->label) put_fmt(keywords, " %s", group->ctrls[k]->label);
            }
            wsSettingsAddPage(hwnd, last, s->boxtitle ? s->boxtitle : "", keywords->s);
            strbuf_free(keywords);
        }
        wsSettingsReady(hwnd);
        pds->initialised = true;
        ShowWindow(hwnd, SW_SHOWNORMAL);
        return TRUE;
    }
    if (msg == WM_DESTROY) { wsSettingsEnd(hwnd); return 0; }
    if (wsSettingsMessage(hwnd, msg, wp, lp, &result)) return result;
    return pds_default_dlgproc(pds, hwnd, msg, wp, lp);
}
