#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include <rlgl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "core/frameTime.h"
#include "core/log.h"
#include "app/controlPipe.h"
#include "app/dialog.h"
#include "app/fileOpen.h"
#include "app/headless.h"
#include "views/views.h"

// The commands come from a script, then from the control pipe if there is one: each command of the pipe is answered
// when it ends, after the frames it draws, with the errors and the output it gave, or "ok".
//
// The input is given to the callbacks that raylib registered in GLFW, as if it came from the system: raylib and raygui
// see the same key, character, button, cursor and wheel events, and the hidden window gets none of the real ones

#define STEP_MAX    1024
#define LINE_SIZE   1024
#define REPLY_SIZE  4096

typedef enum {
    STEP_MOVE,      // a, b: position
    STEP_BUTTON,    // a: button, b: pressed
    STEP_SCROLL,    // a: offset
    STEP_KEY,       // a: key, b: pressed, c: modifiers
    STEP_CHAR,      // a: codepoint
    STEP_SHOT,      // path: image
} StepType;

typedef struct {
    StepType    type;
    int         a, b, c;
    const char* path;
    int         frames; // Frames to draw after the step, before the next one
} Step;

static struct {
    char*   text;       // The script, cut into lines
    char*   next;       // Start of the next line to run, NULL at the end
    int     line;       // Number of the next line
    int     wait;       // Frames to draw before the next step or command
    int     errors;
    Step    steps[STEP_MAX];    // Of the running command
    int     stepCount;
    int     stepIndex;
    const char* shot;   // Image to save at the end of this frame
    bool    active;
    bool    pipe;       // The commands come from the control pipe after the script
    bool    replying;   // A command of the pipe is running, and its client waits for the reply
    char    pipeLine[LINE_SIZE];
    char    reply[REPLY_SIZE];
} g_script;

static struct {
    GLFWwindow*             window;
    GLFWkeyfun              key;
    GLFWcharfun             character;
    GLFWmousebuttonfun      button;
    GLFWcursorposfun        cursor;
    GLFWscrollfun           scroll;
} g_input;

// Names of the keys, besides the letters and the digits
static const struct {
    const char* name;
    int         key;
} g_keys[] = {
    { "enter", KEY_ENTER }, { "escape", KEY_ESCAPE }, { "tab", KEY_TAB }, { "backspace", KEY_BACKSPACE },
    { "delete", KEY_DELETE }, { "insert", KEY_INSERT }, { "space", KEY_SPACE }, { "home", KEY_HOME }, { "end", KEY_END },
    { "pageup", KEY_PAGE_UP }, { "pagedown", KEY_PAGE_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT },
    { "up", KEY_UP }, { "down", KEY_DOWN }, { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f3", KEY_F3 }, { "f4", KEY_F4 },
    { "f5", KEY_F5 }, { "f6", KEY_F6 }, { "f7", KEY_F7 }, { "f8", KEY_F8 }, { "f9", KEY_F9 }, { "f10", KEY_F10 },
    { "f11", KEY_F11 }, { "f12", KEY_F12 }, { "shift", KEY_LEFT_SHIFT }, { "ctrl", KEY_LEFT_CONTROL },
    { "alt", KEY_LEFT_ALT },
};

// Takes the callbacks of the window, putting them back: glfwSet...Callback returns the previous one
static void TakeInput(void) {
    GLFWwindow* window = glfwGetCurrentContext();
    g_input.window = window;
    g_input.key = glfwSetKeyCallback(window, NULL);
    glfwSetKeyCallback(window, g_input.key);
    g_input.character = glfwSetCharCallback(window, NULL);
    glfwSetCharCallback(window, g_input.character);
    g_input.button = glfwSetMouseButtonCallback(window, NULL);
    glfwSetMouseButtonCallback(window, g_input.button);
    g_input.cursor = glfwSetCursorPosCallback(window, NULL);
    glfwSetCursorPosCallback(window, g_input.cursor);
    g_input.scroll = glfwSetScrollCallback(window, NULL);
    glfwSetScrollCallback(window, g_input.scroll);
}

bool Headless_Start(const char* scriptPath, const char* pipeName) {
    if (scriptPath != NULL) {
        g_script.text = LoadFileText(scriptPath);
        if (g_script.text == NULL) {
            Log_Error("[Headless_Start]: Can't read the script %s\n", scriptPath);
            return false;
        }
        g_script.next = g_script.text;
        g_script.line = 1;
    }
    if (pipeName != NULL) {
        if (!ControlPipe_Open(pipeName))
            return false;
        g_script.pipe = true;
    }
    g_script.active = true;
    TakeInput();
    // The mouse starts out of the window, so it isn't over a control
    if (g_input.cursor != NULL)
        g_input.cursor(g_input.window, -1.0, -1.0);
    FrameTime_SetFixed(1.0f / 60.0f);
    Dialog_SetHeadless();
    // The views open the audio device when they need it: it is opened now, muted, so nothing is heard
    InitAudioDevice();
    if (IsAudioDeviceReady())
        SetMasterVolume(0.0f);
    return true;
}

// The next line without the line end and the spaces around it, or NULL at the end of the script
static char* NextLine(void) {
    if (g_script.next == NULL || *g_script.next == '\0')
        return NULL;
    char* line = g_script.next;
    char* end = strchr(line, '\n');
    if (end != NULL) {
        *end = '\0';
        g_script.next = end + 1;
    }
    else
        g_script.next = NULL;
    while (isspace((unsigned char)*line))
        line++;
    size_t length = strlen(line);
    while (length > 0 && isspace((unsigned char)line[length - 1]))
        line[--length] = '\0';
    return line;
}

// Adds a line to the reply of the running command of the pipe
static void AddReply(const char* text) {
    if (!g_script.replying)
        return;
    size_t length = strlen(g_script.reply);
    snprintf(g_script.reply + length, sizeof(g_script.reply) - length, "%s%s", length > 0 ? "\n" : "", text);
}

// line is 0 for the commands of the pipe
static void Fail(int line, const char* message) {
    if (line > 0)
        Log_Error("[Headless] Line %d: %s\n", line, message);
    else
        Log_Error("[Headless] %s\n", message);
    AddReply(TextFormat("error: %s", message));
    g_script.errors++;
}

static void AddStep(StepType type, int a, int b, int c, int frames) {
    if (g_script.stepCount < STEP_MAX)
        g_script.steps[g_script.stepCount++] = (Step) { type, a, b, c, NULL, frames };
}

// The button named by text: left if there is none
static bool ParseButton(const char* text, int* button) {
    if (text == NULL || *text == '\0' || strcmp(text, "left") == 0)
        *button = GLFW_MOUSE_BUTTON_LEFT;
    else if (strcmp(text, "right") == 0)
        *button = GLFW_MOUSE_BUTTON_RIGHT;
    else if (strcmp(text, "middle") == 0)
        *button = GLFW_MOUSE_BUTTON_MIDDLE;
    else
        return false;
    return true;
}

static int KeyByName(const char* text) {
    char name[16];
    size_t length = 0;
    for (; text[length] != '\0' && length < sizeof(name) - 1; length++)
        name[length] = (char)tolower((unsigned char)text[length]);
    name[length] = '\0';
    if (length == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z')
            return KEY_A + (c - 'a');
        if (c >= '0' && c <= '9')
            return KEY_ZERO + (c - '0');
    }
    for (size_t i = 0; i < sizeof(g_keys) / sizeof(g_keys[0]); i++)
        if (strcmp(g_keys[i].name, name) == 0)
            return g_keys[i].key;
    return KEY_NULL;
}

// Adds the steps to press the keys joined by + (the modifiers, then the key), hold them during frames and release them
static bool AddKeySteps(char* keys, int frames) {
    int pressed[8], count = 0, mods = 0;
    for (char* name = strtok(keys, "+"); name != NULL; name = strtok(NULL, "+")) {
        int key = KeyByName(name);
        if (key == KEY_NULL || count == 8)
            return false;
        pressed[count++] = key;
        mods |= key == KEY_LEFT_SHIFT ? GLFW_MOD_SHIFT : key == KEY_LEFT_CONTROL ? GLFW_MOD_CONTROL :
            key == KEY_LEFT_ALT ? GLFW_MOD_ALT : 0;
    }
    if (count == 0)
        return false;
    for (int i = 0; i < count; i++)
        AddStep(STEP_KEY, pressed[i], GLFW_PRESS, mods, i == count - 1 ? frames : 0);
    for (int i = count - 1; i >= 0; i--)
        AddStep(STEP_KEY, pressed[i], GLFW_RELEASE, mods, i == 0 ? 1 : 0);
    return true;
}

static void PrintStatus(void) {
    ViewStatus status;
    const char* text = "no view open";
    if (Views_Status(&status))
        text = TextFormat("file=%s | info=%s | hint=%s", status.file, status.info, status.hint);
    printf("[Headless] Status: %s\n", text);
    fflush(stdout);
    AddReply(text);
}

// Runs a command, or turns it into steps. line is its line in the script, or 0 if it comes from the pipe. Returns false
// to end the application
static bool RunCommand(int line, char* command, char* argument) {
    char* words[6] = { 0 };
    int count = 0;
    char copy[1024];
    snprintf(copy, sizeof(copy), "%s", argument);
    for (char* word = strtok(copy, " \t"); word != NULL && count < 6; word = strtok(NULL, " \t"))
        words[count++] = word;
    int button;

    if (strcmp(command, "open") == 0)
        FileOpen_Open(argument);
    else if (strcmp(command, "frames") == 0)
        g_script.wait = atoi(argument) > 0 ? atoi(argument) : 0;
    else if (strcmp(command, "shot") == 0) {
        AddStep(STEP_SHOT, 0, 0, 0, 1);
        g_script.steps[g_script.stepCount - 1].path = argument;
    }
    else if (strcmp(command, "status") == 0)
        PrintStatus();
    else if (strcmp(command, "move") == 0 && count == 2)
        AddStep(STEP_MOVE, atoi(words[0]), atoi(words[1]), 0, 1);
    else if (strcmp(command, "click") == 0 && count >= 2 && ParseButton(words[2], &button)) {
        AddStep(STEP_MOVE, atoi(words[0]), atoi(words[1]), 0, 0);
        AddStep(STEP_BUTTON, button, GLFW_PRESS, 0, 1);
        AddStep(STEP_BUTTON, button, GLFW_RELEASE, 0, 1);
    }
    else if (strcmp(command, "drag") == 0 && count >= 4 && ParseButton(words[4], &button)) {
        int x1 = atoi(words[0]), y1 = atoi(words[1]), x2 = atoi(words[2]), y2 = atoi(words[3]);
        AddStep(STEP_MOVE, x1, y1, 0, 0);
        AddStep(STEP_BUTTON, button, GLFW_PRESS, 0, 1);
        for (int i = 1; i <= 8; i++)
            AddStep(STEP_MOVE, x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8, 0, 1);
        AddStep(STEP_BUTTON, button, GLFW_RELEASE, 0, 1);
    }
    else if (strcmp(command, "wheel") == 0 && count == 3) {
        AddStep(STEP_MOVE, atoi(words[0]), atoi(words[1]), 0, 0);
        AddStep(STEP_SCROLL, -atoi(words[2]), 0, 0, 1);
    }
    else if (strcmp(command, "key") == 0 && count == 1) {
        if (!AddKeySteps(words[0], 1))
            Fail(line, TextFormat("Unknown key %s", words[0]));
    }
    else if (strcmp(command, "hold") == 0 && count == 2) {
        if (!AddKeySteps(words[0], atoi(words[1]) > 0 ? atoi(words[1]) : 1))
            Fail(line, TextFormat("Unknown key %s", words[0]));
    }
    else if (strcmp(command, "type") == 0) {
        for (const char* c = argument; *c != '\0';) {
            int size = 0;
            int codepoint = GetCodepointNext(c, &size);
            AddStep(STEP_CHAR, codepoint, 0, 0, 1);
            c += size > 0 ? size : 1;
        }
    }
    else if (strcmp(command, "dialog") == 0 && *argument != '\0')
        Dialog_Answer(argument);
    else if (strcmp(command, "quit") == 0)
        return false;
    else
        Fail(line, TextFormat("Unknown command or wrong arguments: %s %s", command, argument));
    return true;
}

static void RunStep(const Step* step) {
    switch (step->type) {
    case STEP_MOVE:
        if (g_input.cursor != NULL)
            g_input.cursor(g_input.window, step->a, step->b);
        break;
    case STEP_BUTTON:
        if (g_input.button != NULL)
            g_input.button(g_input.window, step->a, step->b, 0);
        break;
    case STEP_SCROLL:
        if (g_input.scroll != NULL)
            g_input.scroll(g_input.window, 0.0, step->a);
        break;
    case STEP_KEY:
        if (g_input.key != NULL)
            g_input.key(g_input.window, step->a, 0, step->b, step->c);
        break;
    case STEP_CHAR:
        if (g_input.character != NULL)
            g_input.character(g_input.window, (unsigned int)step->a);
        break;
    case STEP_SHOT:
        g_script.shot = step->path;
        break;
    }
}

// Saves the back buffer, which holds the frame drawn and not yet swapped, after drawing what rlgl still has in its batch
void Headless_Capture(void) {
    if (g_script.shot == NULL)
        return;
    rlDrawRenderBatchActive();
    Image image = LoadImageFromScreen();
    // Opaque, as on the screen: the alpha of the pixels drawn with blending isn't 255
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    if (ExportImage(image, g_script.shot))
        Log_Info("[Headless] Saved %s (%dx%d)\n", g_script.shot, image.width, image.height);
    else
        Fail(0, TextFormat("Can't save %s", g_script.shot));
    UnloadImage(image);
    g_script.shot = NULL;
}

bool Headless_Update(void) {
    while (g_script.wait == 0) {
        if (g_script.stepIndex < g_script.stepCount) {
            const Step* step = &g_script.steps[g_script.stepIndex++];
            RunStep(step);
            g_script.wait = step->frames;
            continue;
        }
        g_script.stepCount = g_script.stepIndex = 0;
        if (g_script.replying) {
            ControlPipe_Reply(g_script.reply[0] != '\0' ? g_script.reply : "ok");
            g_script.replying = false;
        }

        // The next line of the script, or else the next command of the pipe, waited for without drawing frames
        int line = 0;
        char* command = NextLine();
        if (command != NULL)
            line = g_script.line++;
        else if (g_script.pipe && ControlPipe_Receive(g_script.pipeLine, sizeof(g_script.pipeLine))) {
            command = g_script.pipeLine;
            while (isspace((unsigned char)*command))
                command++;
            g_script.replying = true;
            g_script.reply[0] = '\0';
            Log_Info("[Headless] Command: %s\n", command);
        }
        else
            return false;
        if (*command == '\0' || *command == '#')
            continue;
        char* argument = command;
        while (*argument != '\0' && !isspace((unsigned char)*argument))
            argument++;
        if (*argument != '\0') {
            *argument++ = '\0';
            while (isspace((unsigned char)*argument))
                argument++;
        }
        if (!RunCommand(line, command, argument)) {
            if (g_script.replying)
                ControlPipe_Reply("ok");
            return false;
        }
    }
    g_script.wait--;
    return true;
}

bool Headless_IsActive(void) {
    return g_script.active;
}

void Headless_End(void) {
    ControlPipe_Close();
}

int Headless_Errors(void) {
    if (Dialog_PendingAnswers() > 0)
        Log_Warning("[Headless] %d dialog answers not taken\n", Dialog_PendingAnswers());
    return g_script.errors;
}
