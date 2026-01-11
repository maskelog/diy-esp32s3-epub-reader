#pragma once
#include "../../Renderer/Renderer.h"
#include "Block.h"
#include "../../EpubList/Epub.h"
#include <algorithm>
#include <vector>
#ifndef UNIT_TEST
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#else
#define ESP_LOGI(args...)
#define ESP_LOGE(args...)
#define ESP_LOGD(args...)
#define ESP_LOGW(args...)
#define vTaskDelay(t)
#endif

class ImageBlock : public Block
{
public:
  // the src attribute from the image element
  std::string m_src;
  std::string m_alt;  // alt text for the image
  int y_pos;
  int x_pos;
  int width;
  int height;
  int src_width;
  int src_height;

  ImageBlock(std::string src, std::string alt = "") : m_src(src), m_alt(alt)
  {
    width = 0;
    height = 0;
    src_width = 0;
    src_height = 0;
  }
  virtual bool isEmpty()
  {
    return m_src.empty();
  }
  
  // Extract filename from path for display
  std::string get_display_name()
  {
    if (!m_alt.empty())
    {
      return m_alt;
    }
    // Extract filename from src path
    size_t last_slash = m_src.find_last_of('/');
    std::string filename = (last_slash != std::string::npos) ? m_src.substr(last_slash + 1) : m_src;
    // Remove extension
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos)
    {
      filename = filename.substr(0, last_dot);
    }
    return "[Image: " + filename + "]";
  }
  
  void layout(Renderer *renderer, Epub *epub, int max_width = -1)
  {
    int page_width = renderer->get_page_width();
    int page_height = renderer->get_page_height();
    if (page_width <= 0 || page_height <= 0)
    {
      layout_placeholder(renderer);
      return;
    }

    int max_h = page_height;
    int max_w = (max_width > 0) ? std::min(max_width, page_width) : page_width;

    // Avoid heavy image IO during layout; reserve max height to prevent overlap.
    width = max_w;
    height = max_h;
    x_pos = (page_width - width) / 2;
    src_width = 0;
    src_height = 0;
  }
  
  void render(Renderer *renderer, Epub *epub, int y_pos)
  {
    static const size_t kMaxImageBytes = 600 * 1024;
    if (width <= 0 || height <= 0)
    {
      return;
    }
    if (!renderer || !epub)
    {
      draw_placeholder(renderer, y_pos);
      return;
    }

    size_t uncompressed_size = epub->get_item_uncompressed_size(m_src);
    if (uncompressed_size > kMaxImageBytes)
    {
      draw_placeholder(renderer, y_pos);
      return;
    }

    size_t data_size = 0;
    bool cached = false;
    uint8_t *data = fetch_image_data(epub, m_src, &data_size, &cached);
    if (!data || data_size == 0)
    {
      if (data && !cached)
      {
        free(data);
      }
      draw_placeholder(renderer, y_pos);
      return;
    }

    int img_w = src_width;
    int img_h = src_height;
    if (img_w <= 0 || img_h <= 0)
    {
      int tmp_w = 0;
      int tmp_h = 0;
      if (renderer->get_image_size(m_src, data, data_size, &tmp_w, &tmp_h))
      {
        img_w = tmp_w;
        img_h = tmp_h;
        src_width = img_w;
        src_height = img_h;
      }
    }

    int draw_w = width;
    int draw_h = height;
    int draw_x = x_pos;
    int draw_y = y_pos;

    if (img_w > 0 && img_h > 0)
    {
      float scale_w = static_cast<float>(width) / static_cast<float>(img_w);
      float scale_h = static_cast<float>(height) / static_cast<float>(img_h);
      float scale = std::min(scale_w, scale_h); // fit box, allow letterbox
      draw_w = std::max(1, static_cast<int>(img_w * scale));
      draw_h = std::max(1, static_cast<int>(img_h * scale));
      draw_x = x_pos + (width - draw_w) / 2;
      draw_y = y_pos + (height - draw_h) / 2;
    }

    if (renderer)
    {
      draw_x += renderer->get_margin_left();
      draw_y += renderer->get_margin_top();
    }
    renderer->draw_image(m_src, data, data_size, draw_x, draw_y, draw_w, draw_h);

    if (!cached)
    {
      free(data);
    }
  }
  
  virtual void dump()
  {
    printf("ImageBlock: %s (alt: %s)\n", m_src.c_str(), m_alt.c_str());
  }
  virtual BlockType getType()
  {
    return IMAGE_BLOCK;
  }

private:
  struct CacheEntry
  {
    std::string path;
    uint8_t *data;
    size_t size;
    uint32_t last_used;
  };

  void layout_placeholder(Renderer *renderer)
  {
    std::string display_text = get_display_name();
    int line_height = renderer->get_line_height();
    int page_width = renderer->get_page_width();
    int page_height = renderer->get_page_height();

    width = (page_width * 80) / 100;
    height = std::max(100, line_height * 3);
    x_pos = (page_width - width) / 2;
    src_width = 0;
    src_height = 0;

    ESP_LOGE("ImageBlock", "Placeholder layout: %s, size=%dx%d", m_src.c_str(), width, height);
  }

  void draw_placeholder(Renderer *renderer, int y_pos)
  {
    std::string display_text = get_display_name();
    int draw_x = x_pos;
    int draw_y = y_pos;
    if (renderer)
    {
      draw_x += renderer->get_margin_left();
      draw_y += renderer->get_margin_top();
    }
    renderer->fill_rect(draw_x, draw_y, width, height, 230);
    renderer->draw_rect(draw_x, draw_y, width, height, 0);
    renderer->draw_rect(draw_x + 2, draw_y + 2, width - 4, height - 4, 0);

    int text_width = renderer->get_text_width(display_text.c_str(), false, true);
    int text_x = draw_x + (width - text_width) / 2;
    int text_y = draw_y + (height / 2) - (renderer->get_line_height() / 2);
    renderer->draw_text(text_x, text_y, display_text.c_str(), false, true);
  }

  uint8_t *fetch_image_data(Epub *epub, const std::string &path, size_t *size, bool *cached)
  {
    if (!cached || !size)
    {
      return nullptr;
    }
    *cached = false;
    *size = 0;

    static std::vector<CacheEntry> cache;
    static size_t cache_bytes = 0;
    static uint32_t tick = 0;
    const size_t max_cache_bytes = 512 * 1024;
    const size_t max_entry_bytes = 256 * 1024;
    const size_t max_entries = 2;

    for (auto &entry : cache)
    {
      if (entry.path == path)
      {
        entry.last_used = ++tick;
        *size = entry.size;
        *cached = true;
        return entry.data;
      }
    }

    uint8_t *data = epub->get_item_contents(path, size);
    if (!data || *size == 0)
    {
      if (data)
      {
        free(data);
      }
      return nullptr;
    }

    if (*size <= max_entry_bytes)
    {
      CacheEntry entry;
      entry.path = path;
      entry.data = data;
      entry.size = *size;
      entry.last_used = ++tick;
      cache.push_back(entry);
      cache_bytes += *size;
      *cached = true;

      while (cache.size() > max_entries || cache_bytes > max_cache_bytes)
      {
        size_t victim = 0;
        for (size_t i = 1; i < cache.size(); ++i)
        {
          if (cache[i].last_used < cache[victim].last_used)
          {
            victim = i;
          }
        }
        free(cache[victim].data);
        cache_bytes -= cache[victim].size;
        cache.erase(cache.begin() + victim);
      }
      return data;
    }

    return data;
  }
};
