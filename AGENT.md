# Agent Notes (diy-esp32s3-epub-reader)

## What Changed
- Added manual bookmark flow and persistence; bookmark indicator shown in status bar and menu.
- Adjusted sleep image handling to support SD `/Sleep/bg.png` and cover mode; added more logging.
- **Fixed EPUB cover sleep image rendering**:
  - Changed `show_sleep_cover()` to use M5.Display directly instead of renderer (matches CUSTOM mode behavior).
  - Added proper screen initialization (WHITE → BLACK) before rendering cover.
  - Implemented automatic image format detection (JPEG vs PNG).
  - Reduced task stack size from 96KB to 48KB to prevent allocation failures.
  - Added comprehensive debug logging to track image extraction and rendering.
- Reduced full-refresh usage for reading; full refresh remains available via menu/gesture.
- Updated EPUB loading to avoid stack overflows:
  - `Epub::load_with_task()` and `load_internal()` run on dedicated FreeRTOS tasks.
  - `EpubToc::load()` and `EpubReader::load()` use task-based loading.
  - `EpubReader::parse_and_layout_current_section()` and `render()` run on dedicated tasks.
- Improved Zip handling to reduce WDT hits:
  - `ZipFile` moved archive to heap.
  - Streamed extraction with callback and periodic yields.
  - Added `get_file_uncompressed_size()` helper.
- Image rendering improvements:
  - `ImageBlock` renders real images (with cache and size checks) and center alignment.
  - JPEG/PNG scaling paths improved; downscale uses averaging; upscaling uses simple bilinear.
  - M5Paper image output uses 1-bit ordered dithering (Bayer 8x8) only for images.
  - Grayscale conversion uses RGB565 mapping in `M5GfxRenderer::draw_pixel`.
- Library view cover rendering:
  - Cover metadata is stored in index.
  - Cover decode/render done in a separate task to avoid loopTask stack overflow.
  - Fallback to a 4x4 dot card when cover draw fails.
- README updated for M5Paper (separate commit already done).

## Known Issues / Not Fixed Yet
- **Sleep button shows settings/menu screen** when pressed (reported by user).
- Some build logs show `CONFIG_ARDUINO_LOOP_STACK_SIZE` redefinition warning; not resolved.
- Library cover rendering previously caused loopTask stack overflow. Task-based cover render is in place, needs verification on device.

## Files Touched (Key)
- `lib/Epub/EpubList/Epub.cpp`, `lib/Epub/EpubList/Epub.h`
- `lib/Epub/EpubList/EpubReader.cpp`
- `lib/Epub/EpubList/EpubToc.cpp`
- `lib/Epub/EpubList/EpubList.cpp`
- `lib/Epub/Renderer/JPEGHelper.cpp`, `lib/Epub/Renderer/JPEGHelper.h`
- `lib/Epub/Renderer/PNGHelper.cpp`, `lib/Epub/Renderer/PNGHelper.h`
- `lib/Epub/Renderer/M5GfxRenderer.cpp`, `lib/Epub/Renderer/M5GfxRenderer.h`
- `lib/Epub/Renderer/Renderer.h`
- `lib/Epub/RubbishHtmlParser/blocks/ImageBlock.h`
- `lib/Epub/RubbishHtmlParser/blocks/TextBlock.cpp`
- `lib/Epub/ZipFile/ZipFile.cpp`, `lib/Epub/ZipFile/ZipFile.h`
- `src/main.cpp`
- `platformio.ini`

## Notes
- Background images (e.g. `bg.png`) should NOT be committed per user request.
- Cover rendering in library is intentionally lightweight; avoid heavy per-item decode on loopTask.
