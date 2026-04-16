#pragma once
#include <stdint.h>

// ── Setting enums ──────────────────────────────────────────────────────────

typedef enum
{
  SLEEP_IMAGE_COVER  = 0,
  SLEEP_IMAGE_RANDOM = 1,
  SLEEP_IMAGE_CUSTOM = 2,
  SLEEP_IMAGE_OFF    = 3
} SleepImageMode;

typedef enum
{
  IDLE_PROFILE_SHORT  = 0,
  IDLE_PROFILE_NORMAL = 1,
  IDLE_PROFILE_LONG   = 2
} IdleProfile;

typedef enum
{
  MARGIN_PROFILE_NARROW = 0,
  MARGIN_PROFILE_NORMAL = 1,
  MARGIN_PROFILE_WIDE   = 2
} MarginProfile;

typedef enum
{
  GESTURE_SENS_LOW    = 0,
  GESTURE_SENS_MEDIUM = 1,
  GESTURE_SENS_HIGH   = 2
} GestureSensitivity;

typedef enum
{
  LINE_SPACING_100 = 0,
  LINE_SPACING_120 = 1,
  LINE_SPACING_140 = 2
} LineSpacingProfile;

// ── Persistent settings struct (binary serialised to /Books/settings.bin) ──
// WARNING: field layout is frozen — reordering breaks existing save files.

typedef struct
{
  uint8_t version;
  uint8_t flags;
  uint8_t sleep_mode;
  uint8_t reserved;
#ifdef USE_FREETYPE
  int16_t reading_font_px;
  int16_t padding;
#endif
} AppSettings;

// ── Runtime setting variables (defined in AppSettings.cpp) ─────────────────

extern bool             status_bar_visible;
extern bool             open_last_book_on_startup;
extern bool             invert_tap_zones;
extern bool             justify_paragraphs;
extern SleepImageMode   sleep_image_mode;
extern IdleProfile      idle_profile;
extern MarginProfile    margin_profile;
extern GestureSensitivity gesture_sensitivity;
extern LineSpacingProfile line_spacing_profile;
extern int64_t          idle_timeout_reading_us;
extern int64_t          idle_timeout_library_us;

// ── Functions (implemented in AppSettings.cpp) ─────────────────────────────

class Renderer;
void load_app_settings(Renderer *renderer);
void save_app_settings(Renderer *renderer);
void apply_idle_profile();
void apply_page_margins(Renderer *renderer);
void apply_gesture_profile();
void apply_line_spacing_profile(Renderer *renderer);
