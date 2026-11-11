// Renders pages of a book with BookReader (the FreeInkBook reader) into PGM files.
//   engine_shots <sd root> <out dir> <book path> [page ...]   (pages 1-based; "n" = next page)
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

bool InputManager::s_simState[InputManager::BTN_COUNT] = { false };
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = { false };
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
    if (argc < 4) { printf("usage: engine_shots <sd root> <out dir> <book> [page|n ...]\n"); return 2; }
    SDCardManager::getInstance().setRoot(argv[1]);
    g_out = argv[2];
    freeink::FreeInkDisplay::s_callback = onUpdate;
    Display.begin(); Font.begin(Display); Storage.begin();
    static BookReader reader;
    if (!reader.openBook(argv[3])) { printf("open failed\n"); return 1; }
    printf("title '%s', %d pages (estimate), %d TOC entries\n", reader.getTitle().c_str(), reader.getTotalPages(),
           reader.getChapterCount());
    for (int a = 4; a < argc; a++) {
        g_name = std::to_string(a - 3);
        if (strcmp(argv[a], "n") == 0) reader.nextPage();
        else if (strcmp(argv[a], "p") == 0) reader.prevPage();
        else if (argv[a][0] == '%') { reader.setPageByProgress(atof(argv[a] + 1) / 100.0f); reader.renderCurrentPage(); }
        else if (argv[a][0] == 'c') { reader.goToChapter(atoi(argv[a] + 1)); reader.renderCurrentPage(); }
        else { reader.setPage(atoi(argv[a])); reader.renderCurrentPage(); }
        printf("shot %s: page %d/%d, %.1f%%, chapter '%s', cfi %s\n", g_name.c_str(), reader.getCurrentPage(),
               reader.getTotalPages(), reader.getProgress() * 100.0f, reader.getCurrentChapterTitle().c_str(),
               reader.getCfi().c_str());
    }
    return 0;
}
