#include <Arduino.h>
#include "app/app_controller.h"

void setup() {
    AppController::getInstance().setup();
}

void loop() {
    AppController::getInstance().loop();
}
