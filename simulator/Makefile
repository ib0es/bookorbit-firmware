CXX ?= clang++
CC ?= clang

CXXFLAGS += -std=c++17 -O2 -DARDUINO=100
CXXFLAGS += -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1
CXXFLAGS += -Isimulator/include -Iinclude -Isrc
CXXFLAGS += -Isimulator/lib_adafruit_gfx
CXXFLAGS += -Isimulator/lib_arduinojson/src
CXXFLAGS += -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd
CXXFLAGS += -Ifreeink-sdk/libs/ui/FreeInkUI/include
# FreeInkBook (EPUB layout engine) + FreeInkFont, small memory profile like the X4.
FIB := freeink-sdk/libs/book/FreeInkBook
FIB_INCLUDES := -DFREEINK_BOOK_SMALL=1 -I$(FIB)/include -I$(FIB)/third_party/expat -I$(FIB)/third_party/miniz \
    -I$(FIB)/third_party/libunibreak -I$(FIB)/third_party/pngle -I$(FIB)/third_party/stb \
    -Ifreeink-sdk/libs/font/FreeInkFont/include -Ifreeink-sdk/libs/font/FreeInkFont/third_party/stb
CXXFLAGS += $(FIB_INCLUDES)
CXXFLAGS += $(shell sdl2-config --cflags)

CFLAGS += -O2
CFLAGS += -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd
CFLAGS += $(FIB_INCLUDES)

LDFLAGS += $(shell sdl2-config --libs)

SRCS_CXX = \
    simulator/src/sim_main.cpp \
    simulator/src/net_sim.cpp \
    src/main.cpp \
    src/app/app_state.cpp \
    src/app/app_controller.cpp \
    src/app/touch_router.cpp \
    src/app/screens/home_screen.cpp \
    src/app/screens/catalog_screen.cpp \
    src/app/screens/library_screen.cpp \
    src/app/screens/reader_screen.cpp \
    src/app/screens/appearance.cpp \
    src/app/screens/settings_screen.cpp \
    src/ui/modal.cpp \
    src/services/library_manager.cpp \
    src/services/power_service.cpp \
    src/services/bookmarks.cpp \
    src/services/sync_service.cpp \
    src/services/progress_sync.cpp \
    src/services/progress_store.cpp \
    src/services/sync_clock.cpp \
    src/services/time_utils.cpp \
    src/net/book_formats.cpp \
    src/ui/views/status_modal.cpp \
    src/ui/views/home_view.cpp \
    src/ui/views/catalog_view.cpp \
    src/ui/views/library_view.cpp \
    src/ui/views/settings_view.cpp \
    src/ui/views/reader_menu_view.cpp \
    src/ui/views/book_menu_view.cpp \
    src/hal/battery.cpp \
    src/hal/frontlight.cpp \
    src/hal/buttons.cpp \
    src/hal/display.cpp \
    src/hal/storage.cpp \
    src/hal/mem_stats.cpp \
    src/ui/i18n.cpp \
    src/ui/kit/ui_kit.cpp \
    freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp \
    src/ui/font.cpp \
    src/ui/jpeg_viewer.cpp \
    src/ui/keyboard_ui.cpp \
    src/ui/wifi_manager_ui.cpp \
    src/reader/book_reader.cpp \
    src/reader/engine/bitmap_book_font.cpp \
    src/reader/engine/sd_book_storage.cpp \
    src/reader/engine/ttf_book_font.cpp \
    src/reader/fonts/builtin_font.cpp \
    src/reader/font_store.cpp \
    src/convert/zip_writer.cpp \
    src/convert/fb2_to_epub.cpp \
    src/convert/mobi_to_epub.cpp \
    src/convert/txt_to_epub.cpp \
    src/convert/sd_convert.cpp \
    src/reader/engine/engine_text_counter.cpp \
    src/reader/epub_cfi.cpp \
    src/reader/cfi_map.cpp \
    simulator/lib_adafruit_gfx/Adafruit_GFX.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/FreeInkBook.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/BookCatalog.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/epub/ZipCatalog.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/epub/XmlSax.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/epub/PackageParsers.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/epub/ImageProbe.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/text/EntityFilter.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/text/Hyphenator.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/css/Css.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/layout/ChapterLayout.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/cache/PageCache.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/render/ImageRenderer.cpp \
    freeink-sdk/libs/book/FreeInkBook/src/render/PageRenderer.cpp \
    freeink-sdk/libs/font/FreeInkFont/src/TtfFont.cpp \
    freeink-sdk/libs/font/FreeInkFont/src/Gpos.cpp \
    freeink-sdk/libs/font/FreeInkFont/src/Gsub.cpp

# FreeInkBook's vendored C libraries; tjpgd comes from its tjpgd_impl.c (the
# same vendored decoder the firmware links instead of the ROM one).
SRCS_C = \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/miniz_impl.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/expat_xmlparse.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/expat_xmlrole.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/expat_xmltok.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/unibreak_impl.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/pngle_impl.c \
    freeink-sdk/libs/book/FreeInkBook/src/vendor/tjpgd_impl.c \
    freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c

BUILD_DIR = simulator/build
TARGET = simulator/x4_simulator

OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SRCS_CXX)) \
       $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS_C))

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	@echo "Build complete: $(TARGET)"

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)
