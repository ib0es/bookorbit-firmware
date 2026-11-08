#pragma once
#include <Arduino.h>

#ifndef LOG_ERR
#define LOG_ERR(tag, fmt, ...) Serial.printf("[%s][ERR] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#ifndef LOG_INF
#define LOG_INF(tag, fmt, ...) Serial.printf("[%s][INF] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#ifndef LOG_WRN
#define LOG_WRN(tag, fmt, ...) Serial.printf("[%s][WRN] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#ifndef LOG_DBG
#define LOG_DBG(tag, fmt, ...) Serial.printf("[%s][DBG] " fmt "\n", tag, ##__VA_ARGS__)
#endif
