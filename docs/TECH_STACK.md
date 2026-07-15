# Technology Stack

This document summarizes the main technologies used by this repository.
The current maintained target is the M5Stack M5Paper firmware.

## Target Hardware

- M5Stack M5Paper
  - ESP32-D0WDQ6-V3 class MCU
  - 4.7 inch 960x540 e-paper display
  - IT8951 e-paper controller
  - PSRAM
  - SD card storage
  - CP210x USB-to-UART bridge for flashing and serial monitoring

## Build System

- PlatformIO
  - Main environment: `m5paper`
  - Build command: `pio run -e m5paper`
  - Upload command: `pio run -e m5paper -t upload`
  - Platform pinned for M5Paper: `espressif32@6.9.0`
- Arduino framework for M5Paper
  - PlatformIO package: Arduino-ESP32 2.x via `espressif32@6.9.0`
- ESP-IDF for secondary Paper S3 environments
  - Environments: `paper_s3_idf`, `paper_s3_release`
- Native PlatformIO environment for host-side tests
  - Environment: `native`

## Programming Languages

- C++
  - Main firmware, EPUB parsing, rendering, board abstraction, UI logic
- C
  - Embedded libraries and low-level image/zip helpers
- Python
  - Font/image generation scripts
  - PlatformIO pre-build helper script: `fix_assembler.py`
- Shell
  - Release and asset generation scripts
- Assembly
  - ULP wake/sleep support in `ulp/main.S`

## M5Paper Runtime Libraries

- M5GFX
  - E-paper display driver and framebuffer rendering
  - M5Paper display integration through IT8951
- M5Unified
  - M5Paper device abstraction
  - Power, touch, button, and display integration
- efont Unicode Font Data
  - Font data used by M5 libraries
- Arduino core libraries
  - `SD`
  - `SPI`
  - `FS`
  - `WiFi`
  - `WebServer`
- M5StackWiFiUploader
  - WiFi EPUB upload UI and transfer handling

## EPUB and Document Processing

- pugixml
  - XML parsing for EPUB metadata and XHTML content
- miniz 3.1.0
  - ZIP archive reading for EPUB files
- Custom EPUB parser
  - EPUB library index
  - OPF parsing
  - XHTML block parsing
  - Page layout
  - Bookmark persistence
- Custom text layout
  - Word wrapping and pagination for e-paper display dimensions

## Rendering and Image Handling

- M5GFX framebuffer rendering
  - Full-screen PSRAM-backed `LGFX_Sprite`
  - E-paper update modes selected by operation
- JPEGDEC
  - JPEG image decoding
- PNGdec
  - PNG image decoding
- pngle
  - PNG decoding path retained for M5Paper/Paper S3 image handling
- tjpgd3
  - Embedded JPEG helper library retained in `lib/tjpgd3`
- Ordered dithering
  - 1-bit image output for M5Paper e-paper rendering

## Storage and Filesystem

- SD card
  - Primary storage for books and runtime files
  - EPUB path: `/Books`
  - Library index: `/Books/BOOKS.IDX`
  - Optional sleep image: `/Sleep/bg.png`
- SPIFFS
  - Supported by the original project, but not the preferred M5Paper path

## Power and Input

- M5Paper touch input
  - Implemented through M5Unified and board-specific touch controls
- M5Paper physical buttons
  - Board-specific button controls
- Deep sleep
  - State persistence across sleep
  - Wake handling through ESP32 sleep features and ULP support
- Battery monitoring
  - M5Paper-specific battery implementation
  - ADC-based battery support for Paper S3 environments

## Board Abstraction

- `src/boards`
  - Shared board interface
  - M5Paper implementation
  - Paper S3 implementation
  - LilyGo/EPDiy-derived implementations retained from the original project
- `src/boards/controls`
  - Button and touch control abstractions
- `src/boards/battery`
  - Battery abstraction and board-specific battery logic

## Fonts and Assets

- Generated bitmap font data under `lib/Fonts`
- SourceSansPro font sources under `scripts`
- Image conversion scripts under `scripts`
- Sleep/background image assets used during development

## Testing

- PlatformIO native tests
  - `pio test -e native`
- Test coverage areas
  - EPUB loading
  - EPUB index loading
  - HTML/entity parsing
  - RubbishHtmlParser behavior

## External and Retained Components

- epdiy
  - Used by non-M5Paper e-paper environments
  - Retained for Paper S3 and original board support paths
- FreeType
  - Used by Paper S3 ESP-IDF environments
  - Local prebuilt/static library under `lib_freetype`
- FastEPD and other original-project libraries
  - Retained in the repository, but not part of the primary M5Paper build

## Current Primary Build Summary

For the maintained M5Paper target, the practical stack is:

- PlatformIO
- `espressif32@6.9.0`
- Arduino-ESP32 2.x
- C/C++
- M5GFX
- M5Unified
- SD card storage
- pugixml
- miniz
- PNG/JPEG decoding libraries
- Custom EPUB parsing, layout, rendering, and menu logic
