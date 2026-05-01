#include "StatusBar.h"
#include "AppSettings.h"
#include "AppState.h"
#include "Renderer/Renderer.h"
#include "EpubList/State.h"

void draw_battery_level(Renderer *renderer, float voltage, float percentage)
{
  if (!status_bar_visible)
  {
    renderer->set_margin_top(0);
    return;
  }

  // Clear the top margin so we can draw status elements at y=0.
  renderer->set_margin_top(0);

  // Left: page info (only while reading)
  if (ui_state == READING_EPUB &&
      epub_list_state.selected_item >= 0 &&
      epub_list_state.selected_item < epub_list_state.num_epubs)
  {
    EpubListItem &item = epub_list_state.epub_list[epub_list_state.selected_item];
    if (item.pages_in_current_section > 0)
    {
      char page_str[32];
      bool on_bookmarked_page = item.bookmark_set &&
                                item.current_section == item.bookmark_section &&
                                item.current_page    == item.bookmark_page;
      if (on_bookmarked_page)
      {
        snprintf(page_str, sizeof(page_str), "S%d  %d/%d [B]",
                 item.current_section + 1,
                 item.current_page + 1,
                 item.pages_in_current_section);
      }
      else
      {
        snprintf(page_str, sizeof(page_str), "S%d  %d/%d",
                 item.current_section + 1,
                 item.current_page + 1,
                 item.pages_in_current_section);
      }
      renderer->draw_text(10, 10, page_str, false, false);
    }
  }

  // Right: battery icon
  const int width        = 40;
  const int height       = 20;
  const int margin_right = 5;
  const int margin_top   = 10;
  int xpos = renderer->get_page_width() - width - margin_right;
  int ypos = margin_top;
  int percent_width = width * percentage / 100;
  renderer->fill_rect(xpos, ypos, width, height, 255);
  renderer->fill_rect(xpos + width - percent_width, ypos, percent_width, height, 0);
  renderer->draw_rect(xpos, ypos, width, height, 0);
  renderer->fill_rect(xpos - 4, ypos + height / 4, 4, height / 2, 0);

  // Restore content margin.
  renderer->set_margin_top(37);
}

void show_status_bar_toast(Renderer *renderer, const char *text)
{
  if (!text) return;

  int page_width  = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0) return;

  const int padding    = 4;
  const int box_height = line_height + padding * 2;
  int y = page_height - box_height - 2;
  if (y < 0) y = 0;

  renderer->fill_rect(0, y, page_width, box_height, 255);
  renderer->draw_rect(0, y, page_width, box_height, 0);
  renderer->draw_text(5, y + padding + line_height / 2, text, false, false);
}
