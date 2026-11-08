// Pre-includes standard headers before simulator/include/Arduino.h defines min()/max()
// macros, which otherwise break libstdc++ headers (Linux). Harmless on macOS.
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <map>
#include <set>
#include <deque>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <random>
#include <chrono>
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <iomanip>
#include <regex>
#include <cstring>
#include <ctime>
#include <list>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <variant>
#include <limits>
#include <numeric>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>
#include <dirent.h>

// FreeRTOS yield used by the network client.
#define taskYIELD() ((void)0)
