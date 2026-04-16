#pragma once

#ifdef BOARD_TYPE_M5_PAPER

class Renderer;

// Starts the WiFi AP and HTTP upload server.
// Returns true on success. On failure, shows a toast and returns false.
bool start_wifi_uploader(Renderer *renderer);

// Stops the WiFi AP and HTTP upload server.
// If show_toast is true, displays a confirmation toast.
void stop_wifi_uploader(Renderer *renderer, bool show_toast);

// Returns true if the uploader is currently active.
bool wifi_uploader_is_running();

// Processes one round of pending HTTP client requests.
// Call from the main loop while the uploader is running.
void wifi_uploader_handle_client();

#endif // BOARD_TYPE_M5_PAPER
