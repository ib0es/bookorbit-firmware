// Headless driver for the simulator build: runs setup()/loop() with a scripted
// sequence of button presses and dumps every display update as a PGM frame.
//
//   x4_headless <frames dir> <sd root> "<script>"
// Script: U D L R C B P = buttons, w = wait 2.2 s (runs loop()),
//         S = server edit (renames <sd>/server_next.json to server.json),
//         Z = sleep and wake up (setup() runs again, RAM state is rebuilt from the SD card),
//         H<key> = hold an arrow key (U D L R) for 3 s (its auto-repeat),
//         X = card edit: renames <sd>/config_next.json to config.json ("Reload from SD"),
//         T<x>,<y>; tap, W<x0>,<y0>,<x1>,<y1>; swipe (logical portrait coordinates),
//         [ ... ] = steps for a screen that runs its own input loop (keyboard, Wi-Fi
//         list, web portal): queued first, then fed to that loop as it polls.
//         Put the [...] before the step that opens such a screen.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include "Arduino.h"
#include "InputManager.h"
#include "EInkDisplay.h"
#include "Imu.h"
#include "SDCardManager.h"
#include "UsbMassStorage.h"
#include "platform/hal/display.h"
#include "esp_sleep.h"

void setup();
void loop();

bool InputManager::s_simState[InputManager::BTN_COUNT] = { false };
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = { false };
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
namespace FakeNet { void pump(); }
// ---- steps for nested input loops ([...] in the script) ----
// A screen with its own loop (keyboard...) polls input many times within one
// loop() of the runner (the main loop polls once); that is how it is told apart from the main loop.
struct NestedStep { char kind; int a, b, c, d; };
static std::deque<NestedStep> g_nested;
static int g_pumpsThisLoop = 0;
static int g_nestedTick = 0;
static int g_pressed = -1;
static void applyNested(const NestedStep& s);
void simPumpEvents() { FakeNet::pump(); }

void simInputPolled() {
    if (++g_pumpsThisLoop < 4) return;
    if (g_nested.empty() && g_pressed < 0) {
        // A screen waiting for input that the script does not give: stop
        // instead of hanging (the frames so far are written).
        if (g_pumpsThisLoop > 4000) { printf("!!! STUCK in a nested input loop\n"); fflush(stdout); exit(4); }
        return;
    }
    if (++g_nestedTick % 8) return;  // a few polls between steps
    if (g_pressed >= 0) { InputManager::simRelease((InputManager::Button)g_pressed); g_pressed = -1; return; }
    NestedStep s = g_nested.front();
    g_nested.pop_front();
    applyNested(s);
}

static std::string g_out;
static int g_frame = 0;
static std::string g_label = "boot";
static uint8_t g_last[EPD_BUFFER_SIZE];

static void onUpdate(const uint8_t* fb, int mode) {
    memcpy(g_last, fb, sizeof(g_last));
    char name[256];
    snprintf(name, sizeof(name), "%s/%03d_%s.pgm", g_out.c_str(), g_frame++, g_label.c_str());
    FILE* f = fopen(name, "wb");
    // Portrait frame: EPD_HEIGHT x EPD_WIDTH (480x800 on X4, 528x792 on X3).
    fprintf(f, "P5 %d %d 255\n", EPD_HEIGHT, EPD_WIDTH);
    for (int y = 0; y < EPD_WIDTH; y++)
        for (int x = 0; x < EPD_HEIGHT; x++) {
            int rx = y, ry = EPD_HEIGHT - 1 - x;
            int idx = (rx / 8) + ry * (EPD_WIDTH / 8);
            fputc((fb[idx] & (0x80 >> (rx % 8))) ? 255 : 0, f);
        }
    fclose(f);
    printf("    [frame %03d %s%s]\n", g_frame - 1, g_label.c_str(), mode == 0 ? " FULL" : "");
}

static void run(int iters) { for (int i = 0; i < iters; i++) { g_pumpsThisLoop = 0; loop(); } }

static void click(InputManager::Button b, const char* label) {
    g_label = label;
    printf(">>> %s\n", label);
    InputManager::simPress(b); run(3);
    InputManager::simRelease(b); run(3);
}

static void applyNested(const NestedStep& s) {
    switch (s.kind) {
        case 'U': g_label = "n_UP"; printf(">>> (nested) UP\n"); InputManager::simPress(InputManager::BTN_UP); g_pressed = InputManager::BTN_UP; break;
        case 'D': g_label = "n_DOWN"; printf(">>> (nested) DOWN\n"); InputManager::simPress(InputManager::BTN_DOWN); g_pressed = InputManager::BTN_DOWN; break;
        case 'k': g_label = "n_HOME_TAP"; printf(">>> (nested) HOME_TAP\n"); InputManager::simHomeKeyTap(); break;
        case 'T': {
            char lbl[32]; snprintf(lbl, sizeof(lbl), "n_TAP_%d_%d", s.a, s.b); g_label = lbl; printf(">>> (nested) %s\n", lbl);
            InputManager::simTap((float)s.b / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - s.a) / EPD_HEIGHT);
        } break;
        case 'W': {
            char lbl[48]; snprintf(lbl, sizeof(lbl), "n_SWIPE_%d_%d_%d_%d", s.a, s.b, s.c, s.d); g_label = lbl; printf(">>> (nested) %s\n", lbl);
            InputManager::simSwipe((float)s.b / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - s.a) / EPD_HEIGHT,
                                   (float)s.d / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - s.c) / EPD_HEIGHT);
        } break;
    }
}

// Parses the steps between '[' and ']' starting at script[i] == '['; returns the index of ']'.
static size_t queueNested(const std::string& script, size_t i) {
    for (++i; i < script.size() && script[i] != ']'; ++i) {
        const char c = script[i];
        int a = 0, b = 0, cc = 0, d = 0, n = 0;
        if (c == 'U' || c == 'D' || c == 'k') g_nested.push_back({c, 0, 0, 0, 0});
        else if (c == 'T' && sscanf(script.c_str() + i + 1, "%d,%d;%n", &a, &b, &n) == 2) { g_nested.push_back({c, a, b, 0, 0}); i += n; }
        else if (c == 'W' && sscanf(script.c_str() + i + 1, "%d,%d,%d,%d;%n", &a, &b, &cc, &d, &n) == 4) { g_nested.push_back({c, a, b, cc, d}); i += n; }
    }
    printf(">>> queued %u steps for a nested input loop\n", (unsigned)g_nested.size());
    return i;
}

int main(int argc, char** argv) {
    g_out = argv[1];
    SDCardManager::getInstance().setRoot(argv[2]);
    freeink::FreeInkDisplay::setUpdateCallback(onUpdate);
    const std::string script = argc > 3 ? argv[3] : "";
    setup();
    for (size_t i = 0; i < script.size(); i++) {
        char c = script[i];
#if defined(SIM_KEYS_UP_DOWN_POWER_ONLY)
        if (c == 'L' || c == 'R' || c == 'C' || c == 'B') {
            printf("!!! KEY_NOT_ON_DEVICE '%c': this board has only UP, DOWN and POWER\n", c);
            return 3;
        }
#endif
        switch (c) {
            case '[': i = queueNested(script, i); break;
            case 'U': click(InputManager::BTN_UP, "UP"); break;
            case 'D': click(InputManager::BTN_DOWN, "DOWN"); break;
            case 'L': click(InputManager::BTN_LEFT, "LEFT"); break;
            case 'R': click(InputManager::BTN_RIGHT, "RIGHT"); break;
            case 'C': click(InputManager::BTN_CONFIRM, "CONFIRM"); break;
            case 'B': click(InputManager::BTN_BACK, "BACK"); break;
            case 'P': click(InputManager::BTN_POWER, "POWER"); break;
            case 'X': {
                std::string r = std::string(argv[2]);
                std::rename((r + "/config_next.json").c_str(), (r + "/config.json").c_str());
                printf(">>> CONFIG ON THE CARD REPLACED\n");
            } break;
            case 'H': {  // H<key>: hold for 3 s
                const char k = i + 1 < script.size() ? script[++i] : 'D';
                const InputManager::Button b = k == 'U' ? InputManager::BTN_UP : k == 'L' ? InputManager::BTN_LEFT
                                             : k == 'R' ? InputManager::BTN_RIGHT : InputManager::BTN_DOWN;
                g_label = "hold";
                printf(">>> HOLD %c 3s\n", k);
                InputManager::simPress(b);
                const uint32_t t = millis();
                while (millis() - t < 3000) loop();
                InputManager::simRelease(b);
                run(3);
            } break;
            case 'S': { std::string r=std::string(argv[2]); std::rename((r+"/server_next.json").c_str(), (r+"/server.json").c_str()); printf(">>> SERVER UPDATED\n"); } break;
            case 'Z': g_label = "wake"; printf(">>> SLEEP + WAKE (setup again)\n");
                freeink::UsbMassStorage::simReset();
                g_simWakeCause = ESP_SLEEP_WAKEUP_TIMER; setup(); g_simWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED; break;
            case 'T': {  // T<x>,<y>; = tap at logical (portrait) coordinates
                int x = 0, y = 0, n = 0;
                if (sscanf(script.c_str() + i + 1, "%d,%d;%n", &x, &y, &n) == 2 && n > 0) {
                    i += n;
                    // Portrait render on the landscape panel: panel px = y, py = H-1-x.
                    InputManager::simTap((float)y / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - x) / EPD_HEIGHT);
                    char lbl[32]; snprintf(lbl, sizeof(lbl), "TAP_%d_%d", x, y);
                    g_label = lbl; printf(">>> %s\n", lbl);
                    run(3);
                }
            } break;
            case 'W': {  // W<x0>,<y0>,<x1>,<y1>; = swipe in logical (portrait) coordinates
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0, n = 0;
                if (sscanf(script.c_str() + i + 1, "%d,%d,%d,%d;%n", &x0, &y0, &x1, &y1, &n) == 4 && n > 0) {
                    i += n;
                    InputManager::simSwipe((float)y0 / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - x0) / EPD_HEIGHT,
                                           (float)y1 / EPD_WIDTH, (float)(EPD_HEIGHT - 1 - x1) / EPD_HEIGHT);
                    char lbl[48]; snprintf(lbl, sizeof(lbl), "SWIPE_%d_%d_%d_%d", x0, y0, x1, y1);
                    g_label = lbl; printf(">>> %s\n", lbl);
                    run(3);
                }
            } break;
            case 'A': {  // A<ax>,<ay>,<az>; = simulate IMU acceleration (g)
                float ax = 0, ay = -1, az = 0;
                int n = 0;
                if (sscanf(script.c_str() + i + 1, "%f,%f,%f;%n", &ax, &ay, &az, &n) == 3 && n > 0) {
                    i += n;
                    freeink::Imu::simSetAccel(ax, ay, az);
                    printf(">>> IMU_ACCEL %.2f,%.2f,%.2f\n", ax, ay, az);
                }
            } break;
            case 'w': g_label = "wait"; printf(">>> wait 2.2s\n"); { uint32_t t = millis(); while (millis() - t < 2200) loop(); } break;
            case 'k': g_label = "HOME_TAP"; printf(">>> HOME_TAP\n"); InputManager::simHomeKeyTap(); run(3); break;
            case 'K': g_label = "HOME_LONG"; printf(">>> HOME_LONG\n"); InputManager::simHomeKeyLongPress(); run(3); break;
            case 'M': {
                const char ms = (i + 1 < script.size()) ? script[++i] : 'w';
                freeink::UsbMassStorageState st = freeink::UsbMassStorageState::WaitingForHost;
                switch (ms) {
                    case 'c': st = freeink::UsbMassStorageState::Connected; break;
                    case 'a': st = freeink::UsbMassStorageState::Accessed; break;
                    case 'e': st = freeink::UsbMassStorageState::Ejected; break;
                    case 'd': st = freeink::UsbMassStorageState::Disconnected; break;
                    case 'i': st = freeink::UsbMassStorageState::IoError; break;
                    default:  st = freeink::UsbMassStorageState::WaitingForHost; break;
                }
                freeink::UsbMassStorage::simSetState(st);
                char lbl[24]; snprintf(lbl, sizeof(lbl), "MSC_%c", ms);
                g_label = lbl; printf(">>> USB_MSC_STATE %c\n", ms);
                run(3);
            } break;
            default: break;
        }
    }
    printf("done, %d frames\n", g_frame);
    return 0;
}
