#include "wsftp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

enum { PACKET_LIMIT = 1024 * 1024, CHUNK = 32768, PIPELINE = 16 };
typedef struct Packet { unsigned char *data; size_t length, offset, capacity; int valid; } Packet;
struct WsFtp {
    void *context; WsFtpRead read; WsFtpWrite write;
    Packet in, out;
    uint32_t sequence; int status, replace, broken;
    char error[512];
};
static int fail(WsFtp *c, const char *message) {
    snprintf(c->error, sizeof(c->error), "%s", message); return 0;
}
static int reserve(Packet *p, size_t bytes) {
    if (bytes > PACKET_LIMIT || !p->valid) return p->valid = 0;
    if (p->capacity < bytes) {
        void *next = realloc(p->data, bytes);
        if (!next) return p->valid = 0;
        p->data = next; p->capacity = bytes;
    }
    return 1;
}
static void put(Packet *p, const void *data, size_t length) {
    if (length > PACKET_LIMIT - p->length || !reserve(p, p->length + length)) { p->valid = 0; return; }
    if (length) memcpy(p->data + p->length, data, length);
    p->length += length;
}
static void put32(Packet *p, uint32_t value) {
    unsigned char bytes[4] = {(unsigned char)(value >> 24), (unsigned char)(value >> 16), (unsigned char)(value >> 8), (unsigned char)value};
    put(p, bytes, 4);
}
static void put64(Packet *p, uint64_t value) { put32(p, (uint32_t)(value >> 32)); put32(p, (uint32_t)value); }
static void blob(Packet *p, const void *data, size_t length) { put32(p, (uint32_t)length); put(p, data, length); }
static void string(Packet *p, const char *value) {
    size_t length = strlen(value);
    if (length > 32768) { p->valid = 0; return; }
    blob(p, value, length);
}
static const unsigned char *take(Packet *p, size_t count) {
    if (!p->valid || count > p->length - p->offset) { p->valid = 0; return NULL; }
    const unsigned char *out = p->data + p->offset; p->offset += count; return out;
}
static uint32_t get32(Packet *p) {
    const unsigned char *b = take(p, 4);
    return b ? ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3] : 0;
}
static uint64_t get64(Packet *p) { uint64_t high = get32(p); return (high << 32) | get32(p); }
static const unsigned char *getBlob(Packet *p, uint32_t *length) { *length = get32(p); return take(p, *length); }
static int getString(Packet *p, char *output, size_t capacity) {
    uint32_t length; const unsigned char *data = getBlob(p, &length);
    if (!data || length >= capacity || memchr(data, 0, length)) { p->valid = 0; return 0; }
    memcpy(output, data, length); output[length] = 0; return 1;
}
static int attrs(Packet *p, WsFtpAttrs *a) {
    memset(a, 0, sizeof(*a)); a->flags = get32(p);
    if (a->flags & ~0x8000000fu) p->valid = 0;
    if (a->flags & 1) a->size = get64(p);
    if (a->flags & 2) { get32(p); get32(p); }
    if (a->flags & 4) a->permissions = get32(p);
    if (a->flags & 8) { get32(p); a->modified = get32(p); }
    if (a->flags & 0x80000000u) {
        uint32_t count = get32(p);
        if (count > 4096) p->valid = 0;
        while (count-- && p->valid) { uint32_t length; getBlob(p, &length); getBlob(p, &length); }
    }
    return p->valid;
}
static int exact(WsFtp *c, void *data, int length, int writing) {
    unsigned char *bytes = data;
    while (length > 0) {
        int count = writing ? c->write(c->context, bytes, length) : c->read(c->context, bytes, length);
        if (count <= 0 || count > length) { c->broken = 1; return fail(c, "SFTP connection ended, timed out, or was cancelled. Reconnect to continue."); }
        bytes += count; length -= count;
    }
    return 1;
}
static uint32_t request(WsFtp *c, unsigned char type) {
    c->out.length = c->out.offset = 0; c->out.valid = 1; c->status = 0; c->error[0] = 0;
    put32(&c->out, 0); put(&c->out, &type, 1);
    uint32_t id = ++c->sequence; put32(&c->out, id); return id;
}
static int send(WsFtp *c) {
    if (c->broken || !c->out.valid) return fail(c, "SFTP request is too large or the connection is unavailable.");
    uint32_t length = (uint32_t)c->out.length - 4;
    for (int i = 0; i < 4; ++i) c->out.data[i] = (unsigned char)(length >> (24 - 8 * i));
    return exact(c, c->out.data, (int)c->out.length, 1);
}
static int receive(WsFtp *c, uint32_t *id) {
    unsigned char header[4];
    if (!exact(c, header, 4, 0)) return 0;
    uint32_t length = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];
    c->in.valid = 1; c->in.offset = 0;
    if (length < 5 || !reserve(&c->in, length)) { c->broken = 1; return fail(c, "Invalid or oversized SFTP reply."); }
    c->in.length = length;
    if (!exact(c, c->in.data, length, 0)) return 0;
    int type = c->in.data[c->in.offset++]; *id = get32(&c->in);
    return type;
}
static int status(WsFtp *c) {
    c->status = (int)get32(&c->in);
    char message[4096];
    if (!getString(&c->in, message, sizeof(message))) { c->broken = 1; return fail(c, "Malformed SFTP status."); }
    uint32_t ignored; getBlob(&c->in, &ignored);
    if (!c->in.valid || c->in.offset != c->in.length) { c->broken = 1; return fail(c, "Malformed SFTP status."); }
    for (char *p = message; *p; ++p) if ((unsigned char)*p < 32 || *p == 127) *p = ' ';
    if (c->status) {
        snprintf(c->error, sizeof(c->error), "SFTP error %d: %.450s", c->status, message);
        return 0;
    }
    return 1;
}
static int reply(WsFtp *c, uint32_t wanted, int type) {
    uint32_t id; int received = receive(c, &id);
    if (!received) return 0;
    if (id != wanted) { c->broken = 1; return fail(c, "Unexpected SFTP response identifier."); }
    if (received == 101) { int ok = status(c); return type == 101 ? ok : (ok ? fail(c, "Unexpected SFTP status reply.") : 0); }
    if (received != type) { c->broken = 1; return fail(c, "Unexpected SFTP response type."); }
    return 1;
}
static int exchange(WsFtp *c, uint32_t id, int type) { return send(c) && reply(c, id, type); }
static int valid(WsFtp *c) {
    if (!c->in.valid || c->in.offset != c->in.length) { c->broken = 1; return fail(c, "Malformed SFTP reply."); }
    return 1;
}
WsFtp *wsftp_create(void *context, WsFtpRead read, WsFtpWrite write) {
    WsFtp *c = calloc(1, sizeof(*c));
    if (c) { c->context = context; c->read = read; c->write = write; } return c;
}
void wsftp_free(WsFtp *c) { if (c) { free(c->in.data); free(c->out.data); free(c); } }
const char *wsftp_error(WsFtp *c) { return c ? c->error : "Cannot allocate SFTP connection."; }
int wsftp_status(WsFtp *c) { return c->status; }
int wsftp_atomic_replace(WsFtp *c) { return c->replace; }
int wsftp_init(WsFtp *c) {
    request(c, 1); c->out.length = 5; put32(&c->out, 3);
    uint32_t version;
    if (!send(c) || receive(c, &version) != 2 || version != 3) return fail(c, "The server must support SFTP version 3.");
    while (c->in.valid && c->in.offset < c->in.length) {
        char name[1024], value[4096];
        if (!getString(&c->in, name, sizeof(name)) || !getString(&c->in, value, sizeof(value))) break;
        if (!strcmp(name, "posix-rename@openssh.com") && !strcmp(value, "1")) c->replace = 1;
    }
    return valid(c);
}
int wsftp_realpath(WsFtp *c, const char *path, char *output, size_t capacity) {
    uint32_t id = request(c, 16); string(&c->out, path);
    if (!exchange(c, id, 104)) return 0;
    uint32_t count = get32(&c->in), ignored; WsFtpAttrs a;
    if (count != 1 || !getString(&c->in, output, capacity)) return fail(c, "Invalid SFTP canonical path.");
    getBlob(&c->in, &ignored); attrs(&c->in, &a); return valid(c);
}
int wsftp_stat(WsFtp *c, const char *path, WsFtpAttrs *a) {
    uint32_t id = request(c, 7); string(&c->out, path);
    if (!exchange(c, id, 105)) return 0;
    attrs(&c->in, a); return valid(c);
}
static int pathCommand(WsFtp *c, int type, const char *path) {
    uint32_t id = request(c, type); string(&c->out, path);
    if (type == 14) { put32(&c->out, 4); put32(&c->out, 0755); }
    return exchange(c, id, 101);
}
int wsftp_mkdir(WsFtp *c, const char *path) { return pathCommand(c, 14, path); }
int wsftp_remove(WsFtp *c, const char *path, int directory) { return pathCommand(c, directory ? 15 : 13, path); }
int wsftp_rename(WsFtp *c, const char *from, const char *to, int replace) {
    if (replace && !c->replace) return fail(c, "This server cannot replace files atomically. Rename or remove the existing file first.");
    uint32_t id = request(c, replace ? 200 : 18);
    if (replace) string(&c->out, "posix-rename@openssh.com");
    string(&c->out, from); string(&c->out, to); return exchange(c, id, 101);
}
typedef struct Handle { unsigned char data[4096]; uint32_t length; } Handle;
static int openHandle(WsFtp *c, int type, const char *path, int writing, Handle *h) {
    uint32_t id = request(c, type); string(&c->out, path);
    if (type == 3) { put32(&c->out, writing ? 0x2a : 1); put32(&c->out, writing ? 4 : 0); if (writing) put32(&c->out, 0644); }
    if (!exchange(c, id, 102)) return 0;
    const unsigned char *data = getBlob(&c->in, &h->length);
    if (!data || !h->length || h->length > sizeof(h->data)) return fail(c, "Invalid SFTP file handle.");
    memcpy(h->data, data, h->length); return valid(c);
}
static int closeHandle(WsFtp *c, Handle *h) { uint32_t id = request(c, 4); blob(&c->out, h->data, h->length); return exchange(c, id, 101); }
int wsftp_list(WsFtp *c, const char *path, WsFtpEntry entry, void *context) {
    Handle h; if (!openHandle(c, 11, path, 0, &h)) return 0;
    unsigned total = 0, pages = 0;
    for (;;) {
        uint32_t id = request(c, 12); blob(&c->out, h.data, h.length);
        if (!exchange(c, id, 104)) { if (c->status == 1) break; return 0; }
        uint32_t count = get32(&c->in);
        if (count > 100000 - total || ++pages > 100000) return fail(c, "Directory exceeds 100,000 entries.");
        total += count;
        for (uint32_t i = 0; i < count; ++i) {
            char name[4096]; uint32_t ignored; WsFtpAttrs a;
            if (!getString(&c->in, name, sizeof(name))) return fail(c, "Invalid SFTP filename.");
            getBlob(&c->in, &ignored);
            if (!attrs(&c->in, &a)) return fail(c, "Invalid SFTP file attributes.");
            if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
            if (!*name || strchr(name, '/') || !entry(context, name, &a)) return fail(c, "Invalid, unsupported, or excessive directory entries.");
        }
        if (!valid(c)) return 0;
    }
    return closeHandle(c, &h);
}
static int progressCall(WsFtp *c, WsFtpProgress progress, void *context, uint64_t done, uint64_t total) {
    return !progress || progress(context, done, total) ? 1 : fail(c, "Transfer cancelled.");
}
int wsftp_upload(WsFtp *c, const char *path, uint64_t size, WsFtpRead input, WsFtpProgress progress, void *context) {
    Handle h; if (!openHandle(c, 3, path, 1, &h)) return 0;
    uint64_t done = 0;
    while (done < size) {
        uint32_t ids[PIPELINE]; int acknowledged[PIPELINE] = {0}, count = 0;
        for (; count < PIPELINE && done < size; ++count) {
            unsigned char data[CHUNK]; int length = size - done > CHUNK ? CHUNK : (int)(size - done), used = 0;
            while (used < length) {
                int n = input(context, data + used, length - used);
                if (n <= 0 || n > length - used) return fail(c, "Cannot read the local file, or it changed during transfer.");
                used += n;
            }
            ids[count] = request(c, 6); blob(&c->out, h.data, h.length); put64(&c->out, done); blob(&c->out, data, length);
            if (!send(c)) return 0;
            done += length;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t id; int type = receive(c, &id), slot = -1;
            if (!type) return 0;
            for (int j = 0; j < count; ++j) if (ids[j] == id && !acknowledged[j]) slot = j;
            if (type != 101 || slot < 0) { c->broken = 1; return fail(c, "Unexpected upload acknowledgement."); }
            acknowledged[slot] = 1;
            if (!status(c)) return 0;
        }
        if (!progressCall(c, progress, context, done, size)) return 0;
    }
    return progressCall(c, progress, context, done, size) && closeHandle(c, &h);
}
/* A bounded sliding window keeps reads in flight across RTTs. Completed
 * blocks stay in file order; short replies refill only their missing tail. */
int wsftp_download(WsFtp *c, const char *path, uint64_t size, WsFtpWrite output, WsFtpProgress progress, void *context) {
    enum { WINDOW = 64 };
    typedef struct ReadSlot { uint32_t id, expected, used; uint64_t offset; } ReadSlot;
    ReadSlot slots[WINDOW] = {{0}};
    Handle h; if (!openHandle(c, 3, path, 0, &h)) return 0;
    unsigned char *blocks = malloc(WINDOW * CHUNK);
    if (!blocks) return fail(c, "Cannot allocate transfer buffers.");
    /* Bound request bytes too: the synchronous transport must be able to
     * enqueue the window without deadlocking against buffered replies. */
    unsigned window = 32768 / (25 + h.length);
    if (window > WINDOW) window = WINDOW;
    uint64_t done = 0, next = 0;
    unsigned head = 0, count = 0;
    int ok = progressCall(c, progress, context, 0, size);
    while (ok && done < size) {
        while (count < window && next < size) {
            unsigned index = (head + count) % WINDOW;
            ReadSlot *slot = &slots[index];
            slot->offset = next; slot->used = 0;
            slot->expected = size - next > CHUNK ? CHUNK : (uint32_t)(size - next);
            slot->id = request(c, 5); blob(&c->out, h.data, h.length);
            put64(&c->out, next); put32(&c->out, slot->expected);
            if (!(ok = send(c))) break;
            next += slot->expected; ++count;
        }
        if (!ok) break;
        uint32_t id; int type = receive(c, &id), found = -1;
        if (!type) { ok = 0; break; }
        for (unsigned i = 0; i < count; ++i) {
            unsigned index = (head + i) % WINDOW;
            if (slots[index].id == id && slots[index].used < slots[index].expected) found = (int)index;
        }
        if (found < 0) { ok = fail(c, "Unexpected download response identifier."); break; }
        ReadSlot *slot = &slots[found];
        if (type == 101) { status(c); ok = fail(c, "Remote file could not be read completely; it may have changed."); break; }
        uint32_t length = 0;
        const unsigned char *data = type == 103 ? getBlob(&c->in, &length) : NULL;
        if (!data || !length || length > slot->expected - slot->used || !valid(c)) {
            ok = fail(c, "Invalid SFTP data reply."); break;
        }
        memcpy(blocks + found * CHUNK + slot->used, data, length); slot->used += length;
        if (slot->used < slot->expected) {
            slot->id = request(c, 5); blob(&c->out, h.data, h.length);
            put64(&c->out, slot->offset + slot->used); put32(&c->out, slot->expected - slot->used);
            if (!(ok = send(c))) break;
        }
        while (ok && count && slots[head].used == slots[head].expected) {
            unsigned written = 0;
            while (written < slots[head].used) {
                int n = output(context, blocks + head * CHUNK + written, slots[head].used - written);
                if (n <= 0 || (unsigned)n > slots[head].used - written) {
                    ok = fail(c, "Cannot write the local file. Check free space and permissions."); break;
                }
                written += n;
            }
            if (!ok) break;
            done += written; head = (head + 1) % WINDOW; --count;
        }
        if (ok) ok = progressCall(c, progress, context, done, size);
    }
    free(blocks);
    /* Failures may leave outstanding replies. Do not reuse this connection. */
    if (!ok) c->broken = 1;
    return ok && progressCall(c, progress, context, done, size) && closeHandle(c, &h);
}
int wsftp_local_name(const char *name) {
    size_t length = strlen(name);
    if (!length || length > 255 || !strcmp(name, ".") || !strcmp(name, "..") || name[length-1] == '.' || name[length-1] == ' ') return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32 || *p == 127 || strchr("/\\:*?\"<>|", *p)) return 0;
    char stem[16] = {0}; size_t n = strcspn(name, ".");
    if (n >= sizeof(stem)) return 1;
    for (size_t i = 0; i < n; ++i) stem[i] = (char)toupper((unsigned char)name[i]);
    if (!strcmp(stem, "CON") || !strcmp(stem, "PRN") || !strcmp(stem, "AUX") || !strcmp(stem, "NUL") || !strcmp(stem, "CONIN$") || !strcmp(stem, "CONOUT$")) return 0;
    if ((!strncmp(stem, "COM", 3) || !strncmp(stem, "LPT", 3)) &&
        ((n == 4 && stem[3] >= '0' && stem[3] <= '9') ||
         !strcmp(stem+3, "\xc2\xb9") || !strcmp(stem+3, "\xc2\xb2") || !strcmp(stem+3, "\xc2\xb3"))) return 0;
    return 1;
}
