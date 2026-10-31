#pragma once

/* Shared by the workspace and hosted terminal. Return -1 for terminal input,
 * zero for F10 menu focus, or the native top-level menu mnemonic. AltGr and
 * modified function keys remain available to the terminal. */
static inline int wsMenuAccessKey(unsigned key, int control, int shift, int alt) {
    if (control || shift) return -1;
    if (key == 0x79 && !alt) return 0; /* VK_F10 */
    if (!alt) return -1;
    /* This is a virtual-key code, not a character: 0x73 is VK_F4, not 's'. */
    switch (key) {
    case 'F': case 'H': case 'S': case 'V': case 'T': case 'E': return (int)key;
    default: return -1;
    }
}

/* WM_SYSCHAR carries text instead of a virtual-key code. */
static inline int wsMenuAccessChar(unsigned character, int control, int shift) {
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    return wsMenuAccessKey(character, control, shift, 1);
}
