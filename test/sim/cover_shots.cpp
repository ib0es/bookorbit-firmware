// Renders an EPUB cover through the real extraction and cover viewer paths.
//   cover_shots <scratch sd root> <existing out dir> <book path on sd>
// The scratch card must have a covers directory; covers/probe.jpg is replaced.
#include <cstdio>
#include <cstring>
#include <string>
#include "Arduino.h"
#include "InputManager.h"
#include "EInkDisplay.h"
#include "SDCardManager.h"
#include "platform/hal/storage.h"
#include "platform/hal/display.h"
#include "ui/font.h"
#include "reader/book_reader.h"
#include "ui/jpeg_viewer.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = {false};
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = {false};
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
void simPumpEvents() {}
void simInputPolled() {}

static std::string g_out;
static std::string g_name;

static void onUpdate(const uint8_t* fb, int) {
    FILE* f = fopen((g_out + "/" + g_name + ".pgm").c_str(), "wb");
    fprintf(f, "P5 %d %d 255\n", EPD_HEIGHT, EPD_WIDTH);
    for (int y = 0; y < EPD_WIDTH; y++)
        for (int x = 0; x < EPD_HEIGHT; x++) {
            int rx = y, ry = EPD_HEIGHT - 1 - x;
            fputc((fb[(rx / 8) + ry * (EPD_WIDTH / 8)] & (0x80 >> (rx % 8))) ? 255 : 0, f);
        }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("usage: cover_shots <scratch sd root> <out dir> <book>\n");
        return 2;
    }
    SDCardManager::getInstance().setRoot(argv[1]);
    g_out = argv[2];
    freeink::FreeInkDisplay::s_callback = onUpdate;
    Display.begin();
    Font.begin(Display);
    Storage.begin();
    if (!BookReader::extractCover(argv[3], "/covers/probe.jpg")) return 1;
    for (int size : {0, 1}) {
        Display.clear();
        g_name = size ? "sleep" : "thumbnail";
        if (!JpegViewer::drawCover("/covers/probe.jpg", 0, 0, size ? 480 : 160,
                                   size ? 752 : 240, COVER_FIT_SCALE)) return 1;
        Display.update();
    }
    return 0;
}
