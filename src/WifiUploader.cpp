#if defined(BOARD_TYPE_M5_PAPER)

#include "WifiUploader.h"
#include "StatusBar.h"
#include "Renderer/Renderer.h"
#include "config.h"
#include <M5Unified.h>
#include <WiFi.h>
#include "M5StackWiFiUploader.h"
#include <esp_log.h>

static const char *TAG = "WifiUploader";

static M5StackWiFiUploader wifi_uploader;

// 70MB+ epub uploads — cap is well above realistic ebook sizes.
static const uint32_t WIFI_UPLOAD_MAX_FILE_SIZE = 256UL * 1024UL * 1024UL;

static Renderer *g_progress_renderer = nullptr;
static uint32_t  g_progress_start_ms = 0;
static uint32_t  g_progress_last_log_ms = 0;
static uint8_t   g_progress_last_pct = 255;

bool wifi_uploader_is_running()
{
  return wifi_uploader.isRunning();
}

void wifi_uploader_handle_client()
{
  wifi_uploader.handleClient();
}

bool start_wifi_uploader(Renderer *renderer)
{
  if (WIFI_UPLOAD_AP_SSID[0] == '\0')
  {
    show_status_bar_toast(renderer, "WiFi upload not configured");
    return false;
  }

  if (wifi_uploader.isRunning())
    return true;

  WiFi.mode(WIFI_AP);
  bool ap_ok = WiFi.softAP(WIFI_UPLOAD_AP_SSID, WIFI_UPLOAD_AP_PASSWORD, WIFI_UPLOAD_AP_CHANNEL);
  if (!ap_ok)
  {
    show_status_bar_toast(renderer, "WiFi AP start failed");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  wifi_uploader.setMaxFileSize(WIFI_UPLOAD_MAX_FILE_SIZE);

  g_progress_renderer = renderer;

  wifi_uploader.onUploadStart([](const char *filename, uint32_t total) {
    g_progress_start_ms = millis();
    g_progress_last_log_ms = g_progress_start_ms;
    g_progress_last_pct = 255;
    ESP_LOGI(TAG, "Upload start: %s (%u bytes)", filename ? filename : "?", (unsigned)total);
  });

  wifi_uploader.onUploadProgress([](const char *filename, uint32_t uploaded, uint32_t total) {
    uint32_t now = millis();
    uint8_t pct = total ? (uint8_t)((uint64_t)uploaded * 100 / total) : 0;
    // Throttle serial log to once per second OR each 5% step.
    bool pct_step = (total > 0) && (g_progress_last_pct == 255 || pct >= g_progress_last_pct + 5);
    bool time_step = (now - g_progress_last_log_ms) >= 1000;
    if (pct_step || time_step) {
      uint32_t elapsed = now - g_progress_start_ms;
      uint32_t kbps = elapsed ? (uint32_t)((uint64_t)uploaded * 1000 / 1024 / elapsed) : 0;
      if (total > 0) {
        ESP_LOGI(TAG, "Upload %s: %u%% (%u/%u KB) %u KB/s",
                 filename ? filename : "?", pct,
                 (unsigned)(uploaded / 1024), (unsigned)(total / 1024), (unsigned)kbps);
      } else {
        ESP_LOGI(TAG, "Upload %s: %u KB %u KB/s",
                 filename ? filename : "?", (unsigned)(uploaded / 1024), (unsigned)kbps);
      }
      g_progress_last_log_ms = now;
      g_progress_last_pct = pct;
    }
  });

  wifi_uploader.onUploadComplete([](const char *filename, uint32_t filesize, bool success) {
    uint32_t elapsed = millis() - g_progress_start_ms;
    uint32_t kbps = elapsed ? (uint32_t)((uint64_t)filesize * 1000 / 1024 / elapsed) : 0;
    ESP_LOGI(TAG, "Upload %s: %s (%u bytes, %u ms, %u KB/s)",
             filename ? filename : "?", success ? "OK" : "FAIL",
             (unsigned)filesize, (unsigned)elapsed, (unsigned)kbps);
    if (g_progress_renderer) {
      char toast[128];
      if (success) {
        snprintf(toast, sizeof(toast), "Uploaded %s (%u KB, %u KB/s)",
                 filename ? filename : "?", (unsigned)(filesize / 1024), (unsigned)kbps);
      } else {
        snprintf(toast, sizeof(toast), "Upload failed: %s", filename ? filename : "?");
      }
      show_status_bar_toast(g_progress_renderer, toast);
      g_progress_renderer->flush_display();
    }
  });

  wifi_uploader.onUploadError([](const char *filename, uint8_t code, const char *message) {
    ESP_LOGW(TAG, "Upload error %s code=%u: %s",
             filename ? filename : "?", (unsigned)code, message ? message : "");
  });

  if (!wifi_uploader.begin(WIFI_UPLOAD_PORT, WIFI_UPLOAD_PATH))
  {
    show_status_bar_toast(renderer, "WiFi upload start failed");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  IPAddress ip = WiFi.softAPIP();
  char toast[96];
  snprintf(toast, sizeof(toast), "WiFi AP %s %s", WIFI_UPLOAD_AP_SSID, ip.toString().c_str());
  show_status_bar_toast(renderer, toast);
  return true;
}

void stop_wifi_uploader(Renderer *renderer, bool show_toast)
{
  if (!wifi_uploader.isRunning()) return;

  wifi_uploader.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_progress_renderer = nullptr;

  if (show_toast)
    show_status_bar_toast(renderer, "WiFi upload stopped");
}

#endif // BOARD_TYPE_M5_PAPER
