#pragma once

class Renderer;

// Draws the shared bottom pagination bar used by the epub list and TOC
// screens: five equal-width outlined regions [<<] [<] [X / Y] [>] [>>]
// spanning the full page width at the bottom of the screen.
// current_page is zero based; total_pages is clamped to at least 1.
void draw_pagination_bar(Renderer *renderer, int current_page, int total_pages);
