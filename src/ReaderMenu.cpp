#include "ReaderMenu.h"
#include "AppSettings.h"
#include "AppState.h"
#include "Handlers.h"
#include "StatusBar.h"
#include "SleepScreen.h"
#include "Renderer/Renderer.h"
#include "EpubList/State.h"
#include "EpubList/EpubList.h"
#include "EpubList/EpubReader.h"
#include "EpubList/EpubToc.h"
#include "EpubList/PaginationBar.h"
#include <esp_log.h>

#ifdef USE_FREETYPE
#include "Renderer/FreeTypeFont.h"
#endif
#ifdef BOARD_TYPE_M5_PAPER
#include "WifiUploader.h"
#endif

static const char *TAG = "ReaderMenu";

static int  reader_menu_selected = 0;
static bool reader_menu_advanced = false;

enum BasicMenuItem {
  BASIC_REFRESH, BASIC_RETURN, BASIC_BOOKMARK, BASIC_TOC,
  BASIC_LIBRARY, BASIC_MORE, BASIC_SLEEP,
  BASIC_ITEM_COUNT
};

enum AdvancedMenuItem {
  ADV_STATUS_BAR, ADV_LIBRARY_VIEW, ADV_STARTUP, ADV_SLEEP_IMAGE,
  ADV_FONT_SIZE, ADV_ALIGNMENT, ADV_TAP_ZONES, ADV_IDLE,
  ADV_MARGINS, ADV_GESTURES, ADV_LINE_SPACING, ADV_ORIENTATION,
#ifdef BOARD_TYPE_M5_PAPER
  ADV_WIFI_UPLOAD,
#endif
  ADV_SAVE_BACK,
  ADV_ITEM_COUNT
};

// ── open_reader_menu ──────────────────────────────────────────────────────

void open_reader_menu(Renderer *renderer, bool advanced)
{
  reader_menu_advanced = advanced;
  reader_menu_selected = 0;
  // Major screen change from book → menu: kick a one-shot GC16 to wipe
  // ghosting from the book content (otherwise the left text column shows
  // through the menu background as a darker band).
  if (renderer) renderer->request_full_refresh();
  renderReaderMenu(renderer);
}

// ── renderReaderMenu ──────────────────────────────────────────────────────

void renderReaderMenu(Renderer *renderer)
{
  const int max_items = 15;
  const char *labels[max_items];
  int items_total = 0;

  char buf_status[32], buf_view[32], buf_startup[40], buf_sleep[40];
  char buf_font[32], buf_align[32], buf_tap[32], buf_idle[32];
  char buf_margin[32], buf_gest[32], buf_spacing[32], buf_orient[32];
#ifdef BOARD_TYPE_M5_PAPER
  char buf_wifi[32];
#endif

  if (!reader_menu_advanced)
  {
    items_total = BASIC_ITEM_COUNT;
    labels[BASIC_REFRESH]  = "[R] Refresh screen";
    labels[BASIC_RETURN]   = "Return to book";
    labels[BASIC_BOOKMARK] = "Bookmark";
    labels[BASIC_TOC]      = "Table of contents";
    labels[BASIC_LIBRARY]  = "Back to library";
    labels[BASIC_MORE]     = "More";
    labels[BASIC_SLEEP]    = "[Zz] Sleep";
  }
  else
  {
    items_total = ADV_ITEM_COUNT;

    snprintf(buf_status, sizeof(buf_status), "Status bar: %s", status_bar_visible ? "ON" : "OFF");
    labels[ADV_STATUS_BAR] = buf_status;

    snprintf(buf_view, sizeof(buf_view), "Library view: %s", epub_list_state.use_grid_view ? "Grid" : "List");
    labels[ADV_LIBRARY_VIEW] = buf_view;

    snprintf(buf_startup, sizeof(buf_startup), "Startup: %s", open_last_book_on_startup ? "Last book" : "Library");
    labels[ADV_STARTUP] = buf_startup;

    const char *sleep_mode_str = "Cover";
    if      (sleep_image_mode == SLEEP_IMAGE_RANDOM) sleep_mode_str = "Random";
    else if (sleep_image_mode == SLEEP_IMAGE_CUSTOM) sleep_mode_str = "Custom";
    else if (sleep_image_mode == SLEEP_IMAGE_OFF)    sleep_mode_str = "Off";
    snprintf(buf_sleep, sizeof(buf_sleep), "Sleep image: %s", sleep_mode_str);
    labels[ADV_SLEEP_IMAGE] = buf_sleep;

#ifdef USE_FREETYPE
    int px = renderer->get_reading_font_pixel_height();
    const char *font_label = (px <= 18) ? "Small" : (px >= 26) ? "Large" : "Medium";
    snprintf(buf_font, sizeof(buf_font), "Font size: %s", font_label);
#else
    snprintf(buf_font, sizeof(buf_font), "Font size");
#endif
    labels[ADV_FONT_SIZE] = buf_font;

    snprintf(buf_align, sizeof(buf_align), "Alignment: %s", justify_paragraphs ? "Justified" : "Left");
    labels[ADV_ALIGNMENT] = buf_align;

    snprintf(buf_tap, sizeof(buf_tap), "Tap zones: %s", invert_tap_zones ? "Inverted" : "Normal");
    labels[ADV_TAP_ZONES] = buf_tap;

    const char *idle_str = (idle_profile == IDLE_PROFILE_SHORT) ? "Short"
                         : (idle_profile == IDLE_PROFILE_LONG)  ? "Long" : "Normal";
    snprintf(buf_idle, sizeof(buf_idle), "Idle: %s", idle_str);
    labels[ADV_IDLE] = buf_idle;

    const char *margin_str = (margin_profile == MARGIN_PROFILE_NARROW) ? "Narrow"
                           : (margin_profile == MARGIN_PROFILE_WIDE)   ? "Wide" : "Normal";
    snprintf(buf_margin, sizeof(buf_margin), "Margins: %s", margin_str);
    labels[ADV_MARGINS] = buf_margin;

    const char *gest_str = (gesture_sensitivity == GESTURE_SENS_LOW)  ? "Low"
                         : (gesture_sensitivity == GESTURE_SENS_HIGH) ? "High" : "Medium";
    snprintf(buf_gest, sizeof(buf_gest), "Gestures: %s", gest_str);
    labels[ADV_GESTURES] = buf_gest;

    int spacing_percent = (line_spacing_profile == LINE_SPACING_120) ? 120
                        : (line_spacing_profile == LINE_SPACING_140) ? 140 : 100;
    snprintf(buf_spacing, sizeof(buf_spacing), "Line spacing: %d%%", spacing_percent);
    labels[ADV_LINE_SPACING] = buf_spacing;

    snprintf(buf_orient, sizeof(buf_orient), "Orientation: %s", landscape_mode ? "Landscape" : "Portrait");
    labels[ADV_ORIENTATION] = buf_orient;

#ifdef BOARD_TYPE_M5_PAPER
    snprintf(buf_wifi, sizeof(buf_wifi), "WiFi upload: %s", wifi_uploader_is_running() ? "ON" : "OFF");
    labels[ADV_WIFI_UPLOAD] = buf_wifi;
#endif
    labels[ADV_SAVE_BACK] = "Save & Back";
  }

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(false);
#endif

  renderer->clear_screen();
  int page_width  = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (line_height <= 0) line_height = 20;
  if (page_height <= 0) page_height = line_height * items_total * 2;
  if (page_width  <= 0) page_width  = 400;

  if (items_total <= 0)
  {
#ifdef USE_FREETYPE
    renderer->set_freetype_enabled(true);
#endif
    return;
  }

  int items_per_page = EPUB_TOC_ITEMS_PER_PAGE;
  if (items_per_page <= 0 || items_per_page > items_total) items_per_page = items_total;

  if (reader_menu_selected < 0)                reader_menu_selected = 0;
  if (reader_menu_selected >= items_total)     reader_menu_selected = items_total - 1;

  int total_pages   = (items_total + items_per_page - 1) / items_per_page;
  if (total_pages < 1) total_pages = 1;

  int current_page  = reader_menu_selected / items_per_page;
  if (current_page < 0)            current_page = 0;
  if (current_page >= total_pages) current_page = total_pages - 1;

  int start_index   = current_page * items_per_page;
  int end_index     = start_index + items_per_page;
  if (end_index > items_total)     end_index = items_total;
  int visible_count = end_index - start_index;

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int content_height    = (bottom_bar_height > 0 && bottom_bar_height < page_height)
                        ? page_height - bottom_bar_height : page_height;

  int button_vertical_padding = line_height / 4;
  if (button_vertical_padding < 2) button_vertical_padding = 2;
  int button_height  = line_height + button_vertical_padding * 2;
  int button_spacing = line_height / 4;
  if (button_spacing < 2) button_spacing = 2;

  int max_label_width = 0;
  for (int i = 0; i < items_total; i++)
  {
    int w = renderer->get_text_width(labels[i], false, false);
    if (w > max_label_width) max_label_width = w;
  }
  const int horizontal_padding = 30;
  int button_width = max_label_width + horizontal_padding * 2;
  if (button_width > page_width - 40) button_width = page_width - 40;

  int container_width  = button_width;
  int container_height = visible_count * button_height + (visible_count - 1) * button_spacing;
  int container_x = (page_width - container_width) / 2;
  if (container_x < 0) container_x = 0;
  int container_y = (content_height - container_height) / 2;
  if (container_y < 0) container_y = 0;

  int ypos = container_y;
  for (int i = 0; i < visible_count; i++)
  {
    int item_index    = start_index + i;
    const char *label = labels[item_index];

    renderer->fill_rect(container_x, ypos, container_width, button_height, 255);
    renderer->draw_rect(container_x, ypos, container_width, button_height, 0);

    if (item_index == reader_menu_selected)
    {
      for (int line = 0; line < 3; line++)
      {
        renderer->draw_rect(container_x + line, ypos + line,
                            container_width - 2 * line, button_height - 2 * line, 0);
      }
    }

    int label_width = renderer->get_text_width(label, false, false);
    if (label_width < 0) label_width = 0;
    int text_x = container_x + (container_width - label_width) / 2;
    int text_y = ypos + (button_height / 2) - (3 * line_height) / 4;
    renderer->draw_text(text_x, text_y, label, false, false);

    ypos += button_height + button_spacing;
  }

  // Bottom navigation bar (shared with the epub list and TOC screens)
  draw_pagination_bar(renderer, current_page, total_pages);

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(true);
#endif
}

// ── handleReaderMenu ──────────────────────────────────────────────────────

void handleReaderMenu(Renderer *renderer, UIAction action)
{
  int item_total = reader_menu_advanced ? (int)ADV_ITEM_COUNT : (int)BASIC_ITEM_COUNT;

  switch (action)
  {
  case UP:
    if (item_total > 0)
      reader_menu_selected = (reader_menu_selected - 1 + item_total) % item_total;
    renderReaderMenu(renderer);
    break;

  case DOWN:
    if (item_total > 0)
      reader_menu_selected = (reader_menu_selected + 1) % item_total;
    renderReaderMenu(renderer);
    break;

  case SELECT:
    if (!reader_menu_advanced)
    {
      // ── Basic menu ────────────────────────────────────────────────────
      switch (reader_menu_selected)
      {
      case BASIC_REFRESH:
        // Draw the completed page with GC16. Avoid reset(), which pushes a
        // blank GC16 frame before the page is rendered and leaves the final
        // page update to the normal DU path.
        ui_state = READING_EPUB;
        renderer->clear_screen();
        renderer->request_full_refresh();
        if (reader) reader->render();
        break;

      case BASIC_RETURN:
        // Return to book
        ui_state = READING_EPUB;
        renderer->clear_screen();
        if (reader) reader->render();
        break;

      case BASIC_BOOKMARK:
        // Bookmark
        if (epub_list_state.selected_item >= 0 &&
            epub_list_state.selected_item < epub_list_state.num_epubs)
        {
          EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
          item.bookmark_section = item.current_section;
          item.bookmark_page    = item.current_page;
          item.bookmark_set     = true;
          if (epub_list) epub_list->save_index(books_index_path);
          save_last_book_path(item.path);
          show_status_bar_toast(renderer, "Bookmark set");
        }
        ui_state = READING_EPUB;
        renderer->clear_screen();
        if (reader) reader->render();
        break;

      case BASIC_TOC:
        // Table of contents
        ui_state = SELECTING_TABLE_CONTENTS;
        if (contents) { delete contents; contents = nullptr; }
        contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item],
                               epub_index_state, renderer);
        if (!contents->load())
        {
          delete contents; contents = nullptr;
          ui_state = READING_EPUB;
          renderer->clear_screen();
          if (reader) reader->render();
          break;
        }
        contents->set_needs_redraw();
        handleEpubTableContents(renderer, NONE, true);
        break;

      case BASIC_LIBRARY:
        // Back to library
        ui_state = SELECTING_EPUB;
        renderer->reset();
        show_library_loading(renderer);
        if (reader) { delete reader; reader = nullptr; }
        handleEpubList(renderer, NONE, true);
        break;

      case BASIC_MORE:
        // More (open advanced settings)
        reader_menu_advanced = true;
        reader_menu_selected = 0;
        renderReaderMenu(renderer);
        break;

      case BASIC_SLEEP:
        // Sleep
        g_request_sleep_now = true;
        break;

      default:
        break;
      }
    }
    else
    {
      // ── Advanced menu ─────────────────────────────────────────────────
      switch (reader_menu_selected)
      {
      case ADV_STATUS_BAR:
        status_bar_visible = !status_bar_visible;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
        renderReaderMenu(renderer);
        break;

      case ADV_LIBRARY_VIEW:
        epub_list_state.use_grid_view = !epub_list_state.use_grid_view;
        if (epub_list) epub_list->set_needs_redraw();
        save_app_settings(renderer);
        show_status_bar_toast(renderer, epub_list_state.use_grid_view
                                        ? "Library view: Grid" : "Library view: List");
        renderReaderMenu(renderer);
        break;

      case ADV_STARTUP:
        open_last_book_on_startup = !open_last_book_on_startup;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, open_last_book_on_startup
                                        ? "Startup: Open last book" : "Startup: Library");
        renderReaderMenu(renderer);
        break;

      case ADV_SLEEP_IMAGE:
        switch (sleep_image_mode)
        {
        case SLEEP_IMAGE_COVER:  sleep_image_mode = SLEEP_IMAGE_RANDOM; show_status_bar_toast(renderer, "Sleep image: Random"); break;
        case SLEEP_IMAGE_RANDOM: sleep_image_mode = SLEEP_IMAGE_CUSTOM; show_status_bar_toast(renderer, "Sleep image: Custom"); break;
        case SLEEP_IMAGE_CUSTOM: sleep_image_mode = SLEEP_IMAGE_OFF;    show_status_bar_toast(renderer, "Sleep image: Off");    break;
        case SLEEP_IMAGE_OFF:
        default:                 sleep_image_mode = SLEEP_IMAGE_COVER;  show_status_bar_toast(renderer, "Sleep image: Cover");  break;
        }
        save_app_settings(renderer);
        renderReaderMenu(renderer);
        break;

      case ADV_FONT_SIZE:
      {
#ifdef USE_FREETYPE
        int sizes[] = {18, 22, 26};
        int current_px = renderer->get_reading_font_pixel_height();
        int index = 0;
        for (int i = 0; i < 3; i++) { if (sizes[i] == current_px) index = i; }
        renderer->set_reading_font_pixel_height(sizes[(index + 1) % 3]);
        save_app_settings(renderer);
        show_status_bar_toast(renderer, "Font size changed");
#else
        (void)renderer;
#endif
        renderReaderMenu(renderer);
        break;
      }

      case ADV_ALIGNMENT:
        justify_paragraphs = !justify_paragraphs;
        save_app_settings(renderer);
        if (reader) reader->set_justified(justify_paragraphs);
        show_status_bar_toast(renderer, justify_paragraphs ? "Alignment: Justified" : "Alignment: Left");
        renderReaderMenu(renderer);
        break;

      case ADV_TAP_ZONES:
        invert_tap_zones = !invert_tap_zones;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, invert_tap_zones ? "Tap zones: inverted" : "Tap zones: normal");
        renderReaderMenu(renderer);
        break;

      case ADV_IDLE:
      {
        idle_profile = (IdleProfile)(((int)idle_profile + 1) % 3);
        apply_idle_profile();
        save_app_settings(renderer);
        const char *label = (idle_profile == IDLE_PROFILE_SHORT) ? "Idle: Short"
                          : (idle_profile == IDLE_PROFILE_LONG)  ? "Idle: Long" : "Idle: Normal";
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
        break;
      }

      case ADV_MARGINS:
      {
        margin_profile = (MarginProfile)(((int)margin_profile + 1) % 3);
        apply_page_margins(renderer);
        save_app_settings(renderer);
        const char *label = (margin_profile == MARGIN_PROFILE_NARROW) ? "Margins: Narrow"
                          : (margin_profile == MARGIN_PROFILE_WIDE)   ? "Margins: Wide" : "Margins: Normal";
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
        break;
      }

      case ADV_GESTURES:
      {
        gesture_sensitivity = (GestureSensitivity)(((int)gesture_sensitivity + 1) % 3);
        apply_gesture_profile();
        save_app_settings(renderer);
        const char *label = (gesture_sensitivity == GESTURE_SENS_LOW)  ? "Gestures: Low"
                          : (gesture_sensitivity == GESTURE_SENS_HIGH) ? "Gestures: High" : "Gestures: Medium";
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
        break;
      }

      case ADV_LINE_SPACING:
      {
        line_spacing_profile = (LineSpacingProfile)(((int)line_spacing_profile + 1) % 3);
        apply_line_spacing_profile(renderer);
        save_app_settings(renderer);
        int pct = (line_spacing_profile == LINE_SPACING_120) ? 120
                : (line_spacing_profile == LINE_SPACING_140) ? 140 : 100;
        char toast[32];
        snprintf(toast, sizeof(toast), "Line spacing: %d%%", pct);
        show_status_bar_toast(renderer, toast);
        renderReaderMenu(renderer);
        break;
      }

      case ADV_ORIENTATION:
        landscape_mode = !landscape_mode;
        apply_orientation(renderer);
        apply_page_margins(renderer);
        save_app_settings(renderer);
        // Force re-layout under the new geometry: drop the reader so the next
        // render rebuilds pages, and dump cached TOC pagination.
        if (reader)   { delete reader;   reader   = nullptr; }
        if (contents) { delete contents; contents = nullptr; }
        if (epub_list) epub_list->set_needs_redraw();
        renderer->reset();
        show_status_bar_toast(renderer, landscape_mode ? "Orientation: Landscape" : "Orientation: Portrait");
        renderReaderMenu(renderer);
        break;

#ifdef BOARD_TYPE_M5_PAPER
      case ADV_WIFI_UPLOAD:
        if (wifi_uploader_is_running())
          stop_wifi_uploader(renderer, true);
        else
          start_wifi_uploader(renderer);
        renderReaderMenu(renderer);
        break;
#endif

      case ADV_SAVE_BACK:
        // Save & Back
        save_app_settings(renderer);
        reader_menu_advanced = false;
        reader_menu_selected = 0;
        renderReaderMenu(renderer);
        break;

      default:
        break;
      }
    }
    break;

  case NONE:
  default:
    renderReaderMenu(renderer);
    break;
  }
}
