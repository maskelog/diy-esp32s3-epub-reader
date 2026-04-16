#include "AppSettings.h"
#include "Renderer/Renderer.h"
#include "EpubList/State.h"
#include <esp_log.h>

#ifdef ARDUINO
#include <SD.h>
#endif

static const char *TAG = "AppSettings";

// epub_list_state.use_grid_view is read/written by load/save; the instance
// lives in main.cpp.
extern EpubListState epub_list_state;

// ── Runtime variable definitions ──────────────────────────────────────────

bool             status_bar_visible       = true;
bool             open_last_book_on_startup = false;
bool             invert_tap_zones         = false;
bool             justify_paragraphs       = false;
SleepImageMode   sleep_image_mode         = SLEEP_IMAGE_CUSTOM;
IdleProfile      idle_profile             = IDLE_PROFILE_NORMAL;
MarginProfile    margin_profile           = MARGIN_PROFILE_NORMAL;
GestureSensitivity gesture_sensitivity   = GESTURE_SENS_MEDIUM;
LineSpacingProfile line_spacing_profile  = LINE_SPACING_100;
int64_t          idle_timeout_reading_us = 20LL * 60 * 1000 * 1000;
int64_t          idle_timeout_library_us = 5LL  * 60 * 1000 * 1000;

// ── Apply functions ───────────────────────────────────────────────────────

void apply_idle_profile()
{
  switch (idle_profile)
  {
  case IDLE_PROFILE_SHORT:
    idle_timeout_reading_us = 10LL * 60 * 1000 * 1000;
    idle_timeout_library_us = 2LL  * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_LONG:
    idle_timeout_reading_us = 40LL * 60 * 1000 * 1000;
    idle_timeout_library_us = 10LL * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_NORMAL:
  default:
    idle_timeout_reading_us = 20LL * 60 * 1000 * 1000;
    idle_timeout_library_us = 5LL  * 60 * 1000 * 1000;
    break;
  }
}

void apply_page_margins(Renderer *renderer)
{
  int left = 10, right = 10;
  switch (margin_profile)
  {
  case MARGIN_PROFILE_NARROW: left = 5;  right = 5;  break;
  case MARGIN_PROFILE_WIDE:   left = 20; right = 20; break;
  case MARGIN_PROFILE_NORMAL:
  default:                    left = 10; right = 10; break;
  }
  renderer->set_margin_top(37);
  renderer->set_margin_left(left);
  renderer->set_margin_right(right);
}

void apply_gesture_profile()
{
  // Reserved for future gesture sensitivity implementation.
}

void apply_line_spacing_profile(Renderer *renderer)
{
  int spacing_percent = 100;
  if (line_spacing_profile == LINE_SPACING_120)      spacing_percent = 120;
  else if (line_spacing_profile == LINE_SPACING_140) spacing_percent = 140;
  renderer->set_line_spacing_percent(spacing_percent);
}

// ── Persistent load / save ────────────────────────────────────────────────

static const char *app_settings_path = "/Books/settings.bin";

void load_app_settings(Renderer *renderer)
{
#ifdef ARDUINO
  File fp = SD.open(app_settings_path, FILE_READ);
  if (!fp)
  {
    ESP_LOGE(TAG, "Settings file not found: %s", app_settings_path);
    return;
  }
  AppSettings s = {};
  size_t bytes_read = fp.read((uint8_t *)&s, sizeof(s));
  fp.close();
  ESP_LOGE(TAG, "Settings read: %d bytes (expected %d), version=%d, flags=0x%02X",
           bytes_read, sizeof(s), s.version, s.flags);

  if (bytes_read < 4)
  {
    ESP_LOGE(TAG, "Settings file too small, ignoring");
    return;
  }
  if (s.version != 1)
  {
    ESP_LOGE(TAG, "Settings version mismatch: %d != 1", s.version);
    return;
  }

  ESP_LOGE(TAG, "Settings applied: grid_view=%d", (s.flags & 0x2) != 0);
  status_bar_visible            = (s.flags & 0x1) != 0;
  epub_list_state.use_grid_view = (s.flags & 0x2) != 0;
  open_last_book_on_startup     = (s.flags & 0x4) != 0;
  invert_tap_zones              = (s.flags & 0x8) != 0;

  uint8_t margin_bits = (s.flags >> 4) & 0x3;
  if (margin_bits <= MARGIN_PROFILE_WIDE)
    margin_profile = (MarginProfile)margin_bits;

  uint8_t idle_bits = (s.flags >> 6) & 0x3;
  if (idle_bits <= IDLE_PROFILE_LONG)
    idle_profile = (IdleProfile)idle_bits;

  ESP_LOGE(TAG, "Settings file sleep_mode=%d (max valid=%d)", s.sleep_mode, SLEEP_IMAGE_OFF);
  if (s.sleep_mode <= SLEEP_IMAGE_OFF)
  {
    sleep_image_mode = (SleepImageMode)s.sleep_mode;
    ESP_LOGE(TAG, "Sleep mode loaded: %d", (int)sleep_image_mode);
  }

#ifdef USE_FREETYPE
  if (s.reading_font_px > 0)
    renderer->set_reading_font_pixel_height(s.reading_font_px);
#endif

  gesture_sensitivity = (GestureSensitivity)(s.reserved & 0x3);
  justify_paragraphs  = (s.reserved & 0x4) != 0;
  uint8_t spacing_bits = (s.reserved >> 3) & 0x3;
  if (spacing_bits <= LINE_SPACING_140)
    line_spacing_profile = (LineSpacingProfile)spacing_bits;

  apply_idle_profile();
  apply_page_margins(renderer);
  apply_gesture_profile();
  apply_line_spacing_profile(renderer);
#endif // ARDUINO
}

void save_app_settings(Renderer *renderer)
{
#ifdef ARDUINO
  AppSettings s = {};
  s.version = 1;

  if (status_bar_visible)            s.flags |= 0x1;
  if (epub_list_state.use_grid_view) s.flags |= 0x2;
  if (open_last_book_on_startup)     s.flags |= 0x4;
  if (invert_tap_zones)              s.flags |= 0x8;
  s.flags |= (((uint8_t)margin_profile) & 0x3) << 4;
  s.flags |= (((uint8_t)idle_profile)   & 0x3) << 6;
  s.sleep_mode = (uint8_t)sleep_image_mode;

#ifdef USE_FREETYPE
  int px = renderer->get_reading_font_pixel_height();
  if (px > 0) s.reading_font_px = (int16_t)px;
#endif

  s.reserved = (uint8_t)gesture_sensitivity;
  if (justify_paragraphs)  s.reserved |= 0x4;
  s.reserved |= (((uint8_t)line_spacing_profile) & 0x3) << 3;

  File fp = SD.open(app_settings_path, FILE_WRITE);
  if (!fp)
  {
    ESP_LOGE(TAG, "Failed to open settings file for writing: %s", app_settings_path);
    return;
  }
  size_t written = fp.write((uint8_t *)&s, sizeof(s));
  fp.flush();
  fp.close();
  ESP_LOGE(TAG, "Settings saved: grid_view=%d, sleep_mode=%d, written=%d bytes",
           epub_list_state.use_grid_view, (int)sleep_image_mode, written);
#endif // ARDUINO
}
