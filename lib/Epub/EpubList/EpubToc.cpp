#include "EpubToc.h"
#include "../TaskWdtGuard.h"
#include "PaginationBar.h"

static const char *TAG = "PUBINDEX";
#define PADDING 14
#define ITEMS_PER_PAGE 6

EpubToc::~EpubToc()
{
  for (auto *block : m_title_blocks)
  {
    delete block;
  }
  m_title_blocks.clear();
  delete epub;
}

void EpubToc::next()
{
  // must be loaded as we need the information from the epub
  if (!epub)
  {
    load();
  }
  state.selected_item = (state.selected_item + 1) % epub->get_toc_items_count();
}

void EpubToc::prev()
{
  // must be loaded as we need the information from the epub
  if (!epub)
  {
    load();
  }
  state.selected_item = (state.selected_item - 1 + epub->get_toc_items_count()) % epub->get_toc_items_count();
}

bool EpubToc::load()
{
  ESP_LOGE(TAG, ">>> EpubToc::load() START");

  // Suspend watchdog subscription during TOC load; restored automatically
  // on every return path.
  TaskWdtGuard wdt_guard;
  ESP_LOGI(TAG, "Watchdog disabled for TOC load");

  if (!epub || epub->get_path() != selected_epub.path)
  {
    renderer->show_busy();
    vTaskDelay(50); // Allow display update
    
    delete epub;

    epub = new Epub(selected_epub.path);
    vTaskDelay(10);
    
    if (!epub->load_with_task(64 * 1024))
    {
      ESP_LOGE(TAG, "Failed to load epub for index: %s", selected_epub.path);
      return false;
    }
    vTaskDelay(10);
  }
  // If there is no TOC, signal failure so callers can fall back to
  // opening the book directly without an index.
  if (epub->get_toc_items_count() == 0)
  {
    ESP_LOGE(TAG, ">>> No TOC entries available for %s - falling back to direct read", selected_epub.path);
    return false;
  }
  state.num_items = epub->get_toc_items_count();
  if (state.num_items <= 0)
  {
    state.num_items = 0;
    state.selected_item = 0;
  }
  else
  {
    if (state.selected_item < 0)
    {
      state.selected_item = 0;
    }
    else if (state.selected_item >= state.num_items)
    {
      state.selected_item = state.num_items - 1;
    }
  }
  if (state.num_items > 0)
  {
    if (m_title_blocks.size() < static_cast<size_t>(state.num_items))
    {
      m_title_blocks.resize(state.num_items, nullptr);
    }
  }
  else
  {
    m_title_blocks.clear();
  }
  ESP_LOGI(TAG, "Epub index loaded");

  return true;
}

// TODO - this is currently pretty much a copy of the epub list rendering
// we can fit a lot more on the screen by allowing variable cell heights
// and a lot of the optimisations that are used for the list aren't really
// required as we're not rendering thumbnails
void EpubToc::render()
{
  ESP_LOGD(TAG, "Rendering EPUB index");
  // For FreeType-backed renderers (e.g. Paper S3), temporarily
  // increase the reading font size while drawing the TOC so entries
  // are easier to tap and read. We restore the original size on exit.
#ifdef USE_FREETYPE
  int original_px = renderer->get_reading_font_pixel_height();
  bool size_changed = false;
  if (original_px > 0)
  {
    int toc_px = original_px * 2;
    size_changed = renderer->set_reading_font_pixel_height(toc_px);
  }
#endif
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  if (page_width <= 0 || page_height <= 0)
  {
    return;
  }

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int content_height = page_height;
  if (bottom_bar_height > 0 && bottom_bar_height < page_height)
  {
    content_height = page_height - bottom_bar_height;
  }
  // what page are we on?
  int current_page = 0;
  if (state.num_items > 0)
  {
    current_page = state.selected_item / ITEMS_PER_PAGE;
  }
  // show five items per page
  int cell_height = content_height / ITEMS_PER_PAGE;
  int start_index = current_page * ITEMS_PER_PAGE;
  int ypos = 0;
  // starting a fresh page or rendering from scratch?
  ESP_LOGI(TAG, "Current page is %d, previous page %d, redraw=%d", current_page, state.previous_rendered_page, m_needs_redraw);
  if (current_page != state.previous_rendered_page || m_needs_redraw)
  {
    m_needs_redraw = false;
    renderer->clear_screen();
    state.previous_selected_item = -1;
    // trigger a redraw of the items
    state.previous_rendered_page = -1;
  }
  for (int i = start_index; i < start_index + ITEMS_PER_PAGE && i < epub->get_toc_items_count(); i++)
  {
    // do we need to draw a new page of items?
    if (current_page != state.previous_rendered_page)
    {
      TextBlock *title_block = nullptr;
      if (i >= 0 && i < static_cast<int>(m_title_blocks.size()))
      {
        title_block = m_title_blocks[i];
      }
      if (!title_block)
      {
        title_block = new TextBlock(LEFT_ALIGN);
        title_block->add_span(epub->get_toc_item(i).title.c_str(), false, false);
        title_block->layout(renderer, epub, renderer->get_page_width());
        if (i >= 0 && i < static_cast<int>(m_title_blocks.size()))
        {
          m_title_blocks[i] = title_block;
        }
      }
      // work out the height of the title
      int text_height = cell_height - PADDING;
      int title_height = title_block->line_breaks.size() * renderer->get_line_height();
      // center the title in the cell
      int y_offset = title_height < text_height ? (text_height - title_height) / 2 : 0;
      // draw each line of the index block making sure we don't run over the cell
      int height = 0;
      for (int i = 0; i < title_block->line_breaks.size() && height < text_height; i++)
      {
        title_block->render(renderer, i, 10, ypos + height + y_offset);
        height += renderer->get_line_height();
      }
    }
    // clear the selection box around the previous selected item
    if (state.previous_selected_item == i)
    {
      for (int line = 0; line < 3; line++)
      {
        renderer->draw_rect(line, ypos + PADDING / 2 + line, page_width - 2 * line, cell_height - PADDING - 2 * line, 255);
      }
    }
    // draw the selection box around the current selection
    if (state.selected_item == i)
    {
      for (int line = 0; line < 3; line++)
      {
        renderer->draw_rect(line, ypos + PADDING / 2 + line, page_width - 2 * line, cell_height - PADDING - 2 * line, 0);
      }
    }
    ypos += cell_height;
  }
  state.previous_selected_item = state.selected_item;
  state.previous_rendered_page = current_page;

  // draw bottom navigation bar
  int total_pages = 1;
  if (state.num_items > 0)
  {
    total_pages = (state.num_items + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
  }
  draw_pagination_bar(renderer, current_page, total_pages);

#ifdef USE_FREETYPE
  // Restore the original reading font size after TOC rendering so the
  // main reading view keeps its configured size.
  if (size_changed)
  {
    renderer->set_reading_font_pixel_height(original_px);
  }
#endif
}

uint16_t EpubToc::get_selected_toc()
{
  return epub->get_spine_index_for_toc_index(state.selected_item);
}
