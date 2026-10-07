#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define WSHELL_INPUT_MESSAGE 0x57424931u
#define WSHELL_INPUT_LIMIT 65536u
/* WM_COPYDATA payload, followed by bytes or UTF-16. No pointers cross processes. */
typedef struct WsInputHeader { int32_t kind, codepage, length; } WsInputHeader;
/* kind: 1 = Unicode key input, 2 = encoded keys, 3 = Unicode paste,
 * 4 = navigation/editing key, translated using the receiving terminal's mode. */
typedef struct WsInputKey { uint32_t key, flags; unsigned char keyboard[256]; } WsInputKey;
static inline int wsInputValid(const void *data, size_t size) {
    if (!data || size <= sizeof(WsInputHeader) || size > sizeof(WsInputHeader) + WSHELL_INPUT_LIMIT) return 0;
    WsInputHeader header; memcpy(&header, data, sizeof(header));
    size_t bytes = size - sizeof(header);
    if (header.kind == 1 || header.kind == 3)
        return header.length > 0 && (size_t)header.length <= WSHELL_INPUT_LIMIT / 2 && (size_t)header.length * 2 == bytes;
    if (header.kind == 2)
        return header.length >= 0 ? (size_t)header.length == bytes :
            (header.length == -1 || header.length == -2) && ((const unsigned char *)data)[size - 1] == 0;
    if (header.kind == 4) return header.length == sizeof(WsInputKey) && bytes == sizeof(WsInputKey);
    return 0;
}
