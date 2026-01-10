#pragma once

#include <vector>
#include <sys/types.h>
extern "C" {
  #include <dirent.h>
}
#include <string.h>
#include <algorithm>
#include "Epub.h"
#include "Renderer/Renderer.h"
#include "../RubbishHtmlParser/blocks/TextBlock.h"
#include "../RubbishHtmlParser/htmlEntities.h"
#include "./State.h"

#ifndef UNIT_TEST
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#endif
#include <warning.h>

class Epub;
class Renderer;

class EpubList
{
private:
  Renderer *renderer;
  EpubListState &state;
  bool m_needs_redraw = false;
  std::vector<TextBlock *> m_title_blocks;

  bool load_index(const char *books_path, const char *index_path);

public:
  EpubList(Renderer *renderer, EpubListState &state) : renderer(renderer), state(state)
  {
    // Don't override use_grid_view here - it should be set by load_app_settings()
    // Default is already false (List view) from zero-initialization
  }
  ~EpubList()
  {
    for (auto *block : m_title_blocks)
    {
      delete block;
    }
    m_title_blocks.clear();
  }
  bool load(const char *path);
  void set_needs_redraw() { m_needs_redraw = true; }
  void next();
  void prev();
  void render();
  void save_index(const char *index_path);
};