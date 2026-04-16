#pragma once

class Renderer;

// Draws the battery icon and page info at the top of the screen.
// Also manages the top margin: sets margin_top=0 while drawing,
// restores margin_top=37 when the status bar is visible.
void draw_battery_level(Renderer *renderer, float voltage, float percentage);

// Renders a temporary toast message in a box at the bottom of the screen.
void show_status_bar_toast(Renderer *renderer, const char *text);
