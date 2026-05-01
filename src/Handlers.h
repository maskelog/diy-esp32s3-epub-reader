#pragma once
// Forward declarations for the UI handler functions defined in main.cpp.
// Include this header to call them from other modules (e.g. ReaderMenu).

#include "boards/controls/Actions.h"

class Renderer;

void handleEpub(Renderer *renderer, UIAction action);
void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw);
void handleEpubTableContents(Renderer *renderer, UIAction action, bool needs_redraw);
void show_library_loading(Renderer *renderer);

// Persists the last-opened EPUB path to /Books/last_book.txt so that the
// next cold boot opens this book even if other books had higher current_page.
void save_last_book_path(const char *path);
