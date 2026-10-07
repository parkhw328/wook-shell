#pragma once
#include <stdint.h>
#define WSHELL_INPUT_MESSAGE 0x57424931u
#define WSHELL_INPUT_LIMIT 65536u
/* WM_COPYDATA payload, followed by bytes or UTF-16. No pointers cross processes. */
typedef struct WsInputHeader { int32_t kind, codepage, length; } WsInputHeader;
/* kind: 1 = Unicode key input, 2 = encoded keys, 3 = Unicode paste. */
