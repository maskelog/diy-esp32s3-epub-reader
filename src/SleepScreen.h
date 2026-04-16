#pragma once

class Renderer;

// Displays the sleep screen according to the current sleep_image_mode setting.
// Reads the mode from AppSettings and dispatches to the appropriate renderer.
void show_sleep_image(Renderer *renderer);
