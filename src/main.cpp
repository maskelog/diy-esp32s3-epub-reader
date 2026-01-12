#include <Arduino.h>
#include <unistd.h>
#include <esp_sleep.h>
#include <SD.h>
#include "config.h"
#include "EpubList/Epub.h"
#include "EpubList/EpubList.h"
#include "EpubList/EpubReader.h"
#include "EpubList/EpubToc.h"
#include <RubbishHtmlParser/RubbishHtmlParser.h>
#include "ZipFile/ZipFile.h"
#ifdef BOARD_TYPE_M5_PAPER
#include "Renderer/M5GfxRenderer.h"
#endif
#include "boards/Board.h"
#include "boards/controls/M5PaperButtonControls.h"
#include "boards/controls/M5PaperTouchControls.h"

#ifdef BOARD_TYPE_M5_PAPER
#include <M5Unified.h>
#endif

#ifdef USE_FREETYPE
#include "Renderer/FreeTypeFont.h"
#endif

#ifdef LOG_ENABLED
#define LOG_LEVEL ESP_LOG_INFO
#else
#define LOG_LEVEL ESP_LOG_NONE
#endif
#include <esp_log.h>
#include <esp_task_wdt.h>

const char *TAG = "main";

typedef enum
{
  SELECTING_EPUB,
  SELECTING_TABLE_CONTENTS,
  READING_EPUB,
  READING_MENU
} UIState;

UIState ui_state = SELECTING_EPUB;
EpubListState epub_list_state = {};
EpubTocState epub_index_state = {};

bool status_bar_visible = true;
bool open_last_book_on_startup = false; // DISABLED: Was causing infinite loop with problematic EPUBs
bool invert_tap_zones = false;
bool justify_paragraphs = false;

const int READER_MENU_BASIC_ITEMS = 7;
const int READER_MENU_ADVANCED_ITEMS = 12;

typedef enum
{
  SLEEP_IMAGE_COVER,
  SLEEP_IMAGE_RANDOM,
  SLEEP_IMAGE_CUSTOM, // Flash-embedded custom image
  SLEEP_IMAGE_OFF
} SleepImageMode;

SleepImageMode sleep_image_mode = SLEEP_IMAGE_CUSTOM; // Default to custom cat image

typedef enum
{
  IDLE_PROFILE_SHORT = 0,
  IDLE_PROFILE_NORMAL = 1,
  IDLE_PROFILE_LONG = 2
} IdleProfile;

typedef enum
{
  MARGIN_PROFILE_NARROW = 0,
  MARGIN_PROFILE_NORMAL = 1,
  MARGIN_PROFILE_WIDE = 2
} MarginProfile;

typedef enum
{
  GESTURE_SENS_LOW = 0,
  GESTURE_SENS_MEDIUM = 1,
  GESTURE_SENS_HIGH = 2
} GestureSensitivity;

typedef enum
{
  LINE_SPACING_100 = 0,
  LINE_SPACING_120 = 1,
  LINE_SPACING_140 = 2
} LineSpacingProfile;

IdleProfile idle_profile = IDLE_PROFILE_NORMAL;
MarginProfile margin_profile = MARGIN_PROFILE_NORMAL;
GestureSensitivity gesture_sensitivity = GESTURE_SENS_MEDIUM;
LineSpacingProfile line_spacing_profile = LINE_SPACING_100;

int64_t idle_timeout_reading_us = 60 * 1000 * 1000;
int64_t idle_timeout_library_us = 60 * 1000 * 1000;

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

static const char *app_settings_path = "/Books/settings.bin";
static const char *books_index_path = "/Books/BOOKS.IDX";

void handleEpub(Renderer *renderer, UIAction action);
void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw);
void handleReaderMenu(Renderer *renderer, UIAction action);
void handleUserInteraction(Renderer *renderer, UIAction ui_action, bool needs_redraw);
void draw_battery_level(Renderer *renderer, float voltage, float percentage);
static void show_library_loading(Renderer *renderer);
static void show_sleep_image(Renderer *renderer);
static int find_last_open_book_index();
static std::string normalise_path_for_zip(const std::string &path);
static void load_app_settings(Renderer *renderer);
static void save_app_settings(Renderer *renderer);
static void apply_idle_profile();
static void apply_page_margins(Renderer *renderer);
static void apply_gesture_profile();
static void apply_line_spacing_profile(Renderer *renderer);
static void renderReaderMenu(Renderer *renderer);
static void show_status_bar_toast(Renderer *renderer, const char *text);

static EpubList *epub_list = nullptr;
static EpubReader *reader = nullptr;
static EpubToc *contents = nullptr;
int reader_menu_selected = 0;
bool reader_menu_advanced = false;
static bool g_request_sleep_now = false;

Board *board = nullptr;
Renderer *renderer = nullptr;
Battery *battery = nullptr;
ButtonControls *button_controls = nullptr;
TouchControls *touch_controls = nullptr;
QueueHandle_t ui_queue = nullptr;

void setup()
{
  // Temporarily remove this task from watchdog during lengthy initialization
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  if (wdt_err != ESP_OK)
  {
    // If task is not subscribed, that's fine - just continue
    ESP_LOGW(TAG, "Failed to delete task from watchdog: %d", wdt_err);
  }

  board = Board::factory();
  board->power_up();

  ESP_LOGI(TAG, "Board powered up");

  renderer = board->get_renderer();
  ESP_LOGI(TAG, "Renderer created");

  board->start_filesystem();
  ESP_LOGI(TAG, "Filesystem started");

  load_app_settings(renderer);
  ESP_LOGI(TAG, "App settings loaded");

  battery = board->get_battery();
  if (battery)
  {
    battery->setup();
  }

  apply_page_margins(renderer);

  ui_queue = xQueueCreate(10, sizeof(UIAction));

  button_controls = board->get_button_controls(ui_queue);
  touch_controls = board->get_touch_controls(renderer, ui_queue);

  if (button_controls->did_wake_from_deep_sleep())
  {
    bool hydrate_success = renderer->hydrate();
    UIAction ui_action = button_controls->get_deep_sleep_action();
    handleUserInteraction(renderer, ui_action, !hydrate_success);
  }
  else
  {
    ESP_LOGE(TAG, ">>> Normal boot - loading library");
    renderer->reset();
    show_library_loading(renderer);
    ESP_LOGE(TAG, ">>> Library loading screen displayed");
    if (!epub_list)
    {
      ESP_LOGE(TAG, ">>> Creating EPUB list");
      epub_list = new EpubList(renderer, epub_list_state);
      ESP_LOGE(TAG, ">>> Loading EPUBs from /Books");
      epub_list->load("/Books");
      ESP_LOGE(TAG, ">>> EPUB list loaded, count=%d", epub_list_state.num_epubs);
    }
    if (open_last_book_on_startup)
    {
      int last_book_index = find_last_open_book_index();
      ESP_LOGE(TAG, ">>> open_last_book_on_startup: last_book_index=%d", last_book_index);
      if (last_book_index >= 0)
      {
        epub_list_state.selected_item = last_book_index;
        ui_state = READING_EPUB;
      }
    }
    ESP_LOGE(TAG, ">>> Calling handleUserInteraction from setup");
    handleUserInteraction(renderer, NONE, true);
    ESP_LOGE(TAG, ">>> handleUserInteraction returned in setup");
  }

  ESP_LOGE(TAG, ">>> Drawing battery level");
  if (battery)
  {
    draw_battery_level(renderer, battery->get_voltage(), battery->get_percentage());
  }
  ESP_LOGE(TAG, ">>> touch_controls->render");
  touch_controls->render(renderer);
  ESP_LOGE(TAG, ">>> renderer->flush_display() START");
  renderer->flush_display();
  ESP_LOGE(TAG, ">>> renderer->flush_display() END");

  // Re-add this task to watchdog after initialization is complete
  ESP_LOGE(TAG, ">>> Re-adding watchdog");
  esp_err_t wdt_add_err = esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  if (wdt_add_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to re-add task to watchdog: %d", wdt_add_err);
  }

  ESP_LOGE(TAG, ">>> Setup complete!");
}

static int loop_count = 0;

void loop()
{
  // Feed the watchdog to prevent timeout during normal operation
  esp_task_wdt_reset();

  // Check if sleep was requested from the menu
  if (g_request_sleep_now)
  {
    ESP_LOGE(TAG, ">>> Sleep requested, going to deep sleep");
    g_request_sleep_now = false;

    // Show sleep image if enabled
    show_sleep_image(renderer);

    // Prepare board for sleep
    board->prepare_to_sleep();

    // Setup wakeup source (button press)
    button_controls->setup_deep_sleep();

    ESP_LOGE(TAG, ">>> Entering deep sleep now");

    // Enter deep sleep
    esp_deep_sleep_start();

    // Should not reach here
    return;
  }

  loop_count++;

  button_controls->run();
  touch_controls->run();

  UIAction ui_action = NONE;
  if (xQueueReceive(ui_queue, &ui_action, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    if (ui_action != NONE)
    {
      handleUserInteraction(renderer, ui_action, false);
      if (battery)
      {
        draw_battery_level(renderer, battery->get_voltage(), battery->get_percentage());
      }
      renderer->flush_display();
    }
  }
}
// All the other functions from the original main.cpp go here
static int find_last_open_book_index()
{
  int last_index = -1;
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    EpubListItem &item = epub_list_state.epub_list[i];
    if (item.current_section != 0 || item.current_page != 0)
    {
      if (last_index < 0)
      {
        last_index = i;
      }
      else
      {
        EpubListItem &best = epub_list_state.epub_list[last_index];
        if (item.current_section > best.current_section ||
            (item.current_section == best.current_section && item.current_page > best.current_page))
        {
          last_index = i;
        }
      }
    }
  }
  if (last_index >= 0)
  {
    return last_index;
  }

  // Fallback: no book has recorded section/page progress yet. This can
  // happen if the user opened a brand-new book and went to sleep on the
  // very first page (section 0, page 0). In that case, treat any book
  // that has been laid out at least once (pages_in_current_section > 0)
  // as a candidate and pick the first such entry.
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    EpubListItem &item = epub_list_state.epub_list[i];
    if (item.pages_in_current_section > 0)
    {
      return i;
    }
  }

  return -1;
}


void handleEpub(Renderer *renderer, UIAction action)
{
  ESP_LOGE(TAG, ">>> handleEpub START: action=%d", action);

  // Remove from watchdog during EPUB operations (loading/rendering can be slow)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  if (!reader)
  {
    ESP_LOGE(TAG, ">>> Creating EpubReader for: %s", epub_list_state.epub_list[epub_list_state.selected_item].path);
    vTaskDelay(10);

    EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
    if (item.bookmark_set)
    {
      item.current_section = item.bookmark_section;
      item.current_page = item.bookmark_page;
    }
    reader = new EpubReader(item, renderer);
    reader->set_justified(justify_paragraphs);

    ESP_LOGE(TAG, ">>> Loading EPUB via EpubReader");
    vTaskDelay(10);

    if (!reader->load())
    {
      ESP_LOGE(TAG, ">>> EpubReader::load() FAILED!");
      delete reader;
      reader = nullptr;
      if (was_subscribed)
      {
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      }
      return;
    }

    ESP_LOGE(TAG, ">>> EpubReader::load() SUCCESS");
    vTaskDelay(10);

    // Clear screen before rendering new book content
    renderer->clear_screen();
  }
  switch (action)
  {
  case UP:
    reader->prev();
    break;
  case DOWN:
    reader->next();
    break;
  case PREV_SECTION:
    reader->prev_section();
    break;
  case NEXT_SECTION:
    reader->next_section();
    break;
  case REFRESH_PAGE:
    // Force a full-screen refresh of the current reading page to
    // mitigate ghosting. This mirrors the "[R] Refresh screen"
    // reader-menu action but is triggered via a gesture.
    renderer->reset();
    break;
  case SELECT:
    // switch back to main screen
    ui_state = SELECTING_EPUB;
    renderer->clear_screen();
    // clear the epub reader away
    delete reader;
    reader = nullptr;
    // force a redraw
    if (!epub_list)
    {
      epub_list = new EpubList(renderer, epub_list_state);
    }
    handleEpubList(renderer, NONE, true);
    // Re-add to watchdog before returning
    if (was_subscribed)
    {
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
    return;
  case NONE:
  default:
    break;
  }

  ESP_LOGE(TAG, ">>> Rendering page via EpubReader");
  vTaskDelay(10);

  reader->render();

  ESP_LOGE(TAG, ">>> Page rendering complete");
  vTaskDelay(10);


  // Re-add to watchdog after EPUB operations complete
  if (was_subscribed)
  {
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  }
  ESP_LOGE(TAG, "<<< handleEpub END");
}

void handleEpubTableContents(Renderer *renderer, UIAction action, bool needs_redraw)
{
  // Remove from watchdog during TOC operations (loading can be slow)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  if (!contents)
  {
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    contents->set_needs_redraw();
    contents->load();
  }
  switch (action)
  {
  case UP:
    contents->prev();
    break;
  case DOWN:
    contents->next();
    break;
  case SELECT:
    // setup the reader state
    ui_state = READING_EPUB;
    // Replace any existing reader so we don't leak or reuse stale
    // parser state when jumping via the TOC.
    if (reader)
    {
      delete reader;
      reader = nullptr;
    }
    reader = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->set_justified(justify_paragraphs);
    reader->set_state_section(contents->get_selected_toc());
    if (!reader->load())
    {
      ESP_LOGE(TAG, "Failed to load EPUB when opening from TOC selection");
      // Stay in the TOC view; the user can back out to the library or
      // try another entry.
      delete reader;
      reader = nullptr;
      ui_state = SELECTING_TABLE_CONTENTS;
      // Re-add to watchdog before returning
      if (was_subscribed)
      {
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      }
      return;
    }
    // switch to reading the epub
    delete contents;
    contents = nullptr;
    // Re-add to watchdog before calling handleEpub (which manages its own watchdog)
    if (was_subscribed)
    {
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
    handleEpub(renderer, NONE);
    return;
  case NONE:
  default:
    break;
  }
  contents->render();

  // Re-add to watchdog after TOC operations complete
  if (was_subscribed)
  {
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  }
}

static void renderReaderMenu(Renderer *renderer)
{
  const int max_items = 13;
  const char *labels[max_items];
  int items_total = 0;

  char buf_status[32];
  char buf_view[32];
  char buf_startup[40];
  char buf_sleep[40];
  char buf_font[32];
  char buf_align[32];
  char buf_tap[32];
  char buf_idle[32];
  char buf_margin[32];
  char buf_gest[32];
  char buf_spacing[32];

  if (!reader_menu_advanced)
  {
    items_total = READER_MENU_BASIC_ITEMS;
    labels[0] = "Return to book";
    labels[1] = "Bookmark";
    labels[2] = "Table of contents";
    labels[3] = "Back to library";
    labels[4] = "More";
    // Use ASCII-friendly "icon" prefixes so they render on limited fonts.
    labels[5] = "[R] Refresh screen";
    labels[6] = "[Zz] Sleep";
  }
  else
  {
    items_total = READER_MENU_ADVANCED_ITEMS;

    snprintf(buf_status, sizeof(buf_status), "Status bar: %s", status_bar_visible ? "ON" : "OFF");
    labels[0] = buf_status;

    snprintf(buf_view, sizeof(buf_view), "Library view: %s", epub_list_state.use_grid_view ? "Grid" : "List");
    labels[1] = buf_view;

    snprintf(buf_startup, sizeof(buf_startup), "Startup: %s", open_last_book_on_startup ? "Last book" : "Library");
    labels[2] = buf_startup;

    const char *sleep_mode_str = "Cover";
    if (sleep_image_mode == SLEEP_IMAGE_RANDOM)
    {
      sleep_mode_str = "Random";
    }
    else if (sleep_image_mode == SLEEP_IMAGE_CUSTOM)
    {
      sleep_mode_str = "Custom";
    }
    else if (sleep_image_mode == SLEEP_IMAGE_OFF)
    {
      sleep_mode_str = "Off";
    }
    snprintf(buf_sleep, sizeof(buf_sleep), "Sleep image: %s", sleep_mode_str);
    labels[3] = buf_sleep;

#ifdef USE_FREETYPE
    int px = renderer->get_reading_font_pixel_height();
    const char *font_label = "Medium";
    if (px <= 18)
    {
      font_label = "Small";
    }
    else if (px >= 26)
    {
      font_label = "Large";
    }
    snprintf(buf_font, sizeof(buf_font), "Font size: %s", font_label);
#else
    snprintf(buf_font, sizeof(buf_font), "Font size");
#endif
    labels[4] = buf_font;

    snprintf(buf_align, sizeof(buf_align), "Alignment: %s", justify_paragraphs ? "Justified" : "Left");
    labels[5] = buf_align;

    snprintf(buf_tap, sizeof(buf_tap), "Tap zones: %s", invert_tap_zones ? "Inverted" : "Normal");
    labels[6] = buf_tap;

    const char *idle_str = "Normal";
    if (idle_profile == IDLE_PROFILE_SHORT)
    {
      idle_str = "Short";
    }
    else if (idle_profile == IDLE_PROFILE_LONG)
    {
      idle_str = "Long";
    }
    snprintf(buf_idle, sizeof(buf_idle), "Idle: %s", idle_str);
    labels[7] = buf_idle;

    const char *margin_str = "Normal";
    if (margin_profile == MARGIN_PROFILE_NARROW)
    {
      margin_str = "Narrow";
    }
    else if (margin_profile == MARGIN_PROFILE_WIDE)
    {
      margin_str = "Wide";
    }
    snprintf(buf_margin, sizeof(buf_margin), "Margins: %s", margin_str);
    labels[8] = buf_margin;

    const char *gest_str = "Medium";
    if (gesture_sensitivity == GESTURE_SENS_LOW)
    {
      gest_str = "Low";
    }
    else if (gesture_sensitivity == GESTURE_SENS_HIGH)
    {
      gest_str = "High";
    }
    snprintf(buf_gest, sizeof(buf_gest), "Gestures: %s", gest_str);
    labels[9] = buf_gest;
    int spacing_percent = 100;
    if (line_spacing_profile == LINE_SPACING_120)
    {
      spacing_percent = 120;
    }
    else if (line_spacing_profile == LINE_SPACING_140)
    {
      spacing_percent = 140;
    }
    snprintf(buf_spacing, sizeof(buf_spacing), "Line spacing: %d%%", spacing_percent);
    labels[10] = buf_spacing;
    labels[11] = "Save & Back";
  }

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(false);
#endif

  renderer->clear_screen();
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (line_height <= 0)
  {
    line_height = 20;
  }
  if (page_height <= 0)
  {
    page_height = line_height * items_total * 2;
  }

  if (page_width <= 0)
  {
    page_width = 400;
  }

  if (items_total <= 0)
  {
#ifdef USE_FREETYPE
    renderer->set_freetype_enabled(true);
#endif
    return;
  }

  int items_per_page = EPUB_TOC_ITEMS_PER_PAGE;
  if (items_per_page <= 0 || items_per_page > items_total)
  {
    items_per_page = items_total;
  }

  if (reader_menu_selected < 0)
  {
    reader_menu_selected = 0;
  }
  if (reader_menu_selected >= items_total)
  {
    reader_menu_selected = items_total - 1;
  }

  int total_pages = (items_total + items_per_page - 1) / items_per_page;
  if (total_pages < 1)
  {
    total_pages = 1;
  }

  int current_page = reader_menu_selected / items_per_page;
  if (current_page < 0)
  {
    current_page = 0;
  }
  if (current_page >= total_pages)
  {
    current_page = total_pages - 1;
  }

  int start_index = current_page * items_per_page;
  int end_index = start_index + items_per_page;
  if (end_index > items_total)
  {
    end_index = items_total;
  }
  int visible_count = end_index - start_index;

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int content_height = page_height;
  if (bottom_bar_height > 0 && bottom_bar_height < page_height)
  {
    content_height = page_height - bottom_bar_height;
  }

  int button_vertical_padding = line_height / 4;
  if (button_vertical_padding < 2)
  {
    button_vertical_padding = 2;
  }
  int button_height = line_height + button_vertical_padding * 2;
  int button_spacing = line_height / 4;
  if (button_spacing < 2)
  {
    button_spacing = 2;
  }

  int max_label_width = 0;
  for (int i = 0; i < items_total; i++)
  {
    int w = renderer->get_text_width(labels[i], false, false);
    if (w > max_label_width)
    {
      max_label_width = w;
    }
  }
  int horizontal_padding = 30;
  int button_width = max_label_width + horizontal_padding * 2;
  if (button_width > page_width - 40)
  {
    button_width = page_width - 40;
  }

  int container_width = button_width;
  int container_height = visible_count * button_height + (visible_count - 1) * button_spacing;
  int container_x = (page_width - container_width) / 2;
  if (container_x < 0)
  {
    container_x = 0;
  }
  int container_y = (content_height - container_height) / 2;
  if (container_y < 0)
  {
    container_y = 0;
  }

  int ypos = container_y;
  for (int i = 0; i < visible_count; i++)
  {
    int item_index = start_index + i;
    const char *label = labels[item_index];

    renderer->fill_rect(container_x, ypos, container_width, button_height, 255);
    renderer->draw_rect(container_x, ypos, container_width, button_height, 0);

    if (item_index == reader_menu_selected)
    {
      for (int line = 0; line < 3; line++)
      {
        renderer->draw_rect(
            container_x + line,
            ypos + line,
            container_width - 2 * line,
            button_height - 2 * line,
            0);
      }
    }

    int label_width = renderer->get_text_width(label, false, false);
    if (label_width < 0)
    {
      label_width = 0;
    }
    int text_x = container_x + (container_width - label_width) / 2;
    int center_y = ypos + (button_height / 2);
    int text_y = center_y - (3 * line_height) / 4;
    renderer->draw_text(text_x, text_y, label, false, false);

    ypos += button_height + button_spacing;
  }

  if (bottom_bar_height > 0 && bottom_bar_height <= page_height)
  {
    int bar_y = page_height - bottom_bar_height;
    renderer->fill_rect(0, bar_y, page_width, bottom_bar_height, 255);
    int center_x = page_width / 2;
    int center_y = bar_y + bottom_bar_height / 2;

    int page_display = current_page + 1;
    if (page_display < 1)
    {
      page_display = 1;
    }
    if (page_display > total_pages)
    {
      page_display = total_pages;
    }

    const char *left_double = "<<";
    const char *left_single = "<";
    const char *right_single = ">";
    const char *right_double = ">>";

    char center[32];
    snprintf(center, sizeof(center), "%d / %d", page_display, total_pages);

    int w_ld = renderer->get_text_width(left_double, true, false);
    int w_ls = renderer->get_text_width(left_single, true, false);
    int w_center = renderer->get_text_width(center, false, false);
    int w_rs = renderer->get_text_width(right_single, true, false);
    int w_rd = renderer->get_text_width(right_double, true, false);
    if (w_ld < 0)
      w_ld = 0;
    if (w_ls < 0)
      w_ls = 0;
    if (w_center < 0)
      w_center = 0;
    if (w_rs < 0)
      w_rs = 0;
    if (w_rd < 0)
      w_rd = 0;

    int line_h = renderer->get_line_height();
    if (line_h <= 0)
    {
      line_h = 20;
    }
    int label_y = center_y - (3 * line_h) / 4;

    int columns = 5;
    int col_width = page_width / columns;
    if (col_width <= 0)
    {
      col_width = 1;
    }

    int ld_zone_start = 0;
    int ld_zone_end = ld_zone_start + col_width;

    int ls_zone_start = ld_zone_end;
    int ls_zone_end = ls_zone_start + col_width;

    int center_zone_start = ls_zone_end;
    int center_zone_end = center_zone_start + col_width;

    int rs_zone_start = center_zone_end;
    int rs_zone_end = rs_zone_start + col_width;

    int rd_zone_start = rs_zone_end;
    int rd_zone_end = page_width;

    int bar_top = bar_y;
    int bar_bottom = bar_y + bottom_bar_height;
    int bar_height = bar_bottom - bar_top;
    if (bar_height < renderer->get_line_height() + 4)
    {
      bar_height = renderer->get_line_height() + 4;
    }

    int box_y = bar_top + 2;
    int box_h = bar_height - 4;
    if (box_h <= 0)
    {
      box_h = bar_height;
    }

    renderer->draw_rect(ld_zone_start, box_y, ld_zone_end - ld_zone_start, box_h, 0);
    renderer->draw_rect(ls_zone_start, box_y, ls_zone_end - ls_zone_start, box_h, 0);
    renderer->draw_rect(center_zone_start, box_y, center_zone_end - center_zone_start, box_h, 0);
    renderer->draw_rect(rs_zone_start, box_y, rs_zone_end - rs_zone_start, box_h, 0);
    renderer->draw_rect(rd_zone_start, box_y, rd_zone_end - rd_zone_start, box_h, 0);

    auto center_label_x = [](int zone_start, int zone_end, int text_width)
    {
      int w = zone_end - zone_start;
      int x = zone_start + (w - text_width) / 2;
      if (x < zone_start)
      {
        x = zone_start;
      }
      return x;
    };

    int x_ld = center_label_x(ld_zone_start, ld_zone_end, w_ld);
    int x_ls = center_label_x(ls_zone_start, ls_zone_end, w_ls);
    int x_center = center_label_x(center_zone_start, center_zone_end, w_center);
    int x_rs = center_label_x(rs_zone_start, rs_zone_end, w_rs);
    int x_rd = center_label_x(rd_zone_start, rd_zone_end, w_rd);

    renderer->draw_text(x_ld, label_y, left_double, true, false);
    renderer->draw_text(x_ls, label_y, left_single, true, false);
    renderer->draw_text(x_center, label_y, center, false, false);
    renderer->draw_text(x_rs, label_y, right_single, true, false);
    renderer->draw_text(x_rd, label_y, right_double, true, false);
  }

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(true);
#endif
}

static void show_status_bar_toast(Renderer *renderer, const char *text)
{
  if (!text)
  {
    return;
  }

  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0)
  {
    return;
  }

  int padding = 4;
  int box_height = line_height + padding * 2;
  int y = page_height - box_height - 2;
  if (y < 0)
  {
    y = 0;
  }

  // Clear a small strip at the bottom and draw the toast text.
  renderer->fill_rect(0, y, page_width, box_height, 255);
  renderer->draw_rect(0, y, page_width, box_height, 0);
  renderer->draw_text(5, y + padding + line_height / 2, text, false, false);
}

static void load_app_settings(Renderer *renderer)
{
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

  if (bytes_read < 4) // At minimum need version + flags + sleep_mode + reserved
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
  status_bar_visible = (s.flags & 0x1) != 0;
  epub_list_state.use_grid_view = (s.flags & 0x2) != 0;
  open_last_book_on_startup = (s.flags & 0x4) != 0;
  invert_tap_zones = (s.flags & 0x8) != 0;
  uint8_t margin_bits = (s.flags >> 4) & 0x3;
  if (margin_bits <= MARGIN_PROFILE_WIDE)
  {
    margin_profile = (MarginProfile)margin_bits;
  }
  uint8_t idle_bits = (s.flags >> 6) & 0x3;
  if (idle_bits <= IDLE_PROFILE_LONG)
  {
    idle_profile = (IdleProfile)idle_bits;
  }
  ESP_LOGE(TAG, "Settings file sleep_mode=%d (max valid=%d)", s.sleep_mode, SLEEP_IMAGE_OFF);
  if (s.sleep_mode <= SLEEP_IMAGE_OFF)
  {
    sleep_image_mode = (SleepImageMode)s.sleep_mode;
    ESP_LOGE(TAG, "Sleep mode loaded: %d", (int)sleep_image_mode);
  }
#ifdef USE_FREETYPE
  if (s.reading_font_px > 0)
  {
    renderer->set_reading_font_pixel_height(s.reading_font_px);
  }
#endif
  gesture_sensitivity = (GestureSensitivity)(s.reserved & 0x3);
  // Bit 2 of reserved stores the paragraph alignment preference.
  justify_paragraphs = (s.reserved & 0x4) != 0;
  uint8_t spacing_bits = (s.reserved >> 3) & 0x3;
  if (spacing_bits <= LINE_SPACING_140)
  {
    line_spacing_profile = (LineSpacingProfile)spacing_bits;
  }
  apply_idle_profile();
  apply_page_margins(renderer);
  apply_gesture_profile();
  apply_line_spacing_profile(renderer);
}

static void save_app_settings(Renderer *renderer)
{
  AppSettings s = {};
  s.version = 1;
  if (status_bar_visible)
  {
    s.flags |= 0x1;
  }
  if (epub_list_state.use_grid_view)
  {
    s.flags |= 0x2;
  }
  if (open_last_book_on_startup)
  {
    s.flags |= 0x4;
  }
  if (invert_tap_zones)
  {
    s.flags |= 0x8;
  }
  s.flags |= (((uint8_t)margin_profile) & 0x3) << 4;
  s.flags |= (((uint8_t)idle_profile) & 0x3) << 6;
  s.sleep_mode = (uint8_t)sleep_image_mode;
#ifdef USE_FREETYPE
  int px = renderer->get_reading_font_pixel_height();
  if (px > 0)
  {
    s.reading_font_px = (int16_t)px;
  }
#endif
  s.reserved = (uint8_t)gesture_sensitivity;
  if (justify_paragraphs)
  {
    s.reserved |= 0x4;
  }
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
}

static void apply_idle_profile()
{
  switch (idle_profile)
  {
  case IDLE_PROFILE_SHORT:
    idle_timeout_reading_us = 10 * 60 * 1000 * 1000;
    idle_timeout_library_us = 2 * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_LONG:
    idle_timeout_reading_us = 40LL * 60 * 1000 * 1000;
    idle_timeout_library_us = 10LL * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_NORMAL:
  default:
    idle_timeout_reading_us = 20 * 60 * 1000 * 1000;
    idle_timeout_library_us = 5 * 60 * 1000 * 1000;
    break;
  }
}

static void apply_page_margins(Renderer *renderer)
{
  int left = 10;
  int right = 10;
  switch (margin_profile)
  {
  case MARGIN_PROFILE_NARROW:
    left = 5;
    right = 5;
    break;
  case MARGIN_PROFILE_WIDE:
    left = 20;
    right = 20;
    break;
  case MARGIN_PROFILE_NORMAL:
  default:
    left = 10;
    right = 10;
    break;
  }
  renderer->set_margin_top(37);
  renderer->set_margin_left(left);
  renderer->set_margin_right(right);
}

static void apply_gesture_profile()
{
}

static void apply_line_spacing_profile(Renderer *renderer)
{
  int spacing_percent = 100;
  if (line_spacing_profile == LINE_SPACING_120)
  {
    spacing_percent = 120;
  }
  else if (line_spacing_profile == LINE_SPACING_140)
  {
    spacing_percent = 140;
  }
  renderer->set_line_spacing_percent(spacing_percent);
}

void handleUserInteraction(Renderer *renderer, UIAction ui_action, bool needs_redraw)
{
  // Remove from watchdog for entire user interaction handling
  // This is the top-level handler that calls all other handlers
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);
  ESP_LOGE(TAG, ">>> handleUserInteraction START: action=%d, redraw=%d, wdt_removed=%d", ui_action, needs_redraw, was_subscribed);

  // Global handling for status bar toggle while reading.
  if (ui_action == TOGGLE_STATUS_BAR && ui_state == READING_EPUB)
  {
    status_bar_visible = !status_bar_visible;
    save_app_settings(renderer);
    // Re-render the current page; draw_battery_level() will
    // pick up the new visibility on the next flush.
    handleEpub(renderer, NONE);
    show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  // From the library view, allow a gesture (e.g. two-finger swipe up)
  // to open the reader menu directly, focusing on advanced settings.
  if (ui_action == OPEN_READER_MENU && ui_state == SELECTING_EPUB)
  {
    ui_state = READING_MENU;
    reader_menu_advanced = true;
    reader_menu_selected = 0;
    renderReaderMenu(renderer);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  switch (ui_state)
  {
  case READING_MENU:
    handleReaderMenu(renderer, ui_action);
    break;
  case READING_EPUB:
    if (ui_action == SELECT)
    {
      ui_state = READING_MENU;
      reader_menu_selected = 0;
      renderReaderMenu(renderer);
    }
    else
    {
      handleEpub(renderer, ui_action);
    }
    break;
  case SELECTING_TABLE_CONTENTS:
    handleEpubTableContents(renderer, ui_action, needs_redraw);
    break;
  case SELECTING_EPUB:
  default:
    handleEpubList(renderer, ui_action, needs_redraw);
    break;
  }

  // Re-add to watchdog after user interaction handling completes
  ESP_LOGE(TAG, "<<< handleUserInteraction END: re-adding wdt=%d", was_subscribed);
  if (was_subscribed)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
}

void draw_battery_level(Renderer *renderer, float voltage, float percentage)
{
  // If the status bar is hidden, restore full-page content by
  // removing the reserved top margin and skip drawing any
  // status elements.
  if (!status_bar_visible)
  {
    renderer->set_margin_top(0);
    return;
  }

  // clear the margin so we can draw the battery in the right place
  renderer->set_margin_top(0);

  // Draw page number on the left side when reading
  if (ui_state == READING_EPUB && epub_list_state.selected_item >= 0 &&
      epub_list_state.selected_item < epub_list_state.num_epubs)
  {
    EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
    if (item.pages_in_current_section > 0)
    {
      char page_str[32];
      if (item.bookmark_set)
      {
        snprintf(page_str, sizeof(page_str), "S%d  %d/%d [B]",
                 item.current_section + 1,
                 item.current_page + 1,
                 item.pages_in_current_section);
      }
      else
      {
        snprintf(page_str, sizeof(page_str), "S%d  %d/%d",
                 item.current_section + 1,
                 item.current_page + 1,
                 item.pages_in_current_section);
      }
      renderer->draw_text(10, 10, page_str, false, false);
    }
  }

  // Draw battery on the right side
  int width = 40;
  int height = 20;
  int margin_right = 5;
  int margin_top = 10;
  int xpos = renderer->get_page_width() - width - margin_right;
  int ypos = margin_top;
  int percent_width = width * percentage / 100;
  renderer->fill_rect(xpos, ypos, width, height, 255);
  renderer->fill_rect(xpos + width - percent_width, ypos, percent_width, height, 0);
  renderer->draw_rect(xpos, ypos, width, height, 0);
  renderer->fill_rect(xpos - 4, ypos + height / 4, 4, height / 2, 0);
  // put the margin back
  renderer->set_margin_top(37);
}

static void show_library_loading(Renderer *renderer)
{
  renderer->clear_screen();
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0)
  {
    return;
  }

  const char *msg = "Book library is loading";
  int text_width = renderer->get_text_width(msg, false, false);
  if (text_width < 0)
  {
    text_width = 0;
  }

  int x = (page_width - text_width) / 2;
  if (x < 0)
  {
    x = 0;
  }
  int center_y = page_height / 2;
  int y = center_y - (3 * line_height) / 4;

  renderer->draw_text(x, y, msg, false, false);
  renderer->flush_display();
}

static std::string normalise_path_for_zip(const std::string &path)
{
  std::vector<std::string> components;
  std::string component;
  for (char c : path)
  {
    if (c == '/')
    {
      if (!component.empty())
      {
        if (component == "..")
        {
          if (!components.empty())
          {
            components.pop_back();
          }
        }
        else if (component != ".")
        {
          components.push_back(component);
        }
        component.clear();
      }
    }
    else
    {
      component += c;
    }
  }
  if (!component.empty())
  {
    if (component != "." && component != "..")
    {
      components.push_back(component);
    }
  }
  std::string result;
  for (size_t i = 0; i < components.size(); ++i)
  {
    if (i > 0)
    {
      result += "/";
    }
    result += components[i];
  }
  return result;
}

static void show_sleep_cover(Renderer *renderer)
{
  // Remove from watchdog during sleep cover display (EPUB/image loading can be slow)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  int book_index = -1;
  if (epub_list_state.num_epubs > 0)
  {
    if (ui_state == READING_EPUB || ui_state == READING_MENU || ui_state == SELECTING_TABLE_CONTENTS)
    {
      book_index = epub_list_state.selected_item;
    }
    if (book_index < 0)
    {
      book_index = find_last_open_book_index();
    }
  }

  if (book_index < 0)
  {
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  EpubListItem &item = epub_list_state.epub_list[book_index];
  if (item.cover_path[0] == '\0')
  {
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }
#ifndef UNIT_TEST
  ESP_LOGE(TAG, "Sleep cover path: %s", item.cover_path);
#endif

#ifndef UNIT_TEST
  struct SleepCoverContext
  {
    std::string epub_path;
    std::string cover_path;
    uint8_t *data;
    size_t size;
    bool ok;
    SemaphoreHandle_t done;
  };
  SleepCoverContext ctx = {};
  ctx.epub_path = item.path;
  ctx.cover_path = normalise_path_for_zip(item.cover_path);
  ctx.data = nullptr;
  ctx.size = 0;
  ctx.ok = false;
  ctx.done = xSemaphoreCreateBinary();
  if (!ctx.done)
  {
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  auto task_fn = [](void *param) {
    SleepCoverContext *ctx = static_cast<SleepCoverContext *>(param);
    ESP_LOGE(TAG, "Sleep cover task: opening EPUB: %s", ctx->epub_path.c_str());
    ZipFile zip(ctx->epub_path.c_str());
    ESP_LOGE(TAG, "Sleep cover task: looking for: %s", ctx->cover_path.c_str());
    size_t cover_size = 0;
    bool size_ok = zip.get_file_uncompressed_size(ctx->cover_path.c_str(), &cover_size);
    ESP_LOGE(TAG, "Sleep cover task: get_file_uncompressed_size=%d, size=%zu", size_ok ? 1 : 0, cover_size);
    if (size_ok && cover_size > 0 && cover_size <= (600 * 1024))
    {
      ESP_LOGE(TAG, "Sleep cover task: reading file to memory");
      size_t image_data_size = 0;
      uint8_t *image_data = zip.read_file_to_memory(ctx->cover_path.c_str(), &image_data_size);
      ESP_LOGE(TAG, "Sleep cover task: read_file_to_memory returned, data=%p, size=%zu", image_data, image_data_size);
      if (image_data && image_data_size > 0)
      {
        ctx->data = image_data;
        ctx->size = image_data_size;
        ctx->ok = true;
        ESP_LOGE(TAG, "Sleep cover task: SUCCESS");
      }
      else
      {
        ESP_LOGE(TAG, "Sleep cover task: FAILED to read image data");
      }
    }
    else
    {
      ESP_LOGE(TAG, "Sleep cover task: FAILED size check (size_ok=%d, size=%zu, limit=600KB)",
               size_ok ? 1 : 0, cover_size);
    }
    xSemaphoreGive(ctx->done);
    vTaskDelete(nullptr);
  };

  // Reduce stack size from 96KB to 48KB to avoid allocation failure
  const uint32_t stack_words = static_cast<uint32_t>((48 * 1024) / sizeof(StackType_t));
  ESP_LOGE(TAG, "Sleep cover: creating task with stack=%u words (%u bytes)", stack_words, stack_words * sizeof(StackType_t));
  BaseType_t ok = xTaskCreatePinnedToCore(task_fn, "sleep_cover", stack_words, &ctx, 2, nullptr, 1);
  if (ok != pdPASS)
  {
    ESP_LOGE(TAG, "Sleep cover: FAILED to create task (error=%d)", ok);
    vSemaphoreDelete(ctx.done);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  ESP_LOGE(TAG, "Sleep cover: task created, waiting for completion");
  xSemaphoreTake(ctx.done, portMAX_DELAY);
  ESP_LOGE(TAG, "Sleep cover: task completed");
  vSemaphoreDelete(ctx.done);
  if (ctx.ok && ctx.data && ctx.size > 0)
  {
    ESP_LOGE(TAG, "Sleep cover bytes: %zu", ctx.size);

#ifdef BOARD_TYPE_M5_PAPER
    // Use M5.Display directly like CUSTOM mode for better rendering
    // Do a full quality clear to reduce ghosting before the sleep image
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.display();
    M5.Display.waitDisplay();

    // Pre-darken to push the panel toward solid black for better contrast
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.display();
    M5.Display.waitDisplay();

    // Detect image format from first few bytes
    bool is_jpeg = (ctx.size > 2 && ctx.data[0] == 0xFF && ctx.data[1] == 0xD8);
    bool is_png = (ctx.size > 8 && ctx.data[0] == 0x89 && ctx.data[1] == 0x50 &&
                   ctx.data[2] == 0x4E && ctx.data[3] == 0x47);

    bool success = false;
    if (is_jpeg)
    {
      ESP_LOGE(TAG, "Sleep cover: rendering JPEG");
      success = M5.Display.drawJpg(ctx.data, ctx.size, 0, 0,
                                   M5.Display.width(), M5.Display.height(),
                                   0, 0, 0, 0, datum_t::middle_center);
    }
    else if (is_png)
    {
      ESP_LOGE(TAG, "Sleep cover: rendering PNG");
      success = M5.Display.drawPng(ctx.data, ctx.size, 0, 0,
                                   M5.Display.width(), M5.Display.height(),
                                   0, 0, 0, 0, datum_t::middle_center);
    }
    else
    {
      ESP_LOGW(TAG, "Sleep cover: unknown image format (first bytes: 0x%02X 0x%02X)",
               ctx.data[0], ctx.data[1]);
    }

    if (success)
    {
      M5.Display.display();
      M5.Display.waitDisplay();
      ESP_LOGE(TAG, "Sleep cover rendered");
    }
    else
    {
      ESP_LOGW(TAG, "Sleep cover render failed");
    }
#else
    // Fallback to renderer for other boards
    int img_w = 0;
    int img_h = 0;
    bool can_render = renderer->get_image_size(ctx.cover_path, ctx.data, ctx.size, &img_w, &img_h);
    ESP_LOGE(TAG, "Sleep cover size: ok=%d w=%d h=%d", can_render ? 1 : 0, img_w, img_h);
    if (can_render && img_w > 0 && img_h > 0)
    {
      renderer->set_margin_top(0);
      renderer->set_margin_bottom(0);
      renderer->set_margin_left(0);
      renderer->set_margin_right(0);
      int width = renderer->get_page_width();
      int height = renderer->get_page_height();
      renderer->clear_screen();
      renderer->set_image_placeholder_enabled(false);
      renderer->draw_image(ctx.cover_path, ctx.data, ctx.size, 0, 0, width, height);
      renderer->set_image_placeholder_enabled(true);
      renderer->flush_display();
      ESP_LOGE(TAG, "Sleep cover rendered");
    }
    else
    {
      ESP_LOGE(TAG, "Sleep cover render skipped");
    }
#endif
  }
  if (ctx.data)
  {
    free(ctx.data);
  }
  if (was_subscribed)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  return;
#else
  if (was_subscribed)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  return;
#endif
}

static void show_sleep_image(Renderer *renderer)
{
  ESP_LOGE(TAG, ">>> show_sleep_image() called, mode=%d (0=Cover,1=Random,2=Custom,3=Off)", (int)sleep_image_mode);

  if (sleep_image_mode == SLEEP_IMAGE_OFF)
  {
    ESP_LOGE(TAG, ">>> Sleep image OFF, skipping");
    return;
  }

  if (sleep_image_mode == SLEEP_IMAGE_COVER)
  {
    ESP_LOGE(TAG, ">>> Sleep image COVER mode");
    show_sleep_cover(renderer);
    return;
  }

  // Display flash-embedded custom sleep image
  if (sleep_image_mode == SLEEP_IMAGE_CUSTOM)
  {
    const char *sleep_image_path = "/Sleep/bg.png";
    ESP_LOGE(TAG, ">>> Displaying custom sleep image from SD: %s", sleep_image_path);

    // Remove from watchdog during sleep image display (file I/O and image loading can be slow)
    esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    bool was_subscribed = (wdt_err == ESP_OK);

    if (!SD.exists(sleep_image_path))
    {
      ESP_LOGW(TAG, "Sleep image not found at %s", sleep_image_path);
      if (was_subscribed)
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      show_sleep_cover(renderer);
      return;
    }

    // Do a full quality clear to reduce ghosting before the sleep image
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.display();
    M5.Display.waitDisplay();

    // Pre-darken to push the panel toward solid black for the inverted image
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.display();
    M5.Display.waitDisplay();

    File sleep_fp = SD.open(sleep_image_path, FILE_READ);
    if (!sleep_fp)
    {
      ESP_LOGW(TAG, "Failed to open sleep image: %s", sleep_image_path);
      if (was_subscribed)
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      show_sleep_cover(renderer);
      return;
    }

    size_t sleep_size = sleep_fp.size();
    sleep_fp.close();
    ESP_LOGE(TAG, ">>> Sleep image size: %zu bytes", sleep_size);
    if (sleep_size == 0)
    {
      ESP_LOGW(TAG, "Sleep image is empty: %s", sleep_image_path);
      if (was_subscribed)
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      show_sleep_cover(renderer);
      return;
    }

    bool success = M5.Display.drawPngFile(SD, sleep_image_path, 0, 0,
                                          M5.Display.width(), M5.Display.height(),
                                          0, 0, 0, 0, datum_t::middle_center);
    M5.Display.display();
    M5.Display.waitDisplay();

    ESP_LOGE(TAG, ">>> drawPngFile result: %s", success ? "SUCCESS" : "FAILED");

    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    if (!success)
    {
      ESP_LOGW(TAG, "Failed to draw custom sleep image, falling back to cover");
      show_sleep_cover(renderer);
      return;
    }

    ESP_LOGE(TAG, ">>> Custom sleep image displayed");
    return;
  }

  if (sleep_image_mode != SLEEP_IMAGE_RANDOM)
  {
    return;
  }

  // Remove from watchdog during sleep image display (file I/O and image loading can be slow)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  const char *pics_dir = nullptr;
  DIR *dir = nullptr;
  const char *candidates[] = {"/sd/pic"};
  for (int i = 0; i < 1; i++)
  {
    dir = opendir(candidates[i]);
    if (dir)
    {
      pics_dir = candidates[i];
      break;
    }
  }
  if (!dir)
  {
    ESP_LOGW("main", "Sleep image directory not found: %s", "/sd/pic");
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  char selected_path[512] = {0};
  int image_count = 0;
  struct dirent *ent;

  while ((ent = readdir(dir)) != NULL)
  {
    if (ent->d_name[0] == '.' || ent->d_type == DT_DIR)
    {
      continue;
    }

    const char *dot = strrchr(ent->d_name, '.');
    if (!dot || !dot[1])
    {
      continue;
    }
    const char *ext = dot + 1;
    char e0 = tolower(ext[0]);
    char e1 = tolower(ext[1]);
    char e2 = tolower(ext[2]);
    char e3 = tolower(ext[3]);

    bool is_jpg = (e0 == 'j' && e1 == 'p' && e2 == 'g' && ext[3] == '\0');
    bool is_jpeg = (e0 == 'j' && e1 == 'p' && e2 == 'e' && e3 == 'g' && ext[4] == '\0');
    bool is_png = (e0 == 'p' && e1 == 'n' && e2 == 'g' && ext[3] == '\0');
    if (!is_jpg && !is_jpeg && !is_png)
    {
      continue;
    }

    // Reservoir sampling so each image has equal probability.
    image_count++;
    if (image_count == 1)
    {
      snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
    }
    else
    {
      uint32_t r = esp_random();
      if (r % (uint32_t)image_count == 0)
      {
        snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
      }
    }

    // Yield to watchdog on each iteration to prevent timeouts
    vTaskDelay(1);
  }
  closedir(dir);

  if (image_count == 0 || selected_path[0] == '\0')
  {
    ESP_LOGW("main", "No image files found in %s", pics_dir);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  const char *sleep_image_path = selected_path;
  FILE *fp = fopen(sleep_image_path, "rb");
  if (!fp)
  {
    ESP_LOGW("main", "Sleep image not found at %s", sleep_image_path);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  if (fseek(fp, 0, SEEK_END) != 0)
  {
    fclose(fp);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }
  long size = ftell(fp);
  if (size <= 0)
  {
    fclose(fp);
    ESP_LOGW("main", "Sleep image file has invalid size: %s", sleep_image_path);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }
  if (fseek(fp, 0, SEEK_SET) != 0)
  {
    fclose(fp);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (!data)
  {
    fclose(fp);
    ESP_LOGW("main", "Failed to allocate memory for sleep image");
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  size_t read = fread(data, 1, (size_t)size, fp);
  fclose(fp);
  if (read != (size_t)size)
  {
    free(data);
    ESP_LOGW("main", "Failed to read full sleep image");
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

#ifdef BOARD_TYPE_M5_PAPER
  // Use M5GFX's drawJpg directly - much faster and more reliable than JPEGDEC
  // This matches the approach used in the working m5book text reader
  ESP_LOGI("main", "Displaying sleep image: %s (%ld bytes)", sleep_image_path, size);

  vTaskDelay(1);
  // Clear screen and draw image
  M5.Display.fillScreen(0xFF); // White

  bool success = M5.Display.drawJpg(data, (size_t)size, 0, 0,
                                    M5.Display.width(), M5.Display.height());

  free(data);

  if (!success)
  {
    ESP_LOGW("main", "Failed to decode sleep image, falling back to cover");
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  ESP_LOGI("main", "Sleep image displayed successfully");
  if (was_subscribed)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
#else
  // Fallback to renderer-based drawing for other boards
  // Yield before potentially expensive image decode operation
  vTaskDelay(1);

  int img_w = 0;
  int img_h = 0;
  bool can_render = renderer->get_image_size(sleep_image_path, data, (size_t)size, &img_w, &img_h);

  // Yield after image decode
  vTaskDelay(1);
  if (!can_render || img_w <= 0 || img_h <= 0)
  {
    free(data);
    ESP_LOGW("main", "Sleep image decode failed: %s", sleep_image_path);
    if (was_subscribed)
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    show_sleep_cover(renderer);
    return;
  }

  // Draw full-screen, ignoring margins.
  renderer->set_margin_top(0);
  renderer->set_margin_bottom(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);

  int width = renderer->get_page_width();
  int height = renderer->get_page_height();

  renderer->clear_screen();
  // For sleep images we do not want to show the generic cover placeholder
  // if decoding fails; just leave the screen as-is in that case.
  renderer->set_image_placeholder_enabled(false);

  // Yield before drawing the image (potentially expensive operation)
  vTaskDelay(1);

  renderer->draw_image(sleep_image_path, data, (size_t)size, 0, 0, width, height);
  renderer->set_image_placeholder_enabled(true);
  free(data);

  // Yield after drawing before display flush
  vTaskDelay(1);

  renderer->flush_display();
  if (was_subscribed)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
#endif
}

void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw)
{
  // load up the epub list from the filesystem
  if (!epub_list)
  {
    ESP_LOGI("main", "Creating epub list");
    epub_list = new EpubList(renderer, epub_list_state);
    // Paper S3 stores all EPUBs on the SD card under /fs/Books.
    if (epub_list->load("/Books"))
    {
      ESP_LOGI("main", "Epub files loaded");
    }
  }
  if (needs_redraw)
  {
    epub_list->set_needs_redraw();
  }
  // work out what the user wants us to do
  switch (action)
  {
  case UP:
    epub_list->prev();
    break;
  case DOWN:
    epub_list->next();
    break;
  case SELECT:
    // Try to show the table of contents if the book has one; otherwise
    // fall back to opening the book directly.
    ui_state = SELECTING_TABLE_CONTENTS;
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    if (!contents->load())
    {
      ESP_LOGE(TAG, ">>> TOC load failed, falling back to direct EPUB read");
      delete contents;
      contents = nullptr;
      ui_state = READING_EPUB;
      handleEpub(renderer, NONE);
      return;
    }
    contents->set_needs_redraw();
    handleEpubTableContents(renderer, NONE, true);
    return;
  case NONE:
  default:
    // nothing to do
    break;
  }
  epub_list->render();
}

void handleReaderMenu(Renderer *renderer, UIAction action)
{
  int item_total = reader_menu_advanced ? READER_MENU_ADVANCED_ITEMS : READER_MENU_BASIC_ITEMS;

  switch (action)
  {
  case UP:
    if (item_total > 0)
    {
      reader_menu_selected = (reader_menu_selected - 1 + item_total) % item_total;
    }
    renderReaderMenu(renderer);
    break;
  case DOWN:
    if (item_total > 0)
    {
      reader_menu_selected = (reader_menu_selected + 1) % item_total;
    }
    renderReaderMenu(renderer);
    break;
  case SELECT:
    if (!reader_menu_advanced)
    {
      if (reader_menu_selected == 0)
      {
        ui_state = READING_EPUB;
        renderer->clear_screen();
        if (reader)
        {
          reader->render();
        }
      }
      else if (reader_menu_selected == 1)
      {
        if (epub_list_state.selected_item >= 0 && epub_list_state.selected_item < epub_list_state.num_epubs)
        {
          EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
          item.bookmark_section = item.current_section;
          item.bookmark_page = item.current_page;
          item.bookmark_set = true;
          if (epub_list)
          {
            epub_list->save_index(books_index_path);
          }
          show_status_bar_toast(renderer, "Bookmark set");
        }
        ui_state = READING_EPUB;
        renderer->clear_screen();
        if (reader)
        {
          reader->render();
        }
      }
      else if (reader_menu_selected == 2)
      {
        ui_state = SELECTING_TABLE_CONTENTS;
        if (contents)
        {
          delete contents;
          contents = nullptr;
        }
        contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
        if (!contents->load())
        {
          delete contents;
          contents = nullptr;
          ui_state = READING_EPUB;
          renderer->clear_screen();
          if (reader)
          {
            reader->render();
          }
          break;
        }
        contents->set_needs_redraw();
        handleEpubTableContents(renderer, NONE, true);
      }
      else if (reader_menu_selected == 3)
      {
        // Back to library: force a full-screen refresh and show the
        // same "Book library is loading" splash used on cold boot
        // while the EPUB list is (re)rendered.
        ui_state = SELECTING_EPUB;
        renderer->reset();
        show_library_loading(renderer);
        if (reader)
        {
          delete reader;
          reader = nullptr;
        }
        handleEpubList(renderer, NONE, true);
      }
      else if (reader_menu_selected == 4)
      {
        reader_menu_advanced = true;
        reader_menu_selected = 0;
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 5)
      {
        // Full screen refresh of the current reading page to
        // mitigate ghosting.
        ui_state = READING_EPUB;
        renderer->reset();
        if (reader)
        {
          reader->render();
        }
      }
      else if (reader_menu_selected == 6)
      {
        // Request immediate sleep; main_task's event loop will
        // see this flag and break out to the sleep sequence.
        g_request_sleep_now = true;
      }
    }
    else
    {
      // Advanced menu entries
      if (reader_menu_selected == 0)
      {
        status_bar_visible = !status_bar_visible;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 1)
      {
        epub_list_state.use_grid_view = !epub_list_state.use_grid_view;
        if (epub_list)
        {
          epub_list->set_needs_redraw();
        }
        save_app_settings(renderer);
        show_status_bar_toast(renderer, epub_list_state.use_grid_view ? "Library view: Grid" : "Library view: List");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 2)
      {
        open_last_book_on_startup = !open_last_book_on_startup;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, open_last_book_on_startup ? "Startup: Open last book" : "Startup: Library");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 3)
      {
        switch (sleep_image_mode)
        {
        case SLEEP_IMAGE_COVER:
          sleep_image_mode = SLEEP_IMAGE_RANDOM;
          show_status_bar_toast(renderer, "Sleep image: Random");
          break;
        case SLEEP_IMAGE_RANDOM:
          sleep_image_mode = SLEEP_IMAGE_CUSTOM;
          show_status_bar_toast(renderer, "Sleep image: Custom");
          break;
        case SLEEP_IMAGE_CUSTOM:
          sleep_image_mode = SLEEP_IMAGE_OFF;
          show_status_bar_toast(renderer, "Sleep image: Off");
          break;
        case SLEEP_IMAGE_OFF:
        default:
          sleep_image_mode = SLEEP_IMAGE_COVER;
          show_status_bar_toast(renderer, "Sleep image: Cover");
          break;
        }
        save_app_settings(renderer);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 4)
      {
#ifdef USE_FREETYPE
        int sizes[] = {18, 22, 26};
        int current_px = renderer->get_reading_font_pixel_height();
        int index = 0;
        for (int i = 0; i < 3; i++)
        {
          if (sizes[i] == current_px)
          {
            index = i;
          }
        }
        index = (index + 1) % 3;
        int next_px = sizes[index];
        renderer->set_reading_font_pixel_height(next_px);
        save_app_settings(renderer);
        show_status_bar_toast(renderer, "Font size changed");
#else
        (void)renderer;
#endif
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 5)
      {
        // Toggle paragraph alignment between left-aligned and
        // fully-justified. The actual layout is handled by
        // RubbishHtmlParser via EpubReader::set_justified().
        justify_paragraphs = !justify_paragraphs;
        save_app_settings(renderer);
        if (reader)
        {
          reader->set_justified(justify_paragraphs);
        }
        show_status_bar_toast(renderer, justify_paragraphs ? "Alignment: Justified" : "Alignment: Left");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 6)
      {
        invert_tap_zones = !invert_tap_zones;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, invert_tap_zones ? "Tap zones: inverted" : "Tap zones: normal");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 7)
      {
        idle_profile = (IdleProfile)(((int)idle_profile + 1) % 3);
        apply_idle_profile();
        save_app_settings(renderer);
        const char *label = "Idle: Normal";
        if (idle_profile == IDLE_PROFILE_SHORT)
        {
          label = "Idle: Short";
        }
        else if (idle_profile == IDLE_PROFILE_LONG)
        {
          label = "Idle: Long";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 8)
      {
        margin_profile = (MarginProfile)(((int)margin_profile + 1) % 3);
        apply_page_margins(renderer);
        save_app_settings(renderer);
        const char *label = "Margins: Normal";
        if (margin_profile == MARGIN_PROFILE_NARROW)
        {
          label = "Margins: Narrow";
        }
        else if (margin_profile == MARGIN_PROFILE_WIDE)
        {
          label = "Margins: Wide";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 9)
      {
        gesture_sensitivity = (GestureSensitivity)(((int)gesture_sensitivity + 1) % 3);
        apply_gesture_profile();
        save_app_settings(renderer);
        const char *label = "Gestures: Medium";
        if (gesture_sensitivity == GESTURE_SENS_LOW)
        {
          label = "Gestures: Low";
        }
        else if (gesture_sensitivity == GESTURE_SENS_HIGH)
        {
          label = "Gestures: High";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 10)
      {
        line_spacing_profile = (LineSpacingProfile)(((int)line_spacing_profile + 1) % 3);
        apply_line_spacing_profile(renderer);
        save_app_settings(renderer);
        int spacing_percent = 100;
        if (line_spacing_profile == LINE_SPACING_120)
        {
          spacing_percent = 120;
        }
        else if (line_spacing_profile == LINE_SPACING_140)
        {
          spacing_percent = 140;
        }
        char toast[32];
        snprintf(toast, sizeof(toast), "Line spacing: %d%%", spacing_percent);
        show_status_bar_toast(renderer, toast);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 11)
      {
        save_app_settings(renderer);
        reader_menu_advanced = false;
        reader_menu_selected = 0;
        renderReaderMenu(renderer);
      }
    }
    break;
  case NONE:
  default:
    renderReaderMenu(renderer);
    break;
  }
}






