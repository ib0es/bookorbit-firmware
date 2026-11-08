CXX ?= clang++
CC ?= clang

CXXFLAGS += -std=c++17 -O2 -DARDUINO=100
CXXFLAGS += -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1
CXXFLAGS += -Isimulator/include -Iinclude -Isrc
CXXFLAGS += -Isimulator/lib_adafruit_gfx
CXXFLAGS += -Isimulator/lib_arduinojson/src
CXXFLAGS += -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd
CXXFLAGS += -Ifreeink-sdk/libs/ui/FreeInkUI/include
CXXFLAGS += $(shell sdl2-config --cflags)

CFLAGS += -O2
CFLAGS += -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd

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
    src/app/screens/settings_screen.cpp \
    src/ui/modal.cpp \
    src/services/library_manager.cpp \
    src/services/power_service.cpp \
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
    src/reader/simple_reader.cpp \
    src/reader/epub_extractor.cpp \
    src/reader/epub_cfi.cpp \
    src/reader/html_stripper.cpp \
    src/reader/koreader_doc_id.cpp \
    src/reader/koreader_sync_mapper.cpp \
    simulator/lib_adafruit_gfx/Adafruit_GFX.cpp

SRCS_C = \
    src/reader/miniz.c \
    freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.c

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
