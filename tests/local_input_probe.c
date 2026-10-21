/* Isolated ConPTY fixture: retain unread raw input until the test releases it. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD savedMode;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &savedMode))
        return 1;
    if (!SetConsoleMode(input, 0)) {
        CloseHandle(input);
        return 2;
    }
    puts("RAW-READY");
    fflush(stdout);
    ULONGLONG deadline = GetTickCount64() + 15000;
    while (GetFileAttributesW(L"raw-read-now") == INVALID_FILE_ATTRIBUTES &&
           GetTickCount64() < deadline)
        Sleep(10);
    unsigned char buffer[128];
    DWORD count = 0;
    int result = 3;
    if (GetFileAttributesW(L"raw-read-now") != INVALID_FILE_ATTRIBUTES) {
        while (count < sizeof(buffer) && !memchr(buffer, 3, count)) {
            DWORD received = 0;
            if (!ReadFile(input, buffer + count, sizeof(buffer) - count, &received, NULL) || !received)
                break;
            count += received;
        }
        FILE *file = fopen("raw-input.bin", "wb");
        if (file) {
            result = fwrite(buffer, 1, count, file) == count ? 0 : 4;
            fclose(file);
        }
    }
    SetConsoleMode(input, savedMode);
    CloseHandle(input);
    return result;
}
