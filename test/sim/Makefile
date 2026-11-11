# Headless simulator tests (no SDL window). Run from the firmware/ folder:
#   make -f test/sim/Makefile test              scenarios + reader round trip
#   make -f test/sim/Makefile coverage          the same with gcov, see test/coverage.sh
# Builds the same sources as simulator/Makefile, but with test/sim/headless_main.cpp
# instead of the SDL front-end, and the REAL network client (src/net/bookorbit_client.cpp)
# talking to an in-process fake Book Orbit server (test/sim/fake_bookorbit.cpp) through
# the HTTPClient/WiFi stubs in test/sim/stubs_net instead of simulator/src/net_sim.cpp.

SHELL := /bin/bash
CXX ?= c++
CC ?= cc
# DEVICE=x3 builds the simulator for the Xteink X3 (792x528 panel) into its own dir.
DEVICE ?= x4
ifeq ($(DEVICE),picco)
# Picco: touch plus only UP / DOWN / POWER keys (the headless runner rejects
# L, R, C and B script steps in this build).
BUILD ?= test/sim/build-picco
DEVFLAGS := -DFREEINK_DEVICE_PICCO=1 -DUI_FAKE_FRONTLIGHT=1 -DSIM_KEYS_UP_DOWN_POWER_ONLY=1
else ifeq ($(DEVICE),x4pro)
BUILD ?= test/sim/build-x4pro
DEVFLAGS := -DFREEINK_DEVICE_X4PRO=1 -DUI_FAKE_FRONTLIGHT=1
else ifeq ($(DEVICE),x3)
BUILD ?= test/sim/build-x3
DEVFLAGS := -DFREEINK_DEVICE_X3=1
else
BUILD ?= test/sim/build-x4
DEVFLAGS :=
endif
UNAME := $(shell uname)

SIM_CXX := $(filter-out simulator/src/sim_main.cpp simulator/src/net_sim.cpp .cpp,\
             $(shell sed -n '/^SRCS_CXX/,/^$$/p' simulator/Makefile | grep -o '[A-Za-z0-9_/-]*\.cpp'))
SIM_C   := $(shell sed -n '/^SRCS_C =/,/^$$/p' simulator/Makefile | grep -o '[A-Za-z0-9_/.-]*\.c\b')

OPT ?= -O1
# The heap diagnostics (off in device builds) are kept on here: a scenario checks the probe.
CXXFLAGS := -std=c++17 $(OPT) -g -DARDUINO=100 -DBOOKORBIT_MEM_DIAG=1 -w -MMD -MP -include test/sim/prelude.h $(DEVFLAGS) \
            -Itest/sim/stubs_net -Isimulator/include -Iinclude -Isrc -Isimulator/lib_adafruit_gfx \
            -Isimulator/lib_arduinojson/src -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd \
            -Ifreeink-sdk/libs/ui/FreeInkUI/include
CFLAGS   := $(OPT) -w -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd
FIB := freeink-sdk/libs/book/FreeInkBook
FIB_INCLUDES := -DFREEINK_BOOK_SMALL=1 -I$(FIB)/include -I$(FIB)/third_party/expat -I$(FIB)/third_party/miniz \
    -I$(FIB)/third_party/libunibreak -I$(FIB)/third_party/pngle -I$(FIB)/third_party/stb \
    -Ifreeink-sdk/libs/font/FreeInkFont/include -Ifreeink-sdk/libs/font/FreeInkFont/third_party/stb
CXXFLAGS += $(FIB_INCLUDES)
CFLAGS += $(FIB_INCLUDES)
ifeq ($(UNAME),Darwin)
CXXFLAGS += -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1
else
CXXFLAGS += -Itest/sim/stubs_linux
endif

NET_CXX  := src/net/bookorbit_client.cpp src/net/bookorbit_protocol.cpp src/net/opds_stream_parser.cpp test/sim/fake_bookorbit.cpp
APP_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SIM_CXX) $(NET_CXX)) $(patsubst %.c,$(BUILD)/%.o,$(SIM_C))

all: $(BUILD)/x4_headless $(BUILD)/reader_roundtrip $(BUILD)/engine_shots

$(BUILD)/x4_headless: $(APP_OBJS) $(BUILD)/test/sim/headless_main.o
	$(CXX) $(LDEXTRA) $^ -o $@ -lpthread

# The round-trip tool has its own main(): leave src/main.cpp out.
$(BUILD)/reader_roundtrip: $(filter-out $(BUILD)/src/main.o,$(APP_OBJS)) $(BUILD)/test/sim/reader_roundtrip.o
	$(CXX) $(LDEXTRA) $^ -o $@ -lpthread

$(BUILD)/engine_shots: $(filter-out $(BUILD)/src/main.o,$(APP_OBJS)) $(BUILD)/test/sim/engine_shots.o
	$(CXX) $(LDEXTRA) $^ -o $@ -lpthread

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	X4_HEADLESS=$(BUILD)/x4_headless python3 test/sim/run_sync_scenarios.py
	X4_HEADLESS=$(BUILD)/x4_headless python3 test/sim/run_x4_walkthrough.py
	@tmp=$$(mktemp -d) && cp -R simulator/sdcard/. $$tmp/ && cp test/sim/config.json $$tmp/config.json && \
	  $(BUILD)/reader_roundtrip $$tmp  "/books/Alice in Wonderland.epub" "/books/01. Frankenstein (1818).epub" "/books/Тестовая книга.txt" "/books/Pride and Prejudice.epub" | grep -v '^\['; \
	  rc=$${PIPESTATUS[0]}; rm -rf $$tmp; exit $$rc

# Picco: the walkthrough with touch + UP/DOWN/POWER only (the sync scenarios
# use L/R/C/B keys and run on the other builds).
picco-test:
	$(MAKE) -f test/sim/Makefile DEVICE=picco all
	PICCO_HEADLESS=test/sim/build-picco/x4_headless python3 test/sim/run_picco_walkthrough.py

coverage:
	test/coverage.sh

clean:
	rm -rf test/sim/build test/sim/build-cov

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

.PHONY: all test coverage clean
