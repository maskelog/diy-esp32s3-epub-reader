
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
    if ((img_w / s) >= target_w && (img_h / s) >= target_h)
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
  this->target_width = width;
  this->downscale = false;
  this->accum_y = -1;
  this->row_sum.clear();
  this->row_count.clear();
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
  x_scale = float(width) / float(scaled_w);
  y_scale = float(height) / float(scaled_h);
  downscale = (x_scale < 0.999f || y_scale < 0.999f);
  if (downscale && target_width > 0)
  {
    row_sum.assign(static_cast<size_t>(target_width), 0);
    row_count.assign(static_cast<size_t>(target_width), 0);
  }

  ESP_LOGI(TAG, "JPEG Decoded - size %d,%d, scaled %d,%d, scale = %f,%f", img_w, img_h, scaled_w, scaled_h, x_scale, y_scale);
  const int res = jpeg.decode(0, 0, scale_opt);
  if (!res)
  {
    ESP_LOGE(TAG, "JPEG Decode failed (render) - %d", jpeg.getLastError());
  }
  if (downscale && accum_y >= 0)
  {
    flush_downscale_row();
  }
  jpeg.close();
  return res != 0;
}

void JPEGHelper::flush_downscale_row()
{
  if (!renderer || accum_y < 0 || target_width <= 0)
  {
    return;
  }
  for (int i = 0; i < target_width; ++i)
  {
    uint16_t count = row_count[static_cast<size_t>(i)];
    if (count == 0)
    {
      continue;
    }
    uint32_t sum = row_sum[static_cast<size_t>(i)];
    uint8_t gray = static_cast<uint8_t>((sum + (count / 2)) / count);
    uint8_t mapped = renderer->map_image_gray(gray);
    renderer->draw_pixel(x_pos + i, accum_y, mapped);
    row_sum[static_cast<size_t>(i)] = 0;
    row_count[static_cast<size_t>(i)] = 0;
  }
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
  const bool upscale = (context->x_scale > 1.001f || context->y_scale > 1.001f);
  const bool downscale = context->downscale;

  for (int y = 0; y < draw_height; ++y)
  {
    const int src_y = pDraw->y + y;
    int dy0 = context->y_pos + int(src_y * context->y_scale);
    int dy1 = context->y_pos + int((src_y + 1) * context->y_scale);
    if (dy1 <= dy0)
    {
      dy1 = dy0 + 1;
    }
    const int base = y * stride;
    for (int x = 0; x < draw_width; ++x)
    {
      const int src_x = pDraw->x + x;
      int dx0 = context->x_pos + int(src_x * context->x_scale);
      int dx1 = context->x_pos + int((src_x + 1) * context->x_scale);
      if (dx1 <= dx0)
      {
        dx1 = dx0 + 1;
      }

      const uint16_t pixel = pixels[base + x];
      const uint8_t r = (pixel >> 11) & 0x1F;
      const uint8_t g = (pixel >> 5) & 0x3F;
      const uint8_t b = pixel & 0x1F;
      const uint8_t r8 = (r << 3) | (r >> 2);
      const uint8_t g8 = (g << 2) | (g >> 4);
      const uint8_t b8 = (b << 3) | (b >> 2);
      const uint8_t p00 = static_cast<uint8_t>((r8 * 38 + g8 * 75 + b8 * 15) >> 7);

      if (downscale)
      {
        const int dst_y = dy0;
        if (context->accum_y != dst_y)
        {
          if (context->accum_y >= 0)
          {
            context->flush_downscale_row();
          }
          context->accum_y = dst_y;
        }
        const int dst_x = dx0;
        const int idx = dst_x - context->x_pos;
        if (idx >= 0 && idx < context->target_width)
        {
          context->row_sum[static_cast<size_t>(idx)] += p00;
          context->row_count[static_cast<size_t>(idx)] += 1;
        }
        continue;
      }

      if (!upscale)
      {
        const uint8_t mapped = context->renderer->map_image_gray(p00);
        for (int yy = dy0; yy < dy1; ++yy)
        {
          for (int xx = dx0; xx < dx1; ++xx)
          {
            context->renderer->draw_pixel(xx, yy, mapped);
          }
        }
        continue;
      }

      uint8_t p10 = p00;
      uint8_t p01 = p00;
      uint8_t p11 = p00;
      if (x + 1 < draw_width)
      {
        const uint16_t pix_r = pixels[base + x + 1];
        const uint8_t rr = (pix_r >> 11) & 0x1F;
        const uint8_t gg = (pix_r >> 5) & 0x3F;
        const uint8_t bb = pix_r & 0x1F;
        const uint8_t rr8 = (rr << 3) | (rr >> 2);
        const uint8_t gg8 = (gg << 2) | (gg >> 4);
        const uint8_t bb8 = (bb << 3) | (bb >> 2);
        p10 = static_cast<uint8_t>((rr8 * 38 + gg8 * 75 + bb8 * 15) >> 7);
      }
      if (y + 1 < draw_height)
      {
        const uint16_t pix_d = pixels[(y + 1) * stride + x];
        const uint8_t rr = (pix_d >> 11) & 0x1F;
        const uint8_t gg = (pix_d >> 5) & 0x3F;
        const uint8_t bb = pix_d & 0x1F;
        const uint8_t rr8 = (rr << 3) | (rr >> 2);
        const uint8_t gg8 = (gg << 2) | (gg >> 4);
        const uint8_t bb8 = (bb << 3) | (bb >> 2);
        p01 = static_cast<uint8_t>((rr8 * 38 + gg8 * 75 + bb8 * 15) >> 7);
      }
      if (x + 1 < draw_width && y + 1 < draw_height)
      {
        const uint16_t pix_rd = pixels[(y + 1) * stride + x + 1];
        const uint8_t rr = (pix_rd >> 11) & 0x1F;
        const uint8_t gg = (pix_rd >> 5) & 0x3F;
        const uint8_t bb = pix_rd & 0x1F;
        const uint8_t rr8 = (rr << 3) | (rr >> 2);
        const uint8_t gg8 = (gg << 2) | (gg >> 4);
        const uint8_t bb8 = (bb << 3) | (bb >> 2);
        p11 = static_cast<uint8_t>((rr8 * 38 + gg8 * 75 + bb8 * 15) >> 7);
      }

      const int dx_range = dx1 - dx0;
      const int dy_range = dy1 - dy0;
      for (int yy = dy0; yy < dy1; ++yy)
      {
        const int v_num = (dy_range > 1) ? (yy - dy0) : 0;
        const int v = (dy_range > 1) ? (v_num * 255) / (dy_range - 1) : 0;
        const int inv_v = 255 - v;
        for (int xx = dx0; xx < dx1; ++xx)
        {
          const int u_num = (dx_range > 1) ? (xx - dx0) : 0;
          const int u = (dx_range > 1) ? (u_num * 255) / (dx_range - 1) : 0;
          const int inv_u = 255 - u;
          const uint32_t v00 = static_cast<uint32_t>(p00) * inv_u * inv_v;
          const uint32_t v10 = static_cast<uint32_t>(p10) * u * inv_v;
          const uint32_t v01 = static_cast<uint32_t>(p01) * inv_u * v;
          const uint32_t v11 = static_cast<uint32_t>(p11) * u * v;
          const uint32_t sum = v00 + v10 + v01 + v11;
          const uint8_t gray = static_cast<uint8_t>((sum + 32512) / 65025);
          const uint8_t mapped = context->renderer->map_image_gray(gray);
          context->renderer->draw_pixel(xx, yy, mapped);
        }
      }
    }
  }

  return 1;
}
