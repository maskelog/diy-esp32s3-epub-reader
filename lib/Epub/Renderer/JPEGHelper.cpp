
#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#else
#define vTaskDelay(t)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif
#include <algorithm>
#include "JPEGHelper.h"
#include "Renderer.h"

static const char *TAG = "JPG";

static bool is_valid_jpeg_buffer(const uint8_t *data, size_t data_size)
{
  if (!data || data_size == 0)
  {
    return false;
  }
#if !defined(UNIT_TEST)
  // JPEG data should live in internal DRAM or PSRAM on ESP32 targets.
  if (!esp_ptr_in_dram(data) && !esp_ptr_external_ram(data))
  {
    return false;
  }
#endif
  return true;
}

static int select_scale_option(int img_w, int img_h, int target_w, int target_h, int *scale_div)
{
  int selected = 1;
  const int scales[] = {1, 2, 4, 8};
  for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); ++i)
  {
    int s = scales[i];
    if ((img_w / s) <= target_w && (img_h / s) <= target_h)
    {
      selected = s;
    }
  }

  if (scale_div)
  {
    *scale_div = selected;
  }

  switch (selected)
  {
  case 2:
    return JPEG_SCALE_HALF;
  case 4:
    return JPEG_SCALE_QUARTER;
  case 8:
    return JPEG_SCALE_EIGHTH;
  default:
    return 0;
  }
}

bool JPEGHelper::get_size(const uint8_t *data, size_t data_size, int *width, int *height)
{
  if (!width || !height)
  {
    ESP_LOGE(TAG, "Invalid JPEG output pointers");
    return false;
  }
  if (!is_valid_jpeg_buffer(data, data_size))
  {
    ESP_LOGE(TAG, "Invalid JPEG input");
    return false;
  }
  JPEGDEC jpeg;
  // Use NULL callback for get_size - we only need header info, not full decode
  if (!jpeg.openRAM(const_cast<uint8_t *>(data), static_cast<int>(data_size), NULL))
  {
    ESP_LOGE(TAG, "JPEG open failed (get_size) - %d", jpeg.getLastError());
    return false;
  }
  *width = jpeg.getWidth();
  *height = jpeg.getHeight();
  ESP_LOGI(TAG, "JPEG size read - %dx%d", *width, *height);
  jpeg.close();
  return *width > 0 && *height > 0;
}
bool JPEGHelper::render(const uint8_t *data, size_t data_size, Renderer *renderer, int x_pos, int y_pos, int width, int height)
{
  if (!renderer || width <= 0 || height <= 0)
  {
    ESP_LOGE(TAG, "Invalid JPEG render params");
    return false;
  }
  if (!is_valid_jpeg_buffer(data, data_size))
  {
    ESP_LOGE(TAG, "Invalid JPEG render params");
    return false;
  }
  this->renderer = renderer;
  this->y_pos = y_pos;
  this->x_pos = x_pos;
  this->last_y = -1;
  JPEGDEC jpeg;
  if (!jpeg.openRAM(const_cast<uint8_t *>(data), static_cast<int>(data_size), JPEGHelper::draw_jpeg_function))
  {
    ESP_LOGE(TAG, "JPEG open failed (render) - %d", jpeg.getLastError());
    return false;
  }
  jpeg.setUserPointer(this);
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  const int img_w = jpeg.getWidth();
  const int img_h = jpeg.getHeight();
  if (img_w <= 0 || img_h <= 0)
  {
    ESP_LOGE(TAG, "JPEG invalid size");
    jpeg.close();
    return false;
  }

  int scale_div = 1;
  const int scale_opt = select_scale_option(img_w, img_h, width, height, &scale_div);
  const int scaled_w = std::max(1, img_w / scale_div);
  const int scaled_h = std::max(1, img_h / scale_div);
  x_scale = std::min(1.0f, float(width) / float(scaled_w));
  y_scale = std::min(1.0f, float(height) / float(scaled_h));

  ESP_LOGI(TAG, "JPEG Decoded - size %d,%d, scaled %d,%d, scale = %f,%f", img_w, img_h, scaled_w, scaled_h, x_scale, y_scale);
  const int res = jpeg.decode(0, 0, scale_opt);
  if (!res)
  {
    ESP_LOGE(TAG, "JPEG Decode failed (render) - %d", jpeg.getLastError());
  }
  jpeg.close();
  return res != 0;
}

int JPEGHelper::draw_jpeg_function(JPEGDRAW *pDraw)
{
  if (!pDraw || !pDraw->pUser || !pDraw->pPixels)
  {
    return 0;
  }

  JPEGHelper *context = static_cast<JPEGHelper *>(pDraw->pUser);
  if (!context->renderer)
  {
    return 0;
  }

  if (pDraw->y != context->last_y)
  {
    context->last_y = pDraw->y;
    vTaskDelay(1);
  }

  const int draw_width = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
  const int draw_height = pDraw->iHeight;
  const int stride = pDraw->iWidth;
  const uint16_t *pixels = pDraw->pPixels;

  for (int y = 0; y < draw_height; ++y)
  {
    const int base = y * stride;
    for (int x = 0; x < draw_width; ++x)
    {
      const uint16_t pixel = pixels[base + x];
      const uint8_t r = (pixel >> 11) & 0x1F;
      const uint8_t g = (pixel >> 5) & 0x3F;
      const uint8_t b = pixel & 0x1F;
      const uint8_t r8 = (r << 3) | (r >> 2);
      const uint8_t g8 = (g << 2) | (g >> 4);
      const uint8_t b8 = (b << 3) | (b >> 2);
      const uint32_t gray = (r8 * 38 + g8 * 75 + b8 * 15) >> 7;
      const int dx = context->x_pos + int((pDraw->x + x) * context->x_scale);
      const int dy = context->y_pos + int((pDraw->y + y) * context->y_scale);
      context->renderer->draw_pixel(dx, dy, gray);
    }
  }

  return 1;
}
