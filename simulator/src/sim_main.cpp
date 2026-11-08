#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include "../include/Arduino.h"
#include "../include/InputManager.h"
#include "../include/EInkDisplay.h"
#include "../include/SDCardManager.h"
#include "../../include/pins.h"

// Forward declarations from src/main.cpp
void setup();
void loop();

// Static definitions for simulated components
bool InputManager::s_simState[InputManager::BTN_COUNT] = { false };
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = { false };
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;

static SDL_Window* s_window = nullptr;
static SDL_Renderer* s_renderer = nullptr;
static SDL_Texture* s_texture = nullptr;
static bool s_running = true;
static bool s_needsRender = true;
static bool s_sdlReady = false;
static uint32_t s_pixelBuf[EPD_WIDTH * EPD_HEIGHT]; // 800 x 480 raw buffer

// Physical dimensions
static const int SCREEN_W = EPD_HEIGHT;  // portrait: 480 (X4) / 528 (X3)
static const int SCREEN_H = EPD_WIDTH;   // 800 (X4) / 792 (X3)

// E-Ink Palette Colors
static const uint32_t COLOR_EPD_WHITE = 0xFFF5F4EF; // Authentic paper white
static const uint32_t COLOR_EPD_BLACK = 0xFF181818; // Deep e-ink black

static void updateDisplayTexture(const uint8_t* rawEpdBuffer, int mode) {
    if (!rawEpdBuffer) return;

    // Convert raw 800x480 1-bit buffer to 480x800 32-bit ARGB (DISPLAY_ROTATION = 3)
    // Rotation 3 mapping: screen(x, y) -> raw(y, EPD_HEIGHT - 1 - x)
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int rx = y;
            int ry = EPD_HEIGHT - 1 - x;
            if (rx >= 0 && rx < EPD_WIDTH && ry >= 0 && ry < EPD_HEIGHT) {
                int byteIdx = (rx / 8) + (ry * (EPD_WIDTH / 8));
                uint8_t bit = (rawEpdBuffer[byteIdx] & (0x80 >> (rx % 8)));
                s_pixelBuf[x + y * SCREEN_W] = bit ? COLOR_EPD_WHITE : COLOR_EPD_BLACK;
            } else {
                s_pixelBuf[x + y * SCREEN_W] = COLOR_EPD_WHITE;
            }
        }
    }

    if (s_texture) {
        SDL_UpdateTexture(s_texture, nullptr, s_pixelBuf, SCREEN_W * sizeof(uint32_t));
        if (s_renderer) {
            SDL_RenderClear(s_renderer);
            SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
            SDL_RenderPresent(s_renderer);
            s_needsRender = false;
        }
    }

    // Simulate authentic E-Ink refresh flash for FULL_REFRESH
    if (mode == freeink::FreeInkDisplay::FULL_REFRESH && s_renderer) {
        SDL_SetRenderDrawColor(s_renderer, 0x18, 0x18, 0x18, 0xFF);
        SDL_RenderClear(s_renderer);
        SDL_RenderPresent(s_renderer);
        SDL_Delay(35);
        SDL_SetRenderDrawColor(s_renderer, 0xF5, 0xF4, 0xEF, 0xFF);
        SDL_RenderClear(s_renderer);
        SDL_RenderPresent(s_renderer);
        SDL_Delay(35);
        if (s_texture) {
            SDL_RenderClear(s_renderer);
            SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
            SDL_RenderPresent(s_renderer);
        }
    }
}

static void onDisplayUpdate(const uint8_t* buffer, int mode) {
    updateDisplayTexture(buffer, mode);
}

void simInputPolled() {}

void simPumpEvents() {
    if (!s_sdlReady) return;

    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        if (evt.type == SDL_QUIT) {
            s_running = false;
            exit(0);
        } else if (evt.type == SDL_KEYDOWN && !evt.key.repeat) {
            switch (evt.key.keysym.sym) {
                case SDLK_UP:
                    InputManager::simPress(InputManager::BTN_UP);
                    break;
                case SDLK_DOWN:
                    InputManager::simPress(InputManager::BTN_DOWN);
                    break;
                case SDLK_LEFT:
                    InputManager::simPress(InputManager::BTN_LEFT);
                    break;
                case SDLK_RIGHT:
                    InputManager::simPress(InputManager::BTN_RIGHT);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    InputManager::simPress(InputManager::BTN_CONFIRM);
                    break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE:
                    InputManager::simPress(InputManager::BTN_BACK);
                    break;
                case SDLK_p:
                case SDLK_SPACE:
                    InputManager::simPress(InputManager::BTN_POWER);
                    break;
                default:
                    break;
            }
        } else if (evt.type == SDL_KEYUP) {
            switch (evt.key.keysym.sym) {
                case SDLK_UP:
                    InputManager::simRelease(InputManager::BTN_UP);
                    break;
                case SDLK_DOWN:
                    InputManager::simRelease(InputManager::BTN_DOWN);
                    break;
                case SDLK_LEFT:
                    InputManager::simRelease(InputManager::BTN_LEFT);
                    break;
                case SDLK_RIGHT:
                    InputManager::simRelease(InputManager::BTN_RIGHT);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    InputManager::simRelease(InputManager::BTN_CONFIRM);
                    break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE:
                    InputManager::simRelease(InputManager::BTN_BACK);
                    break;
                case SDLK_p:
                case SDLK_SPACE:
                    InputManager::simRelease(InputManager::BTN_POWER);
                    break;
                default:
                    break;
            }
        }
    }

    if (s_needsRender && s_renderer && s_texture) {
        SDL_RenderClear(s_renderer);
        SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
        SDL_RenderPresent(s_renderer);
        s_needsRender = false;
    }
}

int main(int argc, char* argv[]) {
    printf("=====================================================\n");
    printf("  XTEINK X4 E-Reader Simulator (Book Orbit)\n");
    printf("=====================================================\n");
    printf("  Controls:\n");
    printf("    [UP / DOWN]       - Move cursor / Menu navigation\n");
    printf("    [LEFT / RIGHT]    - Previous / Next Page or Carousel\n");
    printf("    [ENTER / RETURN]  - CONFIRM / Open Item\n");
    printf("    [ESCAPE / BKSP]   - BACK / Return\n");
    printf("    [P / SPACE]       - POWER (Click: Sleep / Long: Off)\n");
    printf("=====================================================\n");

    // Ensure sdcard directory exists
    std::string exeDir = std::filesystem::current_path().string();
    std::string sdPath = exeDir + "/simulator/sdcard";
    if (!std::filesystem::exists(sdPath)) {
        sdPath = exeDir + "/sdcard";
    }
    SDCardManager::getInstance().setRoot(sdPath);
    printf("[Simulator] MicroSD root mapped to: %s\n", sdPath.c_str());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    s_window = SDL_CreateWindow(
        "XTEINK X4 - Book Orbit Simulator (480x800)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W,
        SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!s_window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, 0);
    }

    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W,
        SCREEN_H
    );

    // Register display callback
    freeink::FreeInkDisplay::setUpdateCallback(onDisplayUpdate);

    // Initialize initial white canvas
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        s_pixelBuf[i] = COLOR_EPD_WHITE;
    }
    SDL_UpdateTexture(s_texture, nullptr, s_pixelBuf, SCREEN_W * sizeof(uint32_t));

    s_sdlReady = true;

    // Call firmware setup()
    printf("[Simulator] Booting firmware setup()...\n");
    setup();
    printf("[Simulator] Setup completed. Entering main event loop.\n");

    // Main Simulator Loop
    while (s_running) {
        simPumpEvents();

        // Run one iteration of firmware loop()
        loop();

        simPumpEvents();

        // Throttle slightly
        SDL_Delay(8);
    }

    s_sdlReady = false;
    if (s_texture) SDL_DestroyTexture(s_texture);
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window) SDL_DestroyWindow(s_window);
    SDL_Quit();

    printf("[Simulator] Exited cleanly.\n");
    return 0;
}
