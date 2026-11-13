# Book Orbit E-Ink Reader Firmware

<p align="left">
  <a href="https://ib0es.github.io/bookorbit-firmware/">
    <img src="https://img.shields.io/badge/⚡_Web_Flasher-Install_in_Browser-2ea44f?style=for-the-badge&logo=googlechrome&logoColor=white" alt="Open Web Flasher" />
  </a>
  <a href="https://github.com/ib0es/bookorbit-firmware/releases">
    <img src="https://img.shields.io/badge/Firmware-Releases-blue?style=for-the-badge&logo=github" alt="Releases" />
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-gray?style=for-the-badge" alt="MIT License" />
  </a>
</p>

> 🚀 **Quick Install**: Flash and configure your reader directly from Chrome or Edge (no setup required) with the **[Book Orbit Web Flasher](https://ib0es.github.io/bookorbit-firmware/)**!

> [!NOTE]
> **Release Status**:
> * **Beta**: **Xteink X4** and **Picco** (hardware-tested, stable reading and sync features).
> * **Alpha**: **Xteink X3**, **Xteink X4 Classic**, and **Xteink X4 Pro** (experimental builds; feedback and hardware testing are welcome).
>
> Feedback, bug reports, and pull requests are warmly welcome!

An open-source, lightweight, custom firmware engineered from scratch for compact E-Ink readers including the **XTEINK** series (X4, X3, X4 Classic, X4 Pro) and **Picco**.

The firmware natively integrates with your self-hosted **[Book Orbit](https://github.com/ib0es/book-orbit)** library:
* **OPDS Catalog** (`/api/v1/opds`): browse libraries, authors, series, and download books directly to the MicroSD card.
* **KOReader Reading Progress Sync (KOSync)** (`/api/v1/koreader`): automatic bidirectional reading position sync between the device and Book Orbit web reader.
* **Extreme Power Efficiency**: Wi-Fi powers on solely during sync or book downloads and turns off immediately; the E-Ink display draws zero power when static; automatically enters **Deep Sleep** during idle (~15–20 µA standby current).
* **Interface languages**: English, Russian and Spanish (switch in Settings → Interface language). Strings live in `locales/*.json`; after editing them run `python3 tools/generate_i18n.py`.
* **Built-in Web Flasher**: flash and pre-configure the device directly from your web browser via the Web Serial API.

---

## Supported Devices

| Device | Status | PlatformIO Environment | MCU / Display | Input & Frontlight |
|---|---|---|---|---|
| **Xteink X4** | **Beta** | `x4` (default) | ESP32-C3 / 4.3" 480×800 E-Ink | Physical buttons (ADC divider) |
| **Picco** | **Beta** | `picco` | ESP32-S3 / 3.97" 800×480 E-Ink (portrait) | Touchscreen + UP / DOWN / POWER keys, frontlight with a side switch |
| **Xteink X3** | **Alpha** | `x3` | ESP32-C3 / 3.7" 528×792 E-Ink | Physical buttons |
| **Xteink X4 Classic** | **Alpha** | `x4c` | ESP32-S3 / 4.3" 480×800 E-Ink | Physical buttons |
| **Xteink X4 Pro** | **Alpha** | `x4pro` | ESP32-S3 / 4.3" 480×800 E-Ink | Capacitive touch, frontlight |

---

## 1. Quick Installation via Web Flasher (Browser)

You can flash and configure the firmware directly from any Chromium-based browser (Chrome, Edge, Brave, Opera) without installing PlatformIO or Python:
1. Connect your reader to your computer using a USB-C data cable.
2. Open the **Web Flasher** hosted on GitHub Pages: `https://ib0es.github.io/bookorbit-firmware/`.
   To run it locally: `cd web-flasher && npm ci && npm run dev` (the production build goes to `docs/`, which is built by CI and not stored in the repository).
3. Select your device model (or choose Auto-detect).
4. *(Optional)* Enter your Wi-Fi credentials and Book Orbit server URL — they will be written straight to NVS flash memory during installation.
5. Click **Install** and select the serial port for your device.

---

## 2. Direct Firmware Downloads (Pre-compiled Binaries)

Ready-to-flash monolithic binaries (containing bootloader, partition table, otadata, and application) are available for each device for both official tagged releases and continuous rolling builds from the `main` branch.

| Device | MCU | Latest Release (Stable) | Latest Build (Master) | Web Flasher |
|---|---|---|---|---|
| **Xteink X4** | ESP32-C3 | [bookorbit-x4.bin](https://github.com/ib0es/bookorbit-firmware/releases/latest/download/bookorbit-x4.bin) | [bookorbit-x4.bin](https://github.com/ib0es/bookorbit-firmware/releases/download/latest/bookorbit-x4.bin) | [Flash Online](https://ib0es.github.io/bookorbit-firmware/) |
| **Xteink X3** | ESP32-C3 | [bookorbit-x3.bin](https://github.com/ib0es/bookorbit-firmware/releases/latest/download/bookorbit-x3.bin) | [bookorbit-x3.bin](https://github.com/ib0es/bookorbit-firmware/releases/download/latest/bookorbit-x3.bin) | [Flash Online](https://ib0es.github.io/bookorbit-firmware/) |
| **Xteink X4 Classic** | ESP32-S3 | [bookorbit-x4c.bin](https://github.com/ib0es/bookorbit-firmware/releases/latest/download/bookorbit-x4c.bin) | [bookorbit-x4c.bin](https://github.com/ib0es/bookorbit-firmware/releases/download/latest/bookorbit-x4c.bin) | [Flash Online](https://ib0es.github.io/bookorbit-firmware/) |
| **Xteink X4 Pro** | ESP32-S3 | [bookorbit-x4pro.bin](https://github.com/ib0es/bookorbit-firmware/releases/latest/download/bookorbit-x4pro.bin) | [bookorbit-x4pro.bin](https://github.com/ib0es/bookorbit-firmware/releases/download/latest/bookorbit-x4pro.bin) | [Flash Online](https://ib0es.github.io/bookorbit-firmware/) |
| **BOOX Picco** | ESP32-S3 | [bookorbit-picco.bin](https://github.com/ib0es/bookorbit-firmware/releases/latest/download/bookorbit-picco.bin) | [bookorbit-picco.bin](https://github.com/ib0es/bookorbit-firmware/releases/download/latest/bookorbit-picco.bin) | [Flash Online](https://ib0es.github.io/bookorbit-firmware/) |

### Flashing via `esptool.py`:
Each binary is an all-in-one image and is flashed at offset `0x0`:
```bash
esptool.py write_flash 0x0 bookorbit-x4.bin
```

### CLI Downloader:
Download binaries directly to your local computer with:
```bash
# Download latest master build for X4:
python3 tools/download_firmware.py x4

# Download latest stable release for Picco:
python3 tools/download_firmware.py picco --channel release

# Download specific release tag:
python3 tools/download_firmware.py x3 --tag v0.1.0
```

---

## 3. Configuration & Connection Setup

Settings can be configured using any of the following methods:

### Method A: Directly on the device via on-screen keyboard (Recommended)
1. Go to **Settings** from the main menu (`CONFIRM` button or screen tap).
2. Available sections:
   * **Wi-Fi Settings**:
     * **Scan Wi-Fi Networks**: search nearby networks and enter passwords using the on-screen keyboard.
     * **SSID** and **Password**: manual entry.
     * **Test Connection**: checks link, displays assigned IP address and RSSI signal level.
     * **Web Setup via Phone**: launches `BookOrbit-Config` captive portal AP (192.168.4.1).
   * **Book Orbit Server**:
     * **Server URL**: enter your server address (e.g., `http://192.168.1.100:3000`).
     * **Username** and **Password**: account credentials.
     * **Test Server Connection**: end-to-end ping, Wi-Fi, and login verification.
   * **Synchronization**: when reading progress is synced automatically —
     * **On book close** (in the background, the screen is not blocked; on by default),
     * **Before sleep**, **After wake-up**, **On power on**.

### Method B: `config.json` on the MicroSD card
Place a `config.json` file in the root directory of your MicroSD card:
```json
{
  "wifi_ssid": "Your_WiFi_SSID",
  "wifi_pass": "Your_WiFi_Password",
  "server_url": "http://192.168.1.100:3000",
  "username": "your_username",
  "password": "your_password"
}
```

### Method C: Compile-time credentials in source code
Copy `include/secrets.example.h` to `include/secrets.h` (this file is ignored by `.gitignore` and won't be committed):
```cpp
#define DEFAULT_WIFI_SSID         "Your_WiFi_SSID"
#define DEFAULT_WIFI_PASSWORD     "Your_WiFi_Password"
#define DEFAULT_BOOKORBIT_HOST     "http://192.168.1.100:3000"
#define DEFAULT_BOOKORBIT_USER     "your_username"
#define DEFAULT_BOOKORBIT_PASS     "your_password"
```

---

## 4. Building and Flashing via PlatformIO

### Clone repository with submodules:
```bash
git clone --recurse-submodules https://github.com/ib0es/bookorbit-firmware.git
cd bookorbit-firmware

# If cloned previously without --recurse-submodules:
git submodule update --init --recursive
```

### Using the PlatformIO CLI:
```bash
# Build and flash default target (Xteink X4):
pio run -e x4 --target upload

# For other supported devices:
pio run -e x3 --target upload      # Xteink X3
pio run -e x4c --target upload     # Xteink X4 Classic
pio run -e x4pro --target upload   # Xteink X4 Pro
pio run -e picco --target upload   # Picco

# Open serial debug monitor:
pio device monitor -b 115200
```

---

## 5. Hardware Pinout (Example: XTEINK X4 ESP32-C3)

| Component | ESP32-C3 Pin | Description |
|---|---|---|
| **Display SCK** | `GPIO 8` | Shared SPI clock |
| **Display MOSI** | `GPIO 10` | Shared SPI MOSI |
| **Display CS** | `GPIO 21` | Chip Select (Active LOW) |
| **Display DC** | `GPIO 4` | Command / Data select |
| **Display RST** | `GPIO 5` | Display panel reset |
| **Display BUSY** | `GPIO 6` | Display busy state (Active HIGH) |
| **MicroSD CS** | `GPIO 12` | MicroSD Chip Select (Active LOW) |
| **MicroSD MISO** | `GPIO 7` | SPI MISO for SD card |
| **Power Button** | `GPIO 3` | Deep Sleep wakeup pin |
| **ADC Buttons 1** | `GPIO 1` | Resistor ladder: Back (~3564), Confirm (~2731), Left (~1516), Right (~10) |
| **ADC Buttons 2** | `GPIO 2` | Resistor ladder: Up (~2255), Down (~10) |
| **Battery ADC** | `GPIO 0` | Battery voltage sensing via 2:1 voltage divider |
| **USB 5V** | `GPIO 20` | USB charge voltage detection |
| **USB CDC** | `GPIO 18 / 19`| Built-in USB-Serial CDC for flashing and logging |

---

## 6. Controls and Navigation

* **Power Button**:
  * Short press: wake up from sleep.
  * Long press (> 1s): force enter Deep Sleep.
* **Home Screen**:
  * The card shows recently read books; swipe left / right to step through them, tap to open.
* **Reader Screen**:
  * `RIGHT` / `DOWN` (or swipe left / tap on the right): next page.
  * `LEFT` / `UP` (or swipe right / tap on the left): previous page.
  * Tap the center of the screen or press `CONFIRM`: reader menu — table of contents, font size (12–48 pt), sync now, close book.
  * `BACK`: exit to the home screen.
* **Catalog & Library**:
  * Browse by libraries, authors and series; search by title; optionally hide finished books.
  * Books on the card have a filled badge (progress or "Downloaded"); books only on the server have an outlined badge with a cloud icon.
  * `UP` / `DOWN` (or swipe up / down for the next page): navigate the list.
  * `CONFIRM`, `LEFT` or a tap on a book: book menu — Read / Download, Sync, Delete from the card (removes the book file, its cover and caches; the book stays in the catalog as a cloud book).
  * `BACK` (or swipe right / tap ◀ in the title): return to the previous level.
* **Picco** (touch + UP / DOWN / POWER only): every screen is reachable by touch; dialogs have on-screen buttons. The side switch turns the frontlight on and off.

---

## 7. Simulator and Automated Tests

### Desktop Simulator (SDL2)
You can run and test the UI locally on macOS or Linux without physical hardware:
```bash
./run_simulator.sh
```

The simulator and the tests use the PlatformIO library cache: run `pio pkg install -e x4` once after cloning.
The simulator's card (`simulator/sdcard/`) holds public-domain books from Project Gutenberg.
The tests copy it to a temporary folder with the dummy settings from `test/sim/config.json`; your own `simulator/sdcard/config.json` (ignored by git) is used only by the interactive simulator.

### Host Unit Tests
Pure-logic unit tests without requiring hardware or Arduino emulation (OPDS parser, KOReader progress sync, EPUB CFI, i18n, Book Orbit protocol):
```bash
./test/host/run.sh
```

### Headless Simulator Tests
The firmware runs in a headless simulator against a fake Book Orbit server, driven by scripted button presses and touches:
```bash
make -f test/sim/Makefile test                     # sync scenarios + reader position round-trips (X4)
make -f test/sim/Makefile DEVICE=x3 test           # the same on the X3 layout
make -f test/sim/Makefile DEVICE=picco picco-test  # Picco walkthrough: touch + UP / DOWN / POWER only
```

### Lint and Format
| Part | Linter | Formatter | Config |
|---|---|---|---|
| C/C++ (`src`, `test`, `simulator/src`) | cppcheck | clang-format | `.clang-format`, `.clang-format-ignore`, `tools/lint/cppcheck-suppressions.txt` |
| Python (`tools`, `test`) | ruff, mypy | ruff format | `ruff.toml`, `pyproject.toml` |
| Web Flasher | ESLint | Prettier | `web-flasher/eslint.config.js`, `web-flasher/.prettierrc.json` |

```bash
pip install -r tools/lint/requirements.txt   # clang-format, cppcheck, ruff, mypy
(cd web-flasher && npm ci)                   # eslint, prettier
tools/lint.sh            # everything (or: tools/lint.sh cpp|py|web); CI runs the same
tools/format.sh          # fix in place
tools/format.sh src/x.cpp  # format whole C/C++ files
```
C/C++ is formatted only on the lines changed since the merge base with `origin/main` (`LINT_BASE` overrides), so existing code is reformatted gradually as it is touched. cppcheck findings that predate the linter are listed as a baseline at the end of the suppressions file.

---

## Disclaimer

> [!WARNING]
> This is custom, third-party firmware provided **"as is"**, without warranty of any kind, express or implied. Flashing third-party firmware onto hardware carries inherent risks (including, but not limited to, bricking your device, hardware malfunction, data loss, or voiding manufacturer warranties).
>
> The authors and contributors of this project assume no liability or responsibility for any damage, data loss, bricked hardware, or other issues arising from the installation or use of this software. Proceed entirely at your own risk.
>
> This project is an independent community effort and is neither affiliated with, supported by, nor endorsed by XTEINK or any hardware manufacturer.

---

## License

This project is licensed under the [MIT License](LICENSE).
Bundled third-party code, fonts and test books keep their own licenses — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
