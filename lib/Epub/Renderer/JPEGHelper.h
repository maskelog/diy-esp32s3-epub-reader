#pragma once

#include <JPEGDEC.h>
#include <string>
#include "ImageHelper.h"

class JPEGHelper : public ImageHelper
{
private:
  float x_scale;
  float y_scale;
  Renderer *renderer;
  int x_pos;
  int y_pos;

  static int draw_jpeg_function(JPEGDRAW *pDraw);

public:
  bool get_size(const uint8_t *data, size_t data_size, int *width, int *height);
  bool render(const uint8_t *data, size_t data_size, Renderer *renderer, int x_pos, int y_pos, int width, int height);
};
