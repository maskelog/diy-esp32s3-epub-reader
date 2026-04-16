#pragma once
#include "boards/controls/Actions.h"

class Renderer;

// Opens the reader menu.  If advanced=true the advanced settings page is shown
// directly; if false the basic menu is shown.
void open_reader_menu(Renderer *renderer, bool advanced);

// Re-renders the current reader menu page (basic or advanced).
void renderReaderMenu(Renderer *renderer);

// Processes a UIAction while the reader menu is active.
void handleReaderMenu(Renderer *renderer, UIAction action);
