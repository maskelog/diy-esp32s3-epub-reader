#include "PaginationBar.h"

#include <cstdio>

#include "Renderer/Renderer.h"
#include "./State.h"

void draw_pagination_bar(Renderer *renderer, int current_page, int total_pages)
{
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  if (bottom_bar_height <= 0 || bottom_bar_height > page_height)
  {
    return;
  }

  int bar_y = page_height - bottom_bar_height;
  renderer->fill_rect(0, bar_y, page_width, bottom_bar_height, 255);
  int center_y = bar_y + bottom_bar_height / 2;

  if (total_pages < 1)
  {
    total_pages = 1;
  }
  int page_display = current_page + 1;
  if (page_display < 1)
  {
    page_display = 1;
  }
  if (page_display > total_pages)
  {
    page_display = total_pages;
  }

  // Layout "<<      <      Page X of Y      >      >>" so that the
  // arrow groups are bold with ~50% more spacing between single and
  // double arrows, while the "Page X of Y" text remains normal.
  const char *left_double = "<<";
  const char *left_single = "<";
  const char *right_single = ">";
  const char *right_double = ">>";

  char center[32];
  snprintf(center, sizeof(center), "%d / %d", page_display, total_pages);

  int w_ld = renderer->get_text_width(left_double, true, false);
  int w_ls = renderer->get_text_width(left_single, true, false);
  int w_center = renderer->get_text_width(center, false, false);
  int w_rs = renderer->get_text_width(right_single, true, false);
  int w_rd = renderer->get_text_width(right_double, true, false);
  if (w_ld < 0) w_ld = 0;
  if (w_ls < 0) w_ls = 0;
  if (w_center < 0) w_center = 0;
  if (w_rs < 0) w_rs = 0;
  if (w_rd < 0) w_rd = 0;

  // Divide the full page width into five equal navigation regions
  // that span the entire bottom bar: [<<] [<] [X / Y] [>] [>>]. Place
  // the text baseline slightly below geometric center to account for
  // how epdiy bitmap glyphs sit relative to the baseline.
  int line_h = renderer->get_line_height();
  if (line_h <= 0)
  {
    line_h = 20;
  }
  int label_y = center_y - (3 * line_h) / 4;

  int columns = 5;
  int col_width = page_width / columns;
  if (col_width <= 0)
  {
    col_width = 1;
  }

  int ld_zone_start = 0;
  int ld_zone_end = ld_zone_start + col_width;

  int ls_zone_start = ld_zone_end;
  int ls_zone_end = ls_zone_start + col_width;

  int center_zone_start = ls_zone_end;
  int center_zone_end = center_zone_start + col_width;

  int rs_zone_start = center_zone_end;
  int rs_zone_end = rs_zone_start + col_width;

  int rd_zone_start = rs_zone_end;
  int rd_zone_end = page_width; // consume any remainder to reach the edge

  int bar_top = bar_y;
  int bar_bottom = bar_y + bottom_bar_height;
  int bar_height = bar_bottom - bar_top;
  if (bar_height < renderer->get_line_height() + 4)
  {
    bar_height = renderer->get_line_height() + 4;
  }

  auto draw_nav_box = [&](int x0, int x1) {
    if (x1 <= x0)
    {
      return;
    }
    int box_x = x0;
    int box_w = x1 - x0;
    int box_y = bar_top + 2;
    int box_h = bar_height - 4;
    if (box_h <= 0)
    {
      box_h = bar_height;
    }
    renderer->draw_rect(box_x, box_y, box_w, box_h, 0);
  };

  // Outline each interactive region so the user can see the
  // navigation buttons that correspond to the touch zones.
  draw_nav_box(ld_zone_start, ld_zone_end);
  draw_nav_box(ls_zone_start, ls_zone_end);
  draw_nav_box(center_zone_start, center_zone_end);
  draw_nav_box(rs_zone_start, rs_zone_end);
  draw_nav_box(rd_zone_start, rd_zone_end);

  // Center each label within its corresponding region.
  auto center_label_x = [](int zone_start, int zone_end, int text_width) {
    int w = zone_end - zone_start;
    int x = zone_start + (w - text_width) / 2;
    if (x < zone_start)
    {
      x = zone_start;
    }
    return x;
  };

  int x_ld = center_label_x(ld_zone_start, ld_zone_end, w_ld);
  int x_ls = center_label_x(ls_zone_start, ls_zone_end, w_ls);
  int x_center = center_label_x(center_zone_start, center_zone_end, w_center);
  int x_rs = center_label_x(rs_zone_start, rs_zone_end, w_rs);
  int x_rd = center_label_x(rd_zone_start, rd_zone_end, w_rd);

  // Draw each segment: bold arrows, normal "Page X of Y".
  renderer->draw_text(x_ld, label_y, left_double, true, false);
  renderer->draw_text(x_ls, label_y, left_single, true, false);
  renderer->draw_text(x_center, label_y, center, false, false);
  renderer->draw_text(x_rs, label_y, right_single, true, false);
  renderer->draw_text(x_rd, label_y, right_double, true, false);
}
