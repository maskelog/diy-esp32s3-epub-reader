#include <Arduino.h>
#include <unistd.h>
#include <esp_sleep.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <SD.h>

#include "config.h"
#include "UIState.h"
#include "AppSettings.h"
#include "AppState.h"
#include "StatusBar.h"
#include "SleepScreen.h"
#include "ReaderMenu.h"
#include "Handlers.h"

#include "EpubList/Epub.h"
#include "EpubList/EpubList.h"
#include "EpubList/EpubReader.h"
#include "EpubList/EpubToc.h"
#include "EpubList/State.h"
#include "RubbishHtmlParser/RubbishHtmlParser.h"
#include "ZipFile/ZipFile.h"

#ifdef BOARD_TYPE_M5_PAPER
#include "Renderer/M5GfxRenderer.h"
#include "WifiUploader.h"
#include <M5Unified.h>
#endif

#ifdef USE_FREETYPE
#include "Renderer/FreeTypeFont.h"
#endif

#include "boards/Board.h"
#include "boards/controls/M5PaperButtonControls.h"
#include "boards/controls/M5PaperTouchControls.h"

static const char *TAG = "main";

static const TickType_t ACTIVE_QUEUE_WAIT_TICKS = pdMS_TO_TICKS(10);
static const TickType_t READING_QUEUE_WAIT_TICKS = pdMS_TO_TICKS(100);

#ifdef BOARD_TYPE_M5_PAPER
static void set_cpu_active_mode()
{
  setCpuFrequencyMhz(240);
}

static void set_cpu_reading_idle_mode()
{
  if (!wifi_uploader_is_running())
    setCpuFrequencyMhz(80);
}
#else
static void set_cpu_active_mode() {}
static void set_cpu_reading_idle_mode() {}
#endif

// ── App-wide path constants ───────────────────────────────────────────────

const char *books_index_path = "/Books/BOOKS.IDX";

// ── Runtime state (declared extern in AppState.h) ─────────────────────────

UIState      ui_state        = SELECTING_EPUB;
EpubListState epub_list_state = {};
EpubTocState  epub_index_state = {};

EpubList   *epub_list = nullptr;
EpubReader *reader    = nullptr;
EpubToc    *contents  = nullptr;

bool g_request_sleep_now = false;

// When true, the next sleep will skip the sleep cover image — used by the
// idle-timeout path so the user wakes to the same page they were reading
// instead of seeing a cover transition.
static bool g_idle_sleep_silent = false;

// Microsecond timestamp of the last user action; drives idle-timeout sleep.
static int64_t g_last_interaction_us = 0;

// ── Hardware handles ──────────────────────────────────────────────────────

Board         *board           = nullptr;
Renderer      *renderer        = nullptr;
Battery       *battery         = nullptr;
ButtonControls *button_controls = nullptr;
TouchControls  *touch_controls  = nullptr;
QueueHandle_t  ui_queue        = nullptr;

// ── show_library_loading ──────────────────────────────────────────────────

void show_library_loading(Renderer *renderer)
{
  renderer->clear_screen();
  int page_width  = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0) return;

  const char *msg = "Book library is loading";
  int text_width  = renderer->get_text_width(msg, false, false);
  if (text_width < 0) text_width = 0;

  int x = (page_width - text_width) / 2;
  if (x < 0) x = 0;
  int y = page_height / 2 - (3 * line_height) / 4;

  renderer->draw_text(x, y, msg, false, false);
  renderer->flush_display();
}

// ── last-opened-book persistence ──────────────────────────────────────────

static const char *last_book_path_file = "/Books/last_book.txt";

void save_last_book_path(const char *path)
{
  if (!path || !*path) return;
  // Best-effort: rewrite the small text file with just the EPUB path.
  if (SD.exists(last_book_path_file)) SD.remove(last_book_path_file);
  File fp = SD.open(last_book_path_file, FILE_WRITE);
  if (!fp) return;
  fp.write((const uint8_t *)path, strlen(path));
  fp.flush();
  fp.close();
}

static int find_last_book_index_by_saved_path()
{
  File fp = SD.open(last_book_path_file, FILE_READ);
  if (!fp) return -1;
  char buf[MAX_PATH_SIZE] = {0};
  size_t n = fp.read((uint8_t *)buf, sizeof(buf) - 1);
  fp.close();
  if (n == 0) return -1;
  buf[n] = 0;
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                   buf[n - 1] == ' '  || buf[n - 1] == '\t'))
  {
    buf[--n] = 0;
  }
  if (n == 0) return -1;
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    if (strcmp(epub_list_state.epub_list[i].path, buf) == 0) return i;
  }
  return -1;
}

// ── find_last_open_book_index ─────────────────────────────────────────────

static int find_last_open_book_index()
{
  // Prefer the explicitly-recorded last-opened book — fixes the case where
  // a freshly-bookmarked book has lower section/page than an older read.
  int saved = find_last_book_index_by_saved_path();
  if (saved >= 0) return saved;

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
            (item.current_section == best.current_section &&
             item.current_page > best.current_page))
          last_index = i;
      }
    }
  }
  if (last_index >= 0) return last_index;

  // Fallback: any book that has been laid out at least once.
  for (int i = 0; i < epub_list_state.num_epubs; i++)
    if (epub_list_state.epub_list[i].pages_in_current_section > 0) return i;

  return -1;
}

// ── handleEpub ────────────────────────────────────────────────────────────

void handleEpub(Renderer *renderer, UIAction action)
{
  ESP_LOGE(TAG, ">>> handleEpub START: action=%d", action);

#ifdef BOARD_TYPE_M5_PAPER
  if (wifi_uploader_is_running())
    stop_wifi_uploader(renderer, false);
#endif

  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  if (!reader)
  {
    ESP_LOGE(TAG, ">>> Creating EpubReader for: %s",
             epub_list_state.epub_list[epub_list_state.selected_item].path);
    vTaskDelay(10);

    EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
    if (item.bookmark_set)
    {
      item.current_section = item.bookmark_section;
      item.current_page    = item.bookmark_page;
    }
    save_last_book_path(item.path);
    reader = new EpubReader(item, renderer);
    reader->set_justified(justify_paragraphs);
    vTaskDelay(10);

    if (!reader->load())
    {
      ESP_LOGE(TAG, ">>> EpubReader::load() FAILED!");
      delete reader; reader = nullptr;
      if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      return;
    }
    ESP_LOGE(TAG, ">>> EpubReader::load() SUCCESS");
    vTaskDelay(10);
    renderer->clear_screen();
  }

  switch (action)
  {
  case UP:           reader->prev();         break;
  case DOWN:         reader->next();         break;
  case PREV_SECTION: reader->prev_section(); break;
  case NEXT_SECTION: reader->next_section(); break;
  case REFRESH_PAGE: renderer->reset();      break;
  case SELECT:
    ui_state = SELECTING_EPUB;
    renderer->clear_screen();
    delete reader; reader = nullptr;
    if (!epub_list) epub_list = new EpubList(renderer, epub_list_state);
    handleEpubList(renderer, NONE, true);
    if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
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

  if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  ESP_LOGE(TAG, "<<< handleEpub END");
}

// ── handleEpubTableContents ───────────────────────────────────────────────

void handleEpubTableContents(Renderer *renderer, UIAction action, bool needs_redraw)
{
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

  if (!contents)
  {
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item],
                           epub_index_state, renderer);
    contents->set_needs_redraw();
    contents->load();
  }

  switch (action)
  {
  case UP:   contents->prev(); break;
  case DOWN: contents->next(); break;
  case SELECT:
    ui_state = READING_EPUB;
    if (reader) { delete reader; reader = nullptr; }
    reader = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->set_justified(justify_paragraphs);
    reader->set_state_section(contents->get_selected_toc());
    if (!reader->load())
    {
      ESP_LOGE(TAG, "Failed to load EPUB from TOC selection");
      delete reader; reader = nullptr;
      ui_state = SELECTING_TABLE_CONTENTS;
      if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
      return;
    }
    delete contents; contents = nullptr;
    if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    handleEpub(renderer, NONE);
    return;
  case NONE:
  default:
    break;
  }

  contents->render();
  if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
}

// ── handleEpubList ────────────────────────────────────────────────────────

void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw)
{
  if (!epub_list)
  {
    ESP_LOGI(TAG, "Creating epub list");
    epub_list = new EpubList(renderer, epub_list_state);
    if (epub_list->load("/Books"))
      ESP_LOGI(TAG, "Epub files loaded");
  }
  if (needs_redraw) epub_list->set_needs_redraw();

  switch (action)
  {
  case UP:   epub_list->prev(); break;
  case DOWN: epub_list->next(); break;
  case SELECT:
    if (reader)
    {
      delete reader;
      reader = nullptr;
    }
    if (contents)
    {
      delete contents;
      contents = nullptr;
    }
    epub_index_state.selected_item = 0;
    epub_index_state.previous_selected_item = -1;
    epub_index_state.previous_rendered_page = -1;
    epub_index_state.num_items = 0;
    ui_state = READING_EPUB;
    handleEpub(renderer, NONE);
    return;
  case NONE:
  default:
    break;
  }
  epub_list->render();
}

// ── handleUserInteraction ─────────────────────────────────────────────────

void handleUserInteraction(Renderer *renderer, UIAction ui_action, bool needs_redraw)
{
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);
  ESP_LOGE(TAG, ">>> handleUserInteraction START: action=%d, redraw=%d", ui_action, needs_redraw);

  // Toggle status bar while reading
  if (ui_action == TOGGLE_STATUS_BAR && ui_state == READING_EPUB)
  {
    status_bar_visible = !status_bar_visible;
    save_app_settings(renderer);
    handleEpub(renderer, NONE);
    show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
    if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    return;
  }

  // Open advanced settings from library view
  if (ui_action == OPEN_READER_MENU && ui_state == SELECTING_EPUB)
  {
    ui_state = READING_MENU;
    open_reader_menu(renderer, true);
    if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
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
      open_reader_menu(renderer, false);
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

  ESP_LOGE(TAG, "<<< handleUserInteraction END");
  if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
}

// ── setup / loop ──────────────────────────────────────────────────────────

void setup()
{
  set_cpu_active_mode();

  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);

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
  if (battery) battery->setup();

  apply_page_margins(renderer);

  ui_queue       = xQueueCreate(10, sizeof(UIAction));
  button_controls = board->get_button_controls(ui_queue);
  touch_controls  = board->get_touch_controls(renderer, ui_queue);

  if (button_controls->did_wake_from_deep_sleep())
  {
    bool hydrate_success = renderer->hydrate();
    // Don't consume the wake-button press as an action — the user just
    // wants to resume from idle sleep, not navigate. The normal-boot
    // recovery (open_last_book_on_startup) will bring back the last book.
    if (!hydrate_success)
    {
      renderer->reset();
      show_library_loading(renderer);
      if (!epub_list)
      {
        epub_list = new EpubList(renderer, epub_list_state);
        epub_list->load("/Books");
      }
      if (open_last_book_on_startup)
      {
        int last_book_index = find_last_open_book_index();
        if (last_book_index >= 0)
        {
          epub_list_state.selected_item = last_book_index;
          ui_state = READING_EPUB;
        }
      }
    }
    handleUserInteraction(renderer, NONE, !hydrate_success);
  }
  else
  {
    ESP_LOGE(TAG, ">>> Normal boot - loading library");
    renderer->reset();
    show_library_loading(renderer);
    if (!epub_list)
    {
      epub_list = new EpubList(renderer, epub_list_state);
      epub_list->load("/Books");
    }
    if (open_last_book_on_startup)
    {
      int last_book_index = find_last_open_book_index();
      if (last_book_index >= 0)
      {
        epub_list_state.selected_item = last_book_index;
        ui_state = READING_EPUB;
      }
    }
    handleUserInteraction(renderer, NONE, true);
  }

  if (battery) draw_battery_level(renderer, battery->get_voltage(), battery->get_percentage());
  touch_controls->render(renderer);
  renderer->flush_display();

  if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  set_cpu_reading_idle_mode();
  g_last_interaction_us = esp_timer_get_time();
  ESP_LOGE(TAG, ">>> Setup complete!");
}

void loop()
{
  esp_task_wdt_reset();

  if (g_request_sleep_now)
  {
    g_request_sleep_now = false;
    bool silent = g_idle_sleep_silent;
    g_idle_sleep_silent = false;

#ifdef BOARD_TYPE_M5_PAPER
    if (wifi_uploader_is_running())
      stop_wifi_uploader(renderer, false);
#endif

    // Idle-timeout sleep keeps the current screen visible (e-ink retains
    // the image while powered off); only manual sleep shows the cover.
    if (!silent) show_sleep_image(renderer);
    board->prepare_to_sleep();
    button_controls->setup_deep_sleep();
    esp_deep_sleep_start();
    return;
  }

  button_controls->run();
  touch_controls->run();

#ifdef BOARD_TYPE_M5_PAPER
  if (wifi_uploader_is_running()) wifi_uploader_handle_client();
#endif

  UIAction ui_action = NONE;
  TickType_t queue_wait_ticks = (ui_state == READING_EPUB)
                                ? READING_QUEUE_WAIT_TICKS
                                : ACTIVE_QUEUE_WAIT_TICKS;
  if (xQueueReceive(ui_queue, &ui_action, queue_wait_ticks) == pdTRUE)
  {
    if (ui_action != NONE)
    {
      set_cpu_active_mode();
      handleUserInteraction(renderer, ui_action, false);
      if (battery) draw_battery_level(renderer, battery->get_voltage(), battery->get_percentage());
      renderer->flush_display();
      set_cpu_reading_idle_mode();
      g_last_interaction_us = esp_timer_get_time();
    }
  }

  // Idle-timeout auto sleep: after no user input for the configured
  // window (Idle profile setting), deep-sleep silently so the device
  // stops generating heat. The current page stays on the e-ink panel.
  bool busy = false;
#ifdef BOARD_TYPE_M5_PAPER
  busy = wifi_uploader_is_running();
#endif
  if (!busy)
  {
    int64_t timeout_us = (ui_state == READING_EPUB)
                         ? idle_timeout_reading_us
                         : idle_timeout_library_us;
    if (timeout_us > 0 &&
        (esp_timer_get_time() - g_last_interaction_us) > timeout_us)
    {
      g_idle_sleep_silent = true;
      g_request_sleep_now = true;
    }
  }
}
