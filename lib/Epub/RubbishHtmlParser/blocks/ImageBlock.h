#pragma once
#include "../../Renderer/Renderer.h"
#include "Block.h"
#include "../../EpubList/Epub.h"
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

  ImageBlock(std::string src, std::string alt = "") : m_src(src), m_alt(alt)
  {
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
    // Display image placeholder - larger box for visibility
    std::string display_text = get_display_name();
    
    // Calculate text dimensions
    int text_width = renderer->get_text_width(display_text.c_str(), false, true);
    int line_height = renderer->get_line_height();
    
    // Make placeholder box larger and more visible
    int page_width = renderer->get_page_width();
    int page_height = renderer->get_page_height();
    
    // Use 80% of page width, with minimum height of 100px or 3 line heights
    width = (page_width * 80) / 100;
    height = std::max(100, line_height * 3);
    x_pos = (page_width - width) / 2;
    
    ESP_LOGE("ImageBlock", "Placeholder layout: %s, size=%dx%d", m_src.c_str(), width, height);
  }
  
  void render(Renderer *renderer, Epub *epub, int y_pos)
  {
    if (width <= 0 || height <= 0)
    {
      return;
    }
    
    std::string display_text = get_display_name();
    
    // Draw a bordered box with the image name - more visible
    renderer->fill_rect(x_pos, y_pos, width, height, 230);  // Light gray background
    
    // Draw double border for visibility
    renderer->draw_rect(x_pos, y_pos, width, height, 0);
    renderer->draw_rect(x_pos + 2, y_pos + 2, width - 4, height - 4, 0);
    
    // Center text vertically and horizontally in the box
    int text_width = renderer->get_text_width(display_text.c_str(), false, true);
    int text_x = x_pos + (width - text_width) / 2;
    int text_y = y_pos + (height / 2) - (renderer->get_line_height() / 2);
    
    renderer->draw_text(text_x, text_y, display_text.c_str(), false, true);  // italic
    
    ESP_LOGE("ImageBlock", "Rendered placeholder at y=%d: %s", y_pos, display_text.c_str());
  }
  
  virtual void dump()
  {
    printf("ImageBlock: %s (alt: %s)\n", m_src.c_str(), m_alt.c_str());
  }
  virtual BlockType getType()
  {
    return IMAGE_BLOCK;
  }
};
