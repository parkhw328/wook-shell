#pragma once

/* Shared by the workspace and hosted terminal. Return -1 for terminal input,
 * zero for F10 menu focus, or the native top-level menu mnemonic. AltGr and
 * modified function keys remain available to the terminal. */
static inline int wsMenuAccessKey(unsigned key, int control, int shift, int alt) {
    if (control || shift) return -1;
    if (key == 0x79 && !alt) return 0; /* VK_F10 */
    if (!alt) return -1;
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    switch (key) {
    case 'F': case 'H': case 'S': case 'V': case 'T': case 'E': return (int)key;
    default: return -1;
    }
}
