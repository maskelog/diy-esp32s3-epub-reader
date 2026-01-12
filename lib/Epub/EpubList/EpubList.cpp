#include "EpubList.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <SPI.h>
#include <SD.h>
#include "../ZipFile/ZipFile.h"

#ifndef UNIT_TEST
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <esp_log.h>
  #include <esp_task_wdt.h>
#else
  #define vTaskDelay(t)
  #define ESP_LOGE(args...)
  #define ESP_LOGI(args...)
  #define ESP_LOGD(args...)
#endif

static const char *TAG = "PUBLIST";

#define PADDING 20
#define EPUBS_PER_PAGE 5
static const size_t kMaxCoverBytes = 600 * 1024;

static bool render_cover_image(Renderer *renderer, const EpubListItem &item, int x, int y, int width, int height)
{
  if (!renderer || width <= 0 || height <= 0)
  {
    return false;
  }
  if (item.cover_path[0] == '\0')
  {
    return false;
  }
#ifndef UNIT_TEST
  struct CoverRenderContext
  {
    Renderer *renderer;
    std::string epub_path;
    std::string cover_path;
    int x;
    int y;
    int w;
    int h;
    bool ok;
    SemaphoreHandle_t done;
  };
  CoverRenderContext ctx = {};
  ctx.renderer = renderer;
  ctx.epub_path = item.path;
  ctx.cover_path = item.cover_path;
  ctx.x = x;
  ctx.y = y;
  ctx.w = width;
  ctx.h = height;
  ctx.ok = false;
  ctx.done = xSemaphoreCreateBinary();
  if (!ctx.done)
  {
    return false;
  }
  auto task_fn = [](void *param) {
    CoverRenderContext *ctx = static_cast<CoverRenderContext *>(param);
    ZipFile zip(ctx->epub_path.c_str());
    size_t cover_size = 0;
    if (zip.get_file_uncompressed_size(ctx->cover_path.c_str(), &cover_size) &&
        cover_size > 0 && cover_size <= kMaxCoverBytes)
    {
      size_t data_size = 0;
      uint8_t *data = zip.read_file_to_memory(ctx->cover_path.c_str(), &data_size);
      if (data && data_size > 0)
      {
        int img_w = 0;
        int img_h = 0;
        bool can_render = ctx->renderer->get_image_size(ctx->cover_path, data, data_size, &img_w, &img_h);
        if (can_render && img_w > 0 && img_h > 0 && ctx->w > 0 && ctx->h > 0)
        {
          float scale_w = static_cast<float>(ctx->w) / static_cast<float>(img_w);
          float scale_h = static_cast<float>(ctx->h) / static_cast<float>(img_h);
          float scale = std::min(scale_w, scale_h);
          int draw_w = std::max(1, static_cast<int>(img_w * scale));
          int draw_h = std::max(1, static_cast<int>(img_h * scale));
          int draw_x = ctx->x + (ctx->w - draw_w) / 2;
          int draw_y = ctx->y + (ctx->h - draw_h) / 2;
          draw_x += ctx->renderer->get_margin_left();
          draw_y += ctx->renderer->get_margin_top();
          ctx->renderer->set_image_placeholder_enabled(false);
          ctx->renderer->draw_image(ctx->cover_path, data, data_size, draw_x, draw_y, draw_w, draw_h);
          ctx->renderer->set_image_placeholder_enabled(true);
          ctx->ok = true;
        }
        free(data);
      }
    }
    xSemaphoreGive(ctx->done);
    vTaskDelete(nullptr);
  };
  const uint32_t stack_words = static_cast<uint32_t>((64 * 1024) / sizeof(StackType_t));
  BaseType_t ok = xTaskCreatePinnedToCore(task_fn, "cover_draw", stack_words, &ctx, 2, nullptr, 1);
  if (ok != pdPASS)
  {
    vSemaphoreDelete(ctx.done);
    return false;
  }
  xSemaphoreTake(ctx.done, portMAX_DELAY);
  vSemaphoreDelete(ctx.done);
  return ctx.ok;
#else
  (void)x;
  (void)y;
  (void)width;
  (void)height;
  return false;
#endif
}

static void draw_cover_dots(Renderer *renderer, const char *title, int x, int y, int width, int height)
{
  if (!renderer || width <= 0 || height <= 0)
  {
    return;
  }
  renderer->fill_rect(x, y, width, height, 255);
  renderer->draw_rect(x, y, width, height, 0);

  unsigned int hash = 2166136261u;
  if (title)
  {
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(title); *p; ++p)
    {
      hash ^= *p;
      hash *= 16777619u;
    }
  }

  int grid = 4;
  int cell_w = width / grid;
  int cell_h = height / grid;
  int size = std::min(cell_w, cell_h) / 2;
  if (size < 2)
  {
    size = 2;
  }

  for (int gy = 0; gy < grid; ++gy)
  {
    for (int gx = 0; gx < grid; ++gx)
    {
      int bit = (gy * grid + gx) & 31;
      bool on = ((hash >> bit) & 0x1) != 0;
      if (!on)
      {
        continue;
      }
      int cx = x + gx * cell_w + (cell_w - size) / 2;
      int cy = y + gy * cell_h + (cell_h - size) / 2;
      renderer->fill_rect(cx, cy, size, size, 0);
    }
  }
}

void EpubList::next()
{
  if (state.num_epubs == 0)
  {
    return;
  }
  state.selected_item = (state.selected_item + 1) % state.num_epubs;
}

void EpubList::prev()
{
  if (state.num_epubs == 0)
  {
    return;
  }
  state.selected_item = (state.selected_item - 1 + state.num_epubs) % state.num_epubs;
}

bool EpubList::load(const char *path)
{
  if (state.is_loaded)
  {
    ESP_LOGD(TAG, "Already loaded books");
    return true;
  }
  if (SD.cardType() == CARD_NONE)
  {
    ESP_LOGE(TAG, "SD card not mounted; skipping library load");
    return false;
  }
  // Use an 8.3-safe filename so the FAT implementation can always create it.
  const char *index_path = "/Books/BOOKS.IDX";
  if (load_index(path, index_path))
  {
    ESP_LOGI(TAG, "Loaded EPUB index from %s", index_path);
    state.previous_rendered_page = -1;
    state.previous_selected_item = -1;
    if (m_title_blocks.size() < static_cast<size_t>(state.num_epubs))
    {
      m_title_blocks.resize(state.num_epubs, nullptr);
    }
#ifndef UNIT_TEST
    esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    bool was_subscribed = (wdt_err == ESP_OK);
#endif
    bool updated = false;
    for (int i = 0; i < state.num_epubs; ++i)
    {
      if (state.epub_list[i].cover_path[0] != '\0')
      {
        continue;
      }
      vTaskDelay(5);
      Epub epub(state.epub_list[i].path);
      if (!epub.load_with_task(64 * 1024))
      {
        continue;
      }
      const std::string &cover_item = epub.get_cover_image_item();
      if (!cover_item.empty())
      {
        strncpy(state.epub_list[i].cover_path, cover_item.c_str(), MAX_PATH_SIZE);
        state.epub_list[i].cover_path[MAX_PATH_SIZE - 1] = '\0';
        updated = true;
      }
    }
    if (updated)
    {
      save_index(index_path);
    }
#ifndef UNIT_TEST
    if (was_subscribed)
    {
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
#endif
    return true;
  }
  else
  {
    // If the index is corrupt or from an unsupported version, remove it
    // so a fresh scan can rebuild it cleanly.
    SD.remove(index_path);
  }
  
#ifndef UNIT_TEST
  // Remove from watchdog for entire load process (can take many seconds)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);
  ESP_LOGI(TAG, "Watchdog disabled for library load");
#endif
  
  renderer->show_busy();
  vTaskDelay(50); // Allow display update
  // trigger a proper redraw
  state.previous_rendered_page = -1;
  // read in the list of epubs
  state.num_epubs = 0;
  // normalise the base path for epub files so we can support
  // directories like /fs/Books as well as the root /fs/.
  std::string base_path = path;
  if (!base_path.empty() && base_path.back() != '/')
  {
    base_path += "/";
  }
  File dir = SD.open(path);
  File file;
  while ((file = dir.openNextFile()))
  {
    ESP_LOGD(TAG, "Found file: %s", file.name());
    // ignore any hidden files starting with "." and any directories
    // Also ignore files starting with '_' which on macOS FAT volumes are
    // typically AppleDouble metadata (e.g. "_THEGR~6.EPU").
    if (file.name()[0] == '.' || file.name()[0] == '_' || file.isDirectory())
    {
      continue;
    }

    const char *dot = strrchr(file.name(), '.');
      if (!dot || !dot[1])
      {
        continue;
      }
      const char *ext = dot + 1;
      // Accept "epub" and also the 8.3 short-name variant "epu" (case-insensitive)
      char e0 = tolower(ext[0]);
      char e1 = tolower(ext[1]);
      char e2 = tolower(ext[2]);
      char e3 = tolower(ext[3]);
      bool is_epu = (e0 == 'e' && e1 == 'p' && e2 == 'u' && (ext[3] == '\0'));
      bool is_epub = (e0 == 'e' && e1 == 'p' && e2 == 'u' && e3 == 'b' && ext[4] == '\0');
      if (!is_epu && !is_epub)
      {
        continue;
      }
      ESP_LOGE(TAG, ">>> Loading epub %d: %s", state.num_epubs + 1, file.name());

      // Yield before loading each EPUB to prevent watchdog timeout
      vTaskDelay(10);

      ESP_LOGE(TAG, ">>> Creating Epub object");
      Epub *epub = new Epub(base_path + file.name());
      ESP_LOGE(TAG, ">>> Calling epub->load_with_task()");
      if (epub->load_with_task(64 * 1024))
      {
        ESP_LOGE(TAG, "  EPUB loaded successfully: %s", file.name());
        strncpy(state.epub_list[state.num_epubs].path, epub->get_path().c_str(), MAX_PATH_SIZE);
        strncpy(state.epub_list[state.num_epubs].title, replace_html_entities(epub->get_title()).c_str(), MAX_TITLE_SIZE);
        const std::string &cover_item = epub->get_cover_image_item();
        if (!cover_item.empty())
        {
          strncpy(state.epub_list[state.num_epubs].cover_path, cover_item.c_str(), MAX_PATH_SIZE);
          state.epub_list[state.num_epubs].cover_path[MAX_PATH_SIZE - 1] = '\0';
        }
        else
        {
          state.epub_list[state.num_epubs].cover_path[0] = '\0';
        }
        state.num_epubs++;
        if (state.num_epubs == MAX_EPUB_LIST_SIZE)
        {
          ESP_LOGE(TAG, "Too many epubs, max is %d", MAX_EPUB_LIST_SIZE);
          break;
        }
      }
      else
      {
        ESP_LOGE(TAG, "Failed to load epub %s", file.name());
      }
      delete epub;

      // Yield to watchdog after processing each EPUB to prevent timeouts
      vTaskDelay(1);
    }
    dir.close();
    std::sort(
        state.epub_list,
        state.epub_list + state.num_epubs,
        [](const EpubListItem &a, const EpubListItem &b)
        {
          return strcmp(a.title, b.title) < 0;
        });
  // sanity check our state
  if (state.selected_item >= state.num_epubs)
  {
    state.selected_item = 0;
    state.previous_rendered_page = -1;
    state.previous_selected_item = -1;
  }
  ESP_LOGI(TAG, "Loaded %d EPUBs from %s", state.num_epubs, path);
  state.is_loaded = true;
  // prepare title layout cache
  // ensure the title blocks vector has an entry for each epub; layout will
  // be performed lazily on first render for each item.
  // (We avoid storing pointers in RTC state; this cache is per-session only.)
  // Note: MAX_EPUB_LIST_SIZE is small (20), so this is safe.
  extern EpubListState epub_list_state; // declaration elsewhere; we use state directly here
  (void)epub_list_state;
  // ensure capacity but leave pointers null for lazy init
  // m_title_blocks is a member of EpubList; here we just size it
  // to match the number of epubs.
  // Any existing blocks (if reload ever happens) are left as-is for indices < num_epubs.
  if (m_title_blocks.size() < static_cast<size_t>(state.num_epubs))
  {
    m_title_blocks.resize(state.num_epubs, nullptr);
  }
  save_index(index_path);
  
#ifndef UNIT_TEST
  // Re-add to watchdog after load completes
  if (was_subscribed)
  {
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "Watchdog re-enabled after library load");
  }
#endif
  
  return true;
}

void EpubList::render()
{
  ESP_LOGE(TAG, ">>> EpubList::render() START, num_epubs=%d, selected=%d", state.num_epubs, state.selected_item);
  
#ifndef UNIT_TEST
  // Remove from watchdog during render (image loading can be slow)
  esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  bool was_subscribed = (wdt_err == ESP_OK);
#endif
  
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  if (page_width <= 0 || page_height <= 0)
  {
#ifndef UNIT_TEST
    if (was_subscribed) esp_task_wdt_add(xTaskGetCurrentTaskHandle());
#endif
    return;
  }

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int content_height = page_height;
  if (bottom_bar_height > 0 && bottom_bar_height < page_height)
  {
    content_height = page_height - bottom_bar_height;
  }

  int items_per_page = state.use_grid_view ? (EPUB_GRID_ROWS * EPUB_GRID_COLUMNS) : EPUB_LIST_ITEMS_PER_PAGE;
  if (items_per_page <= 0)
  {
    items_per_page = 1;
  }

  // what page are we on?
  int current_page = 0;
  if (state.num_epubs > 0)
  {
    current_page = state.selected_item / items_per_page;
  }
  // draw a page of epubs
  ESP_LOGD(TAG, "Current page is %d, previous page %d, redraw=%d", current_page, state.previous_rendered_page, m_needs_redraw);
  // starting a fresh page or rendering from scratch?
  if (current_page != state.previous_rendered_page || m_needs_redraw)
  {
    m_needs_redraw = false;
    renderer->clear_screen();
    state.previous_selected_item = -1;
    // trigger a redraw of the items
    state.previous_rendered_page = -1;
  }

  int start_index = current_page * items_per_page;

  if (m_title_blocks.size() < static_cast<size_t>(state.num_epubs))
  {
    m_title_blocks.resize(state.num_epubs, nullptr);
  }

  if (state.use_grid_view)
  {
    int cell_width = page_width / EPUB_GRID_COLUMNS;
    int cell_height = content_height / EPUB_GRID_ROWS;
    ESP_LOGI(TAG, "Grid cell size is %dx%d, rendering page %d (items %d-%d)",
             cell_width, cell_height, current_page, start_index,
             std::min(start_index + items_per_page - 1, state.num_epubs - 1));

    // Yield before starting to render grid
    vTaskDelay(20);

    for (int i = start_index; i < start_index + items_per_page && i < state.num_epubs; i++)
    {
      int index_in_page = i - start_index;
      int row = index_in_page / EPUB_GRID_COLUMNS;
      int col = index_in_page % EPUB_GRID_COLUMNS;
      int cell_x = col * cell_width;
      int cell_y = row * cell_height;

      // do we need to draw a new page of items?
      if (current_page != state.previous_rendered_page)
      {
        ESP_LOGE(TAG, "Grid: Rendering item %d/%d: %s", i + 1, state.num_epubs, state.epub_list[i].title);

        // Yield before loading each EPUB's cover to prevent watchdog timeout
        vTaskDelay(50);

        int available_height = cell_height - PADDING * 2;
        if (available_height < 1)
        {
          available_height = 1;
        }
        int image_height = available_height;
        int image_width = 2 * image_height / 3;
        if (image_width > cell_width - PADDING * 2)
        {
          image_width = cell_width - PADDING * 2;
        }
        int image_xpos = cell_x + (cell_width - image_width) / 2;
        int image_ypos = cell_y + (cell_height - image_height) / 2;

        bool drew_cover = render_cover_image(renderer, state.epub_list[i], image_xpos, image_ypos, image_width, image_height);
        if (!drew_cover)
        {
          draw_cover_dots(renderer, state.epub_list[i].title, image_xpos, image_ypos, image_width, image_height);
        }

        // Yield after rendering each item
        vTaskDelay(10);

        ESP_LOGI(TAG, "  Item %d rendered", i + 1);
      }

      int sel_x = cell_x + 2;
      int sel_y = cell_y + 2;
      int sel_w = cell_width - 4;
      int sel_h = cell_height - 4;
      if (sel_w < 0)
      {
        sel_w = 0;
      }
      if (sel_h < 0)
      {
        sel_h = 0;
      }

      if (state.previous_selected_item == i)
      {
        for (int line = 0; line < 3; line++)
        {
          renderer->draw_rect(sel_x + line, sel_y + line, sel_w - 2 * line, sel_h - 2 * line, 255);
        }
      }
      if (state.selected_item == i)
      {
        for (int line = 0; line < 3; line++)
        {
          renderer->draw_rect(sel_x + line, sel_y + line, sel_w - 2 * line, sel_h - 2 * line, 0);
        }
      }
    }
  }
  else
  {
    int cell_height = content_height / EPUB_LIST_ITEMS_PER_PAGE;
    ESP_LOGI(TAG, "List cell height is %d, rendering page %d (items %d-%d)",
             cell_height, current_page, start_index,
             std::min(start_index + EPUB_LIST_ITEMS_PER_PAGE - 1, state.num_epubs - 1));

    // Yield before starting to render list
    vTaskDelay(20);

    int ypos = 0;
    for (int i = start_index; i < start_index + EPUB_LIST_ITEMS_PER_PAGE && i < state.num_epubs; i++)
    {
      // do we need to draw a new page of items?
      if (current_page != state.previous_rendered_page)
      {
        ESP_LOGE(TAG, "List: Rendering item %d/%d: %s", i + 1, state.num_epubs, state.epub_list[i].title);

        // Yield before loading each EPUB's cover to prevent watchdog timeout
        vTaskDelay(50);

        // draw the cover page area (now just title card)
        int image_xpos = PADDING;
        int image_ypos = ypos + PADDING;
        int image_height = cell_height - PADDING * 2;
        int image_width = 2 * image_height / 3;

        bool drew_cover = render_cover_image(renderer, state.epub_list[i], image_xpos, image_ypos, image_width, image_height);
        if (!drew_cover)
        {
          draw_cover_dots(renderer, state.epub_list[i].title, image_xpos, image_ypos, image_width, image_height);
        }
        // draw the title
        int text_xpos = image_xpos + image_width + PADDING;
        int text_ypos = ypos + PADDING / 2;
        int text_width = page_width - (text_xpos + PADDING);
        int text_height = cell_height - PADDING * 2;
        // use the text block to layout the title
        TextBlock *title_block = m_title_blocks[i];
        if (!title_block)
        {
          title_block = new TextBlock(LEFT_ALIGN);
          // Render library titles in bold in the list view.
          title_block->add_span(state.epub_list[i].title, true, false);
          title_block->layout(renderer, nullptr, text_width);
          m_title_blocks[i] = title_block;
        }
        // work out the height of the title
        int title_height = title_block->line_breaks.size() * renderer->get_line_height();
        // center the title in the cell
        int y_offset = title_height < text_height ? (text_height - title_height) / 2 : 0;
        // draw each line of the title making sure we don't run over the cell
        for (int li = 0; li < title_block->line_breaks.size() && y_offset + renderer->get_line_height() < text_height; li++)
        {
          title_block->render(renderer, li, text_xpos, text_ypos + y_offset);
          y_offset += renderer->get_line_height();
        }
        // Yield between list items to keep the watchdog happy when
        // rendering many large covers and titles.
        vTaskDelay(1);
      }
      // clear the selection box around the previous selected item
      if (state.previous_selected_item == i)
      {
        for (int line = 0; line < 5; line++)
        {
          renderer->draw_rect(line, ypos + PADDING / 2 + line, page_width - 2 * line, cell_height - PADDING - 2 * line, 255);
        }
      }
      // draw the selection box around the current selection
      if (state.selected_item == i)
      {
        for (int line = 0; line < 5; line++)
        {
          renderer->draw_rect(line, ypos + PADDING / 2 + line, page_width - 2 * line, cell_height - PADDING - 2 * line, 0);
        }
      }
      ypos += cell_height;
    }
  }

  // draw bottom navigation / toggle bar
  if (bottom_bar_height > 0 && bottom_bar_height <= page_height)
  {
    int bar_y = page_height - bottom_bar_height;
    renderer->fill_rect(0, bar_y, page_width, bottom_bar_height, 255);
    int center_x = page_width / 2;
    int center_y = bar_y + bottom_bar_height / 2;

    int total_pages = 1;
    if (state.num_epubs > 0 && items_per_page > 0)
    {
      total_pages = (state.num_epubs + items_per_page - 1) / items_per_page;
    }
    if (total_pages < 1)
    {
      total_pages = 1;
    }
    int page_display = current_page + 1;
    if (page_display < 1)
    {
      page_display = 1;
    }
    if (page_display > total_pages)
    {
      page_display = total_pages;
    }

    // Layout "<<      <      Page X of Y      >      >>" so that the
    // arrow groups are bold with ~50% more spacing between single and
    // double arrows, while the "Page X of Y" text remains normal.
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
    if (w_ld < 0) w_ld = 0;
    if (w_ls < 0) w_ls = 0;
    if (w_center < 0) w_center = 0;
    if (w_rs < 0) w_rs = 0;
    if (w_rd < 0) w_rd = 0;

    int space_w = renderer->get_space_width();
    if (space_w < 0)
    {
      space_w = 0;
    }
    // Use 6 spaces (~50% more than the previous 4-space gaps).
    int gap_arrows = space_w * 6;
    int gap_center = space_w * 6;

    // Divide the full page width into five equal navigation regions
    // that span the entire bottom bar: [<<] [<] [X / Y] [>] [>>]. Place
    // the text baseline slightly below geometric center to account for
    // how epdiy bitmap glyphs sit relative to the baseline.
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
    int rd_zone_end = page_width; // consume any remainder to reach the edge

    int bar_top = bar_y;
    int bar_bottom = bar_y + bottom_bar_height;
    int bar_height = bar_bottom - bar_top;
    if (bar_height < renderer->get_line_height() + 4)
    {
      bar_height = renderer->get_line_height() + 4;
    }

    auto draw_nav_box = [&](int x0, int x1) {
      if (x1 <= x0)
      {
        return;
      }
      int box_x = x0;
      int box_w = x1 - x0;
      int box_y = bar_top + 2;
      int box_h = bar_height - 4;
      if (box_h <= 0)
      {
        box_h = bar_height;
      }
      renderer->draw_rect(box_x, box_y, box_w, box_h, 0);
    };

    // Outline each interactive region so the user can see the
    // navigation buttons that correspond to the touch zones.
    draw_nav_box(ld_zone_start, ld_zone_end);
    draw_nav_box(ls_zone_start, ls_zone_end);
    draw_nav_box(center_zone_start, center_zone_end);
    draw_nav_box(rs_zone_start, rs_zone_end);
    draw_nav_box(rd_zone_start, rd_zone_end);

    // Center each label within its corresponding region.
    auto center_label_x = [](int zone_start, int zone_end, int text_width) {
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

    // Draw each segment: bold arrows, normal "Page X of Y".
    renderer->draw_text(x_ld, label_y, left_double, true, false);
    renderer->draw_text(x_ls, label_y, left_single, true, false);
    renderer->draw_text(x_center, label_y, center, false, false);
    renderer->draw_text(x_rs, label_y, right_single, true, false);
    renderer->draw_text(x_rd, label_y, right_double, true, false);
  }

  state.previous_selected_item = state.selected_item;
  state.previous_rendered_page = current_page;

  ESP_LOGE(TAG, "<<< EpubList::render() END, page=%d", current_page);
  
#ifndef UNIT_TEST
  // Re-add to watchdog after render completes
  if (was_subscribed)
  {
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
  }
#endif
}

// Binary index format:
// uint32_t magic ('EBIX'), uint16_t version (1), uint16_t count,
// followed by count EpubListItem records.
bool EpubList::load_index(const char *books_path, const char *index_path)
{
  File fp = SD.open(index_path, FILE_READ);
  if (!fp)
  {
    return false;
  }
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t count = 0;
  if (fp.read((uint8_t *)&magic, sizeof(magic)) != sizeof(magic) ||
      fp.read((uint8_t *)&version, sizeof(version)) != sizeof(version) ||
      fp.read((uint8_t *)&count, sizeof(count)) != sizeof(count))
  {
    fp.close();
    return false;
  }
  const uint32_t EXPECTED_MAGIC = 0x58494245; // 'EBIX'
  if (magic != EXPECTED_MAGIC || (version != 1 && version != 2) || count == 0 || count > MAX_EPUB_LIST_SIZE)
  {
    fp.close();
    return false;
  }
  if (version == 1)
  {
    struct EpubListItemV1
    {
      char path[MAX_PATH_SIZE];
      char title[MAX_TITLE_SIZE];
      uint16_t current_section;
      uint16_t current_page;
      uint16_t pages_in_current_section;
      char cover_path[MAX_PATH_SIZE];
    };
    EpubListItemV1 *tmp = static_cast<EpubListItemV1 *>(
        malloc(sizeof(EpubListItemV1) * count));
    if (!tmp)
    {
      fp.close();
      return false;
    }
    if (fp.read((uint8_t *)tmp, sizeof(EpubListItemV1) * count) != sizeof(EpubListItemV1) * count)
    {
      free(tmp);
      fp.close();
      return false;
    }
    for (int i = 0; i < count; i++)
    {
      EpubListItem &dst = state.epub_list[i];
      memset(&dst, 0, sizeof(dst));
      memcpy(dst.path, tmp[i].path, MAX_PATH_SIZE);
      memcpy(dst.title, tmp[i].title, MAX_TITLE_SIZE);
      dst.current_section = tmp[i].current_section;
      dst.current_page = tmp[i].current_page;
      dst.pages_in_current_section = tmp[i].pages_in_current_section;
      memcpy(dst.cover_path, tmp[i].cover_path, MAX_PATH_SIZE);
      dst.bookmark_set = false;
      dst.bookmark_section = 0;
      dst.bookmark_page = 0;
    }
    free(tmp);
  }
  else
  {
    if (fp.read((uint8_t *)state.epub_list, sizeof(EpubListItem) * count) != sizeof(EpubListItem) * count)
    {
      fp.close();
      return false;
    }
  }
  fp.close();

  // Quick validation: count EPUB-like files in books_path via SD.h to avoid dirent stack use.
  int dir_count = 0;
  File dir = SD.open(books_path);
  if (dir)
  {
    File f;
    while ((f = dir.openNextFile()))
    {
      if (f.isDirectory())
      {
        f.close();
        continue;
      }
      const char *name = f.name();
      if (!name || name[0] == '.' || name[0] == '_')
      {
        f.close();
        continue;
      }
      const char *dot = strrchr(name, '.');
      if (!dot || !dot[1])
      {
        f.close();
        continue;
      }
      const char *ext = dot + 1;
      char e0 = tolower(ext[0]);
      char e1 = tolower(ext[1]);
      char e2 = tolower(ext[2]);
      char e3 = tolower(ext[3]);
      bool is_epu = (e0 == 'e' && e1 == 'p' && e2 == 'u' && (ext[3] == '\0'));
      bool is_epub = (e0 == 'e' && e1 == 'p' && e2 == 'u' && e3 == 'b' && ext[4] == '\0');
      if (is_epu || is_epub)
      {
        dir_count++;
      }
      f.close();
    }
    dir.close();
  }
  if (dir_count != count)
  {
    return false;
  }

  state.num_epubs = count;
  state.is_loaded = true;
  state.previous_rendered_page = -1;
  state.previous_selected_item = -1;
  if (state.selected_item >= state.num_epubs)
  {
    state.selected_item = 0;
  }
  return true;
}

void EpubList::save_index(const char *index_path)
{
  File fp = SD.open(index_path, FILE_WRITE);
  if (!fp)
  {
    ESP_LOGE(TAG, "Failed to open index file %s for write", index_path);
    return;
  }
  const uint32_t magic = 0x58494245; // 'EBIX'
  const uint16_t version = 2;
  uint16_t count = static_cast<uint16_t>(state.num_epubs);
  if (fp.write((const uint8_t *)&magic, sizeof(magic)) != sizeof(magic) ||
      fp.write((const uint8_t *)&version, sizeof(version)) != sizeof(version) ||
      fp.write((const uint8_t *)&count, sizeof(count)) != sizeof(count))
  {
    ESP_LOGE(TAG, "Failed to write index header to %s", index_path);
    fp.close();
    return;
  }
  if (count > 0)
  {
    if (fp.write((uint8_t *)state.epub_list, sizeof(EpubListItem) * count) != sizeof(EpubListItem) * count)
    {
      ESP_LOGE(TAG, "Failed to write index entries to %s", index_path);
    }
  }
  fp.close();
}
