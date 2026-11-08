// Reader position invariants on real books, for every page:
//   setPageByProgress(getProgress()), setPageByXPath(getXPath(), getProgress()) and, for EPUBs,
//   setPageByCfi(getCfi(), getProgress()) keep the page.
// Plus a CFI in the Book Orbit web reader format for "01. Frankenstein (1818).epub"
// (the start of "Chapter 10") must open the page where that chapter starts.
//   reader_roundtrip <sd root> <book path>...
#include <cstdio>
#include <string>
#include "Arduino.h"
#include "InputManager.h"
#include "EInkDisplay.h"
#include "SDCardManager.h"
#include "hal/storage.h"
#include "hal/display.h"
#include "ui/font.h"
#include "reader/simple_reader.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = { false };
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = { false };
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
void simPumpEvents() {}
void simInputPolled() {}

int main(int argc, char** argv) {
    SDCardManager::getInstance().setRoot(argv[1]);
    Display.begin(); Font.begin(Display); Storage.begin();
    int totalFail = 0;
    for (int a = 2; a < argc; a++) {
        if (!Reader.openBook(argv[a])) { printf("open failed %s\n", argv[a]); continue; }
        const int n = Reader.getTotalPages();
        int failP = 0, failX = 0, failC = 0, checked = 0;
        const bool epub = String(argv[a]).endsWith(".epub");
        for (int p = 1; p <= n; p++) {
            Reader.setPage(p);
            float pct = Reader.getProgress();
            String x = Reader.getXPath();
            Reader.setPageByProgress(pct);
            if (Reader.getCurrentPage() != p) { if (failP++ < 3) printf("  pct  p%d -> p%d (%.5f)\n", p, Reader.getCurrentPage(), pct); }
            Reader.setPage(1);
            Reader.setPageByXPath(x, pct);
            if (Reader.getCurrentPage() != p) { if (failX++ < 3) printf("  xpath p%d -> p%d (%s)\n", p, Reader.getCurrentPage(), x.c_str()); }
            if (epub) {
                Reader.setPage(p);
                const String c = Reader.getCfi();
                Reader.setPage(1);
                if (c.length() == 0 || !Reader.setPageByCfi(c, pct) || Reader.getCurrentPage() != p) {
                    if (failC++ < 3) printf("  cfi  p%d -> p%d (%s)\n", p, Reader.getCurrentPage(), c.c_str());
                }
            }
            checked++;
        }
        if (String(argv[a]).endsWith("01. Frankenstein (1818).epub")) {
            // Web-reader style position: epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0) = "Chapter 10" heading.
            Reader.setPage(1);
            const bool ok = Reader.setPageByCfi("epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0)", 0.372f);
            const int page = Reader.getCurrentPage();
            const String here = Reader.getXPath();
            Reader.setPage(page - 1);
            const String before = Reader.getXPath();
            const bool chapterStart = here.startsWith("/body/DocFragment[17]/") && before.startsWith("/body/DocFragment[16]/");
            printf("  web CFI 'Chapter 10' -> page %d (%s)%s\n", page, here.c_str(), ok && chapterStart ? "" : "  WRONG");
            if (!ok || !chapterStart) failC++;
        }
        printf("%-40s pages=%d  percent-fail=%d  xpath-fail=%d  cfi-fail=%d\n", argv[a], checked, failP, failX, failC);
        totalFail += failP + failX + failC;
        Reader.closeBook();
    }
    printf("%s\n", totalFail ? "ROUNDTRIP FAILED" : "ROUNDTRIP OK");
    return totalFail ? 1 : 0;
}
