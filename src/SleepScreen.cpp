#include "SleepScreen.h"
#include "AppSettings.h"
#include "AppState.h"
#include "Renderer/Renderer.h"
#include "ZipFile/ZipFile.h"
#include "TaskWdtGuard.h"
#include <esp_log.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>

#ifdef BOARD_TYPE_M5_PAPER
#include <M5Unified.h>
#include <SD.h>
#endif

static const char *TAG = "SleepScreen";

// Maximum size of a book cover image loaded for the sleep screen.
static const size_t kMaxSleepCoverBytes = 600 * 1024;

#ifdef BOARD_TYPE_M5_PAPER
// Full e-ink "flash clear" (white then black) to remove ghosting before
// drawing the sleep image.
static void epd_flash_clear()
{
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.display();
  M5.Display.waitDisplay();

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.display();
  M5.Display.waitDisplay();
}
#endif

// ── Path helper ───────────────────────────────────────────────────────────

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
          if (!components.empty()) components.pop_back();
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
  if (!component.empty() && component != "." && component != "..")
    components.push_back(component);

  std::string result;
  for (size_t i = 0; i < components.size(); ++i)
  {
    if (i > 0) result += "/";
    result += components[i];
  }
  return result;
}

// ── find_last_open_book_index (local helper needed by show_sleep_cover) ───
// Returns the index of the most-recently-read epub, or -1 if none found.
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
  if (last_index >= 0) return last_index;

  // Fallback: any book that has been laid out at least once.
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    if (epub_list_state.epub_list[i].pages_in_current_section > 0)
      return i;
  }
  return -1;
}

// ── show_sleep_cover ─────────────────────────────────────────────────────

static void show_sleep_cover(Renderer *renderer)
{
  // Suspend watchdog subscription while loading/drawing the cover; restored
  // automatically on every return path.
  TaskWdtGuard wdt_guard;

  int book_index = -1;
  if (epub_list_state.num_epubs > 0)
  {
    if (ui_state == READING_EPUB || ui_state == READING_MENU ||
        ui_state == SELECTING_TABLE_CONTENTS)
      book_index = epub_list_state.selected_item;
    if (book_index < 0)
      book_index = find_last_open_book_index();
  }

  if (book_index < 0)
  {
    return;
  }

  EpubListItem &item = epub_list_state.epub_list[book_index];
  if (item.cover_path[0] == '\0')
  {
    return;
  }
  ESP_LOGE(TAG, "Sleep cover path: %s", item.cover_path);

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
  ctx.epub_path  = item.path;
  ctx.cover_path = normalise_path_for_zip(item.cover_path);
  ctx.data = nullptr;
  ctx.size = 0;
  ctx.ok   = false;
  ctx.done = xSemaphoreCreateBinary();
  if (!ctx.done)
  {
    return;
  }

  auto task_fn = [](void *param) {
    SleepCoverContext *ctx = static_cast<SleepCoverContext *>(param);
    ESP_LOGE("SleepScreen", "Sleep cover task: opening EPUB: %s", ctx->epub_path.c_str());
    ZipFile zip(ctx->epub_path.c_str());
    ESP_LOGE("SleepScreen", "Sleep cover task: looking for: %s", ctx->cover_path.c_str());
    size_t cover_size = 0;
    bool size_ok = zip.get_file_uncompressed_size(ctx->cover_path.c_str(), &cover_size);
    ESP_LOGE("SleepScreen", "Sleep cover task: size_ok=%d, size=%zu", size_ok ? 1 : 0, cover_size);
    if (size_ok && cover_size > 0 && cover_size <= kMaxSleepCoverBytes)
    {
      size_t image_data_size = 0;
      uint8_t *image_data = zip.read_file_to_memory(ctx->cover_path.c_str(), &image_data_size);
      if (image_data && image_data_size > 0)
      {
        ctx->data = image_data;
        ctx->size = image_data_size;
        ctx->ok   = true;
        ESP_LOGE("SleepScreen", "Sleep cover task: SUCCESS");
      }
      else
      {
        ESP_LOGE("SleepScreen", "Sleep cover task: FAILED to read image data");
      }
    }
    else
    {
      ESP_LOGE("SleepScreen", "Sleep cover task: FAILED size check (size=%zu, limit=%zu)", cover_size, kMaxSleepCoverBytes);
    }
    xSemaphoreGive(ctx->done);
    vTaskDelete(nullptr);
  };

  const uint32_t stack_words = static_cast<uint32_t>((48 * 1024) / sizeof(StackType_t));
  ESP_LOGE(TAG, "Sleep cover: creating task, stack=%u words", stack_words);
  BaseType_t ok = xTaskCreatePinnedToCore(task_fn, "sleep_cover", stack_words, &ctx, 2, nullptr, 1);
  if (ok != pdPASS)
  {
    ESP_LOGE(TAG, "Sleep cover: FAILED to create task");
    vSemaphoreDelete(ctx.done);
    return;
  }

  xSemaphoreTake(ctx.done, portMAX_DELAY);
  vSemaphoreDelete(ctx.done);
  ESP_LOGE(TAG, "Sleep cover: task completed");

  if (ctx.ok && ctx.data && ctx.size > 0)
  {
    ESP_LOGE(TAG, "Sleep cover bytes: %zu", ctx.size);

#ifdef BOARD_TYPE_M5_PAPER
    epd_flash_clear();

    bool is_jpeg = (ctx.size > 2 && ctx.data[0] == 0xFF && ctx.data[1] == 0xD8);
    bool is_png  = (ctx.size > 8 && ctx.data[0] == 0x89 && ctx.data[1] == 0x50 &&
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
      ESP_LOGW(TAG, "Sleep cover: unknown image format");
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
    int img_w = 0, img_h = 0;
    bool can_render = renderer->get_image_size(ctx.cover_path, ctx.data, ctx.size, &img_w, &img_h);
    if (can_render && img_w > 0 && img_h > 0)
    {
      renderer->set_margin_top(0);
      renderer->set_margin_bottom(0);
      renderer->set_margin_left(0);
      renderer->set_margin_right(0);
      renderer->clear_screen();
      renderer->set_image_placeholder_enabled(false);
      renderer->draw_image(ctx.cover_path, ctx.data, ctx.size, 0, 0,
                           renderer->get_page_width(), renderer->get_page_height());
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

  if (ctx.data) free(ctx.data);
}

// ── show_sleep_image ──────────────────────────────────────────────────────

void show_sleep_image(Renderer *renderer)
{
  ESP_LOGE(TAG, "show_sleep_image() mode=%d (0=Cover,1=Random,2=Custom,3=Off)", (int)sleep_image_mode);

  if (sleep_image_mode == SLEEP_IMAGE_OFF)
  {
    ESP_LOGE(TAG, "Sleep image OFF, skipping");
    return;
  }

  if (sleep_image_mode == SLEEP_IMAGE_COVER)
  {
    ESP_LOGE(TAG, "Sleep image COVER mode");
    show_sleep_cover(renderer);
    return;
  }

  // ── Custom image from SD ──────────────────────────────────────────────
  if (sleep_image_mode == SLEEP_IMAGE_CUSTOM)
  {
    const char *sleep_image_path = "/Sleep/bg.png";
    ESP_LOGE(TAG, "Displaying custom sleep image from SD: %s", sleep_image_path);

    // Suspend watchdog subscription while loading/drawing the custom image;
    // restored automatically on every return path.
    TaskWdtGuard wdt_guard;

#ifdef BOARD_TYPE_M5_PAPER
    if (!SD.exists(sleep_image_path))
    {
      ESP_LOGW(TAG, "Sleep image not found at %s, falling back to cover", sleep_image_path);
      show_sleep_cover(renderer);
      return;
    }

    epd_flash_clear();

    File sleep_fp = SD.open(sleep_image_path, FILE_READ);
    if (!sleep_fp || sleep_fp.size() == 0)
    {
      if (sleep_fp) sleep_fp.close();
      ESP_LOGW(TAG, "Failed to open sleep image: %s", sleep_image_path);
      show_sleep_cover(renderer);
      return;
    }
    size_t sleep_size = sleep_fp.size();

    // Read into heap — drawPng() from memory avoids the DataWrapper streaming API
    // limitation that prevents drawPngFile() from being used in separate TUs.
    uint8_t *sleep_data = (uint8_t *)malloc(sleep_size);
    if (!sleep_data)
    {
      sleep_fp.close();
      ESP_LOGW(TAG, "OOM for sleep image (%zu bytes)", sleep_size);
      show_sleep_cover(renderer);
      return;
    }
    sleep_fp.read(sleep_data, sleep_size);
    sleep_fp.close();

    bool success = M5.Display.drawPng(sleep_data, sleep_size, 0, 0,
                                      M5.Display.width(), M5.Display.height(),
                                      0, 0, 0, 0, datum_t::middle_center);
    free(sleep_data);
    M5.Display.display();
    M5.Display.waitDisplay();
    ESP_LOGE(TAG, "drawPng result: %s", success ? "SUCCESS" : "FAILED");
    if (!success)
    {
      ESP_LOGW(TAG, "Failed to draw custom sleep image, falling back to cover");
      show_sleep_cover(renderer);
    }
#endif
    return;
  }

  // ── Random image from /sd/pic ─────────────────────────────────────────
  if (sleep_image_mode != SLEEP_IMAGE_RANDOM)
    return;

  // Suspend watchdog subscription while picking/drawing a random image;
  // restored automatically on every return path.
  TaskWdtGuard wdt_guard;

  const char *pics_dir = "/sd/pic";
  DIR *dir = opendir(pics_dir);
  if (!dir)
  {
    ESP_LOGW(TAG, "Sleep image directory not found");
    show_sleep_cover(renderer);
    return;
  }

  char selected_path[512] = {0};
  int image_count = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL)
  {
    if (ent->d_name[0] == '.' || ent->d_type == DT_DIR) continue;

    const char *dot = strrchr(ent->d_name, '.');
    if (!dot || !dot[1]) continue;
    const char *ext = dot + 1;
    char e0 = tolower(ext[0]), e1 = tolower(ext[1]), e2 = tolower(ext[2]), e3 = tolower(ext[3]);

    bool is_jpg  = (e0=='j' && e1=='p' && e2=='g' && ext[3]=='\0');
    bool is_jpeg = (e0=='j' && e1=='p' && e2=='e' && e3=='g' && ext[4]=='\0');
    bool is_png  = (e0=='p' && e1=='n' && e2=='g' && ext[3]=='\0');
    if (!is_jpg && !is_jpeg && !is_png) continue;

    image_count++;
    if (image_count == 1)
    {
      snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
    }
    else
    {
      if (esp_random() % (uint32_t)image_count == 0)
        snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
    }
    vTaskDelay(1);
  }
  closedir(dir);

  if (image_count == 0 || selected_path[0] == '\0')
  {
    ESP_LOGW(TAG, "No image files found in %s", pics_dir);
    show_sleep_cover(renderer);
    return;
  }

  FILE *fp = fopen(selected_path, "rb");
  if (!fp)
  {
    show_sleep_cover(renderer);
    return;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 0)
  {
    fclose(fp);
    show_sleep_cover(renderer);
    return;
  }

  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (!data)
  {
    fclose(fp);
    ESP_LOGW(TAG, "Failed to allocate memory for sleep image");
    show_sleep_cover(renderer);
    return;
  }

  size_t bytes_read = fread(data, 1, (size_t)size, fp);
  fclose(fp);
  if (bytes_read != (size_t)size)
  {
    free(data);
    show_sleep_cover(renderer);
    return;
  }

#ifdef BOARD_TYPE_M5_PAPER
  ESP_LOGI(TAG, "Displaying sleep image: %s (%ld bytes)", selected_path, size);
  vTaskDelay(1);
  M5.Display.fillScreen(0xFF);
  bool success = M5.Display.drawJpg(data, (size_t)size, 0, 0,
                                    M5.Display.width(), M5.Display.height());
  free(data);
  if (!success)
  {
    ESP_LOGW(TAG, "Failed to decode sleep image, falling back to cover");
    show_sleep_cover(renderer);
    return;
  }
  ESP_LOGI(TAG, "Sleep image displayed successfully");
#else
  vTaskDelay(1);
  int img_w = 0, img_h = 0;
  bool can_render = renderer->get_image_size(selected_path, data, (size_t)size, &img_w, &img_h);
  vTaskDelay(1);
  if (!can_render || img_w <= 0 || img_h <= 0)
  {
    free(data);
    show_sleep_cover(renderer);
    return;
  }
  renderer->set_margin_top(0);
  renderer->set_margin_bottom(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);
  renderer->clear_screen();
  renderer->set_image_placeholder_enabled(false);
  vTaskDelay(1);
  renderer->draw_image(selected_path, data, (size_t)size, 0, 0,
                       renderer->get_page_width(), renderer->get_page_height());
  renderer->set_image_placeholder_enabled(true);
  free(data);
  vTaskDelay(1);
  renderer->flush_display();
#endif
}
