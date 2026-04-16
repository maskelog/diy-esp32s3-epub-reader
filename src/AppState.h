#pragma once
// Runtime state globals defined in main.cpp.
// Include this header to get extern access to app-wide state.

#include "UIState.h"
#include "EpubList/State.h"

class EpubList;
class EpubReader;
class EpubToc;

extern UIState ui_state;
extern EpubListState epub_list_state;
extern EpubTocState epub_index_state;
extern EpubList *epub_list;
extern EpubReader *reader;
extern EpubToc *contents;
extern bool g_request_sleep_now;
extern const char *books_index_path;
