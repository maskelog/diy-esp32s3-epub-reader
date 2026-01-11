#ifndef UNIT_TEST
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#define vTaskDelay(t)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif

#include <algorithm>

#include "PNGHelper.h"
#include "Renderer.h"

static const char *TAG = "PNG";

#ifdef USE_PNGLE

// PNGLE-based implementation -------------------------------------------------

void pngle_init_callback(pngle_t *pngle, uint32_t w, uint32_t h)
{
  PNGHelper *helper = static_cast<PNGHelper *>(pngle_get_user_data(pngle));
  if (!helper)
  {
    return;
  }
  // compute scaling factors based on destination box
  helper->last_y = -1;
  helper->x_scale = 1.0f;
  helper->y_scale = 1.0f;
  helper->downscale = false;
  if (w > 0 && h > 0 && helper->target_width > 0 && helper->target_height > 0)
  {
    helper->x_scale = static_cast<float>(helper->target_width) / static_cast<float>(w);
    helper->y_scale = static_cast<float>(helper->target_height) / static_cast<float>(h);
    helper->downscale = (helper->x_scale < 0.999f || helper->y_scale < 0.999f);
    if (helper->downscale && helper->target_width > 0)
    {
      helper->row_sum.assign(static_cast<size_t>(helper->target_width), 0);
      helper->row_count.assign(static_cast<size_t>(helper->target_width), 0);
      helper->accum_y = -1;
    }
  }
}

void pngle_draw_callback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4])
{
  PNGHelper *helper = static_cast<PNGHelper *>(pngle_get_user_data(pngle));
  if (!helper || !helper->renderer)
  {
    return;
  }

  uint8_t r = rgba[0];
  uint8_t g = rgba[1];
  uint8_t b = rgba[2];
  uint8_t a = rgba[3];

  uint32_t gray = (r * 38 + g * 75 + b * 15) >> 7;
  if (a < 255)
  {
    gray = (gray * a + 255 * (255 - a)) / 255;
  }
  uint8_t gray8 = static_cast<uint8_t>(gray);

  int sx0 = helper->x_pos + static_cast<int>(x * helper->x_scale);
  int sy0 = helper->y_pos + static_cast<int>(y * helper->y_scale);
  int sx1 = helper->x_pos + static_cast<int>((x + w) * helper->x_scale);
  int sy1 = helper->y_pos + static_cast<int>((y + h) * helper->y_scale);

  if (helper->downscale)
  {
    int dst_y = sy0;
    if (helper->accum_y != dst_y)
    {
      if (helper->accum_y >= 0)
      {
        for (int i = 0; i < helper->target_width; ++i)
        {
          uint16_t count = helper->row_count[static_cast<size_t>(i)];
          if (count == 0)
          {
            continue;
          }
          uint32_t sum = helper->row_sum[static_cast<size_t>(i)];
          uint8_t gray = static_cast<uint8_t>((sum + (count / 2)) / count);
          uint8_t mapped = helper->renderer->map_image_gray(gray);
          helper->renderer->draw_pixel(helper->x_pos + i, helper->accum_y, mapped);
          helper->row_sum[static_cast<size_t>(i)] = 0;
          helper->row_count[static_cast<size_t>(i)] = 0;
        }
      }
      helper->accum_y = dst_y;
    }
    const int dst_x = sx0;
    const int idx = dst_x - helper->x_pos;
    if (idx >= 0 && idx < helper->target_width)
    {
      helper->row_sum[static_cast<size_t>(idx)] += gray8;
      helper->row_count[static_cast<size_t>(idx)] += 1;
    }
    return;
  }

  if (sx1 <= sx0)
  {
    sx1 = sx0 + 1;
  }
  if (sy1 <= sy0)
  {
    sy1 = sy0 + 1;
  }

  for (int yy = sy0; yy < sy1; ++yy)
  {
    if (yy != helper->last_y)
    {
      vTaskDelay(1);
      helper->last_y = yy;
    }
    for (int xx = sx0; xx < sx1; ++xx)
    {
      helper->renderer->draw_pixel(xx, yy, helper->renderer->map_image_gray(gray8));
    }
  }
}

bool PNGHelper::get_size(const uint8_t *data, size_t data_size, int *width, int *height)
{
  pngle_t *png = pngle_new();
  if (!png)
  {
    ESP_LOGE(TAG, "pngle_new failed");
    return false;
  }

  int local_width = 0;
  int local_height = 0;

  auto init_cb = [](pngle_t *p, uint32_t w, uint32_t h) {
    PNGHelper *self = static_cast<PNGHelper *>(pngle_get_user_data(p));
    if (!self)
    {
      return;
    }
    int *w_ptr = reinterpret_cast<int *>(&self->x_scale); // reuse storage
    int *h_ptr = reinterpret_cast<int *>(&self->y_scale);
    *w_ptr = static_cast<int>(w);
    *h_ptr = static_cast<int>(h);
  };

  pngle_set_user_data(png, this);
  pngle_set_init_callback(png, init_cb);

  int fed = pngle_feed(png, data, data_size);
  if (fed < 0)
  {
    ESP_LOGE(TAG, "pngle error: %s", pngle_error(png));
    pngle_destroy(png);
    return false;
  }

  // Recover width/height from reused storage
  local_width = *reinterpret_cast<int *>(&x_scale);
  local_height = *reinterpret_cast<int *>(&y_scale);

  pngle_destroy(png);

  if (local_width <= 0 || local_height <= 0)
  {
    return false;
  }
  *width = local_width;
  *height = local_height;
  return true;
}

bool PNGHelper::render(const uint8_t *data, size_t data_size, Renderer *renderer, int x_pos, int y_pos, int width, int height)
{
  this->renderer = renderer;
  this->y_pos = y_pos;
  this->x_pos = x_pos;
  this->target_width = width;
  this->target_height = height;
  this->last_y = -1;
  this->x_scale = 1.0f;
  this->y_scale = 1.0f;
  this->downscale = false;
  this->accum_y = -1;
  this->row_sum.clear();
  this->row_count.clear();

  pngle_t *png = pngle_new();
  if (!png)
  {
    ESP_LOGE(TAG, "pngle_new failed");
    return false;
  }

  pngle_set_user_data(png, this);
  pngle_set_init_callback(png, pngle_init_callback);
  pngle_set_draw_callback(png, pngle_draw_callback);

  int fed = pngle_feed(png, data, data_size);
  if (fed < 0)
  {
    ESP_LOGE(TAG, "pngle error: %s", pngle_error(png));
    pngle_destroy(png);
    return false;
  }

  if (downscale && accum_y >= 0)
  {
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
  pngle_destroy(png);
  return true;
}

#else // USE_PNGLE

#include <PNGdec.h>

void convert_rgb_565_to_rgb(uint16_t rgb565, uint8_t *r, uint8_t *g, uint8_t *b)
{
  *r = ((rgb565 >> 11) & 0x1F) << 3;
  *g = ((rgb565 >> 5) & 0x3F) << 2;
  *b = (rgb565 & 0x1F) << 3;
}

bool PNGHelper::get_size(const uint8_t *data, size_t data_size, int *width, int *height)
{
  int rc = png.openRAM(const_cast<uint8_t *>(data), data_size, NULL);
  if (rc == PNG_SUCCESS)
  {
    ESP_LOGI(TAG, "image specs: (%d x %d), %d bpp, pixel type: %d", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    *width = png.getWidth();
    *height = png.getHeight();
    png.close();
    return true;
  }
  else
  {
    ESP_LOGE(TAG, "failed to open png %d", rc);
    return false;
  }
}

bool PNGHelper::render(const uint8_t *data, size_t data_size, Renderer *renderer, int x_pos, int y_pos, int width, int height)
{
  this->renderer = renderer;
  this->y_pos = y_pos;
  this->x_pos = x_pos;
  int rc = png.openRAM(const_cast<uint8_t *>(data), data_size, png_draw_callback);
  if (rc == PNG_SUCCESS)
  {
    int img_w = png.getWidth();
    int img_h = png.getHeight();
    if (width <= 0 || height <= 0 || img_w <= 0 || img_h <= 0)
    {
      ESP_LOGE(TAG, "invalid PNG size (%d x %d) or target (%d x %d)", img_w, img_h, width, height);
      png.close();
      return false;
    }
    this->x_scale = float(width) / float(img_w);
    this->y_scale = float(height) / float(img_h);
    if (this->x_scale <= 0.0f || this->y_scale <= 0.0f)
    {
      ESP_LOGE(TAG, "invalid PNG scale (%f x %f)", this->x_scale, this->y_scale);
      png.close();
      return false;
    }
    this->last_y = -1;
    this->downscale = (x_scale < 0.999f || y_scale < 0.999f);
    this->accum_y = -1;
    this->row_sum.clear();
    this->row_count.clear();
    size_t row_bytes = static_cast<size_t>(img_w) * sizeof(uint16_t);
    if (row_bytes / sizeof(uint16_t) != static_cast<size_t>(img_w))
    {
      ESP_LOGE(TAG, "PNG row size overflow: %d", img_w);
      png.close();
      return false;
    }
    this->tmp_rgb565_buffer = (uint16_t *)malloc(row_bytes);
    if (!this->tmp_rgb565_buffer)
    {
      ESP_LOGE(TAG, "PNG row buffer alloc failed: %d", img_w);
      png.close();
      return false;
    }

    png.decode(this, PNG_FAST_PALETTE);
    if (downscale && accum_y >= 0)
    {
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
    png.close();
    free(this->tmp_rgb565_buffer);
    return true;
  }
  else
  {
    ESP_LOGE(TAG, "failed to parse png %d", rc);
    return false;
  }
}

void PNGHelper::draw_callback(PNGDRAW *draw)
{
  if (!tmp_rgb565_buffer)
  {
    return;
  }
  // work out where we should be drawing this line
  int y = y_pos + draw->y * y_scale;
  // only bother to draw if we haven't already drawn to this destination line
  if (y != last_y)
  {
    // feed the watchdog
    vTaskDelay(1);
    // get the rgb 565 pixel values                 BKG is in form of 00BBGGRR
    png.getLineAsRGB565(draw, tmp_rgb565_buffer, 0, 0x00FFFFFF);
    int src_width = png.getWidth();
    if (src_width <= 0 || x_scale <= 0.0f)
    {
      return;
    }
    if (downscale)
    {
      int dst_y = y_pos + static_cast<int>(draw->y * y_scale);
      if (accum_y != dst_y)
      {
        if (accum_y >= 0)
        {
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
        accum_y = dst_y;
      }

      for (int src_x = 0; src_x < src_width; ++src_x)
      {
        int dst_x = x_pos + static_cast<int>(src_x * x_scale);
        int idx = dst_x - x_pos;
        if (idx < 0 || idx >= target_width)
        {
          continue;
        }
        uint8_t r, g, b;
        convert_rgb_565_to_rgb(tmp_rgb565_buffer[src_x], &r, &g, &b);
        uint32_t gray = (r * 38 + g * 75 + b * 15) >> 7;
        row_sum[static_cast<size_t>(idx)] += static_cast<uint8_t>(gray);
        row_count[static_cast<size_t>(idx)] += 1;
      }
    }
    else
    {
      int dst_width = static_cast<int>(src_width * x_scale);
      if (dst_width <= 0)
      {
        return;
      }
      for (int x = 0; x < dst_width; x++)
      {
        uint8_t r, g, b;
        int src_x = static_cast<int>(x / x_scale);
        if (src_x < 0)
        {
          src_x = 0;
        }
        else if (src_x >= src_width)
        {
          src_x = src_width - 1;
        }
        convert_rgb_565_to_rgb(tmp_rgb565_buffer[src_x], &r, &g, &b);
        uint32_t gray = (r * 38 + g * 75 + b * 15) >> 7;
        renderer->draw_pixel(x_pos + x, y, renderer->map_image_gray(static_cast<uint8_t>(gray)));
      }
    }
    last_y = y;
  }
};

int png_draw_callback(PNGDRAW *draw)
{
  PNGHelper *helper = (PNGHelper *)draw->pUser;
  helper->draw_callback(draw);
  return 1;
}

#endif // USE_PNGLE
