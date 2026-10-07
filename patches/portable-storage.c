/* Wook Shell portable storage adapter. PuTTY's crypto is unchanged. MIT. */
#include "putty.h"
#include "storage.h"
#include "wook-store.h"
#include <limits.h>

struct settings_w { WsStore *store; };
struct settings_r { WsStore *store; };
struct settings_e { char **names; size_t count, index; };
struct host_ca_enum { char **names; size_t count, index; };

static WsStore *openPortable(const wchar_t *category, const char *name, bool write) {
    wchar_t *path = wsPath(category, name);
    WsStore *store = wsOpen(path, write);
    free(path);
    return store;
}

settings_w *open_settings_w(const char *name, char **errmsg) {
    *errmsg = NULL;
    WsStore *store = openPortable(L"sessions", name && *name ? name : "Default Settings", true);
    if (!store) { *errmsg = dupstr("Cannot open portable session file. Check folder permissions, name length, or file corruption."); return NULL; }
    settings_w *out = snew(settings_w); out->store = store; return out;
}
void write_setting_s(settings_w *h, const char *key, const char *value) { if (h) wsSet(h->store, key, value); }
void write_setting_i(settings_w *h, const char *key, int value) {
    char text[40]; snprintf(text, sizeof(text), "%d", value); write_setting_s(h, key, text);
}
void close_settings_w(settings_w *h) {
    if (!h) return;
    if (!wsSave(h->store)) nonfatal("Could not save portable settings. Check free space and folder permissions.");
    wsClose(h->store); sfree(h);
}
settings_r *open_settings_r(const char *name) {
    WsStore *store = openPortable(L"sessions", name && *name ? name : "Default Settings", false);
    if (!store || !store->exists) { wsClose(store); return NULL; }
    settings_r *out = snew(settings_r); out->store = store; return out;
}
char *read_setting_s(settings_r *h, const char *key) {
    const char *value = wsGet(h ? h->store : NULL, key); return value ? dupstr(value) : NULL;
}
int read_setting_i(settings_r *h, const char *key, int fallback) {
    const char *value = wsGet(h ? h->store : NULL, key);
    if (!value) return fallback;
    char *end; long n = strtol(value, &end, 10);
    return *end || n < INT_MIN || n > INT_MAX ? fallback : (int)n;
}
void close_settings_r(settings_r *h) { if (h) { wsClose(h->store); sfree(h); } }
FontSpec *read_setting_fontspec(settings_r *h, const char *name) {
    char *font = read_setting_s(h, name);
    if (!font) return NULL;
    char *key = dupcat(name, "IsBold"); int bold = read_setting_i(h, key, 0); sfree(key);
    key = dupcat(name, "Height"); int height = read_setting_i(h, key, 11); sfree(key);
    key = dupcat(name, "CharSet"); int charset = read_setting_i(h, key, DEFAULT_CHARSET); sfree(key);
    FontSpec *out = fontspec_new(font, bold, height, charset); sfree(font); return out;
}
void write_setting_fontspec(settings_w *h, const char *name, FontSpec *font) {
    write_setting_s(h, name, font->name);
    char *key = dupcat(name, "IsBold"); write_setting_i(h, key, font->isbold); sfree(key);
    key = dupcat(name, "Height"); write_setting_i(h, key, font->height); sfree(key);
    key = dupcat(name, "CharSet"); write_setting_i(h, key, font->charset); sfree(key);
}
Filename *read_setting_filename(settings_r *h, const char *name) {
    char *text = read_setting_s(h, name);
    if (!text) return NULL;
    wchar_t *unicode = dup_mb_to_wc(CP_UTF8, text);
    Filename *file = filename_from_wstr(unicode); sfree(unicode); sfree(text); return file;
}
void write_setting_filename(settings_w *h, const char *name, Filename *file) {
    char *utf8 = dup_wc_to_mb(CP_UTF8, filename_to_wstr(file), NULL);
    write_setting_s(h, name, utf8); sfree(utf8);
}
void del_settings(const char *name) {
    wchar_t *path = wsPath(L"sessions", name);
    if (!wsRemove(path)) nonfatal("Cannot delete portable session.");
    free(path);
}
settings_e *enum_settings_start(void) {
    settings_e *e = snew(settings_e); e->index = 0; e->names = wsList(L"sessions", &e->count); return e;
}
bool enum_settings_next(settings_e *e, strbuf *out) {
    if (!e || e->index >= e->count) return false;
    put_data(out, e->names[e->index], strlen(e->names[e->index])); ++e->index; return true;
}
void enum_settings_finish(settings_e *e) { if (e) { wsFreeList(e->names, e->count); sfree(e); } }

int check_stored_host_key(const char *host, int port, const char *type, const char *key) {
    WsStore *store = openPortable(L"trust", "hostkeys", false);
    /* Unreadable trust data is not treated as a new/unknown host. */
    if (!store) { nonfatal("Cannot read portable host keys. Repair file permissions or restore your trust database."); return 2; }
    char *name = dupprintf("%s@%d:%s", type, port, host);
    const char *saved = wsGet(store, name);
    int result = saved ? (strcmp(saved, key) ? 2 : 0) : 1;
    sfree(name); wsClose(store); return result;
}
bool have_ssh_host_key(const char *host, int port, const char *type) { return check_stored_host_key(host, port, type, "") != 1; }
void store_host_key(Seat *seat, const char *host, int port, const char *type, const char *key) {
    (void)seat;
    WsStore *store = openPortable(L"trust", "hostkeys", true);
    char *name = dupprintf("%s@%d:%s", type, port, host);
    if (!store || !wsSet(store, name, key) || !wsSave(store)) nonfatal("The host key could not be saved to the portable trust database.");
    sfree(name); wsClose(store);
}
host_ca_enum *enum_host_ca_start(void) {
    host_ca_enum *e = snew(host_ca_enum); e->index = 0; e->names = wsList(L"cas", &e->count); return e;
}
bool enum_host_ca_next(host_ca_enum *e, strbuf *out) {
    if (!e || e->index >= e->count) return false;
    put_data(out, e->names[e->index], strlen(e->names[e->index])); ++e->index; return true;
}
void enum_host_ca_finish(host_ca_enum *e) { if (e) { wsFreeList(e->names, e->count); sfree(e); } }
host_ca *host_ca_load(const char *name) {
    WsStore *store = openPortable(L"cas", name, false);
    if (!store || !store->exists) { wsClose(store); return NULL; }
    host_ca *ca = host_ca_new(); ca->name = dupstr(name);
    const char *s = wsGet(store, "PublicKey"); if (s) ca->ca_public_key = base64_decode_sb(ptrlen_from_asciz(s));
    s = wsGet(store, "Validity"); if (s) ca->validity_expression = dupstr(s);
    s = wsGet(store, "PermitRSASHA1"); if (s) ca->opts.permit_rsa_sha1 = atoi(s);
    s = wsGet(store, "PermitRSASHA256"); if (s) ca->opts.permit_rsa_sha256 = atoi(s);
    s = wsGet(store, "PermitRSASHA512"); if (s) ca->opts.permit_rsa_sha512 = atoi(s);
    wsClose(store); return ca;
}
char *host_ca_save(host_ca *ca) {
    if (!ca->name || !*ca->name || !ca->ca_public_key) return dupstr("CA name and public key are required.");
    WsStore *store = openPortable(L"cas", ca->name, true);
    if (!store) return dupstr("Cannot open portable CA file.");
    strbuf *key = base64_encode_sb(ptrlen_from_strbuf(ca->ca_public_key), 0);
    wsSet(store, "PublicKey", key->s); strbuf_free(key);
    wsSet(store, "Validity", ca->validity_expression ? ca->validity_expression : "");
    wsSet(store, "PermitRSASHA1", ca->opts.permit_rsa_sha1 ? "1" : "0");
    wsSet(store, "PermitRSASHA256", ca->opts.permit_rsa_sha256 ? "1" : "0");
    wsSet(store, "PermitRSASHA512", ca->opts.permit_rsa_sha512 ? "1" : "0");
    int ok = wsSave(store); wsClose(store); return ok ? NULL : dupstr("Cannot save portable CA file.");
}
char *host_ca_delete(const char *name) {
    wchar_t *path = wsPath(L"cas", name); int ok = wsRemove(path); free(path);
    return ok ? NULL : dupstr("Cannot delete portable CA file.");
}

/* PuTTY obtains fresh OS entropy (CryptGenRandom). No seed is persisted. */
void read_random_seed(noise_consumer_t consumer) { (void)consumer; }
void write_random_seed(void *data, int len) { (void)data; (void)len; }
void cleanup_all(void) { nonfatal("Portable data is managed by Wook Shell. Back up or remove its data folder manually."); }
int add_to_jumplist_registry(const char *item) { (void)item; return 0; }
int remove_from_jumplist_registry(const char *item) { (void)item; return 0; }
char *get_jumplist_registry_entries(void) { return dupstr(""); }
