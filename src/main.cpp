#include <Arduino.h>
#include "app/app_controller.h"

void setup() { AppController::getInstance().setup(); }

void loop() { AppController::getInstance().loop(); }

#ifdef ESP32
extern "C" const char bookOrbitFontCatalogCapability[] __attribute__((section(".rodata_custom_desc"), used, retain)) =
        "BookOrbitFontStoreV2";
#endif
