#include <stdio.h>
#include <string.h>
#include "core/log.h"
#include "app/controlPipe.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static bool g_connected;

bool ControlPipe_Open(const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "\\\\.\\pipe\\%s", name);
    g_pipe = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
        PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, NULL);
    if (g_pipe == INVALID_HANDLE_VALUE) {
        Log_Error("[ControlPipe_Open]: Can't create the pipe %s (error %lu)\n", path, GetLastError());
        return false;
    }
    Log_Info("[Headless] Waiting for commands on %s\n", path);
    return true;
}

static void Disconnect(void) {
    if (g_connected) {
        FlushFileBuffers(g_pipe);
        DisconnectNamedPipe(g_pipe);
        g_connected = false;
    }
}

bool ControlPipe_Receive(char* line, size_t size) {
    if (g_pipe == INVALID_HANDLE_VALUE || size == 0)
        return false;
    for (;;) {
        Disconnect();
        // A client that connected before this call is connected too
        if (!ConnectNamedPipe(g_pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
            Log_Error("[ControlPipe_Receive]: Connection failed (error %lu)\n", GetLastError());
            return false;
        }
        g_connected = true;
        size_t length = 0;
        bool ended = false;
        while (!ended && length < size - 1) {
            char c;
            DWORD read = 0;
            if (!ReadFile(g_pipe, &c, 1, &read, NULL) || read == 0)
                break;
            if (c == '\n')
                ended = true;
            else if (c != '\r')
                line[length++] = c;
        }
        line[length] = '\0';
        // A client that closed without sending a command is dropped
        if (ended || length > 0)
            return true;
    }
}

void ControlPipe_Reply(const char* text) {
    if (!g_connected)
        return;
    DWORD written;
    WriteFile(g_pipe, text, (DWORD)strlen(text), &written, NULL);
    WriteFile(g_pipe, "\n", 1, &written, NULL);
    Disconnect();
}

void ControlPipe_Close(void) {
    Disconnect();
    if (g_pipe != INVALID_HANDLE_VALUE)
        CloseHandle(g_pipe);
    g_pipe = INVALID_HANDLE_VALUE;
}

#else

bool ControlPipe_Open(const char* name) {
    Log_Error("[ControlPipe_Open]: The control pipe %s is only available on Windows\n", name);
    return false;
}

bool ControlPipe_Receive(char* line, size_t size) {
    (void)line;
    (void)size;
    return false;
}

void ControlPipe_Reply(const char* text) {
    (void)text;
}

void ControlPipe_Close(void) {
}

#endif
