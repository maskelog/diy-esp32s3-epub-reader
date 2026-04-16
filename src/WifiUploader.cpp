#if defined(BOARD_TYPE_M5_PAPER)

#include "WifiUploader.h"
#include "StatusBar.h"
#include "config.h"
#include <M5Unified.h>
#include <WiFi.h>
#include "M5StackWiFiUploader.h"
#include <esp_log.h>

static const char *TAG = "WifiUploader";

static M5StackWiFiUploader wifi_uploader;

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

  if (show_toast)
    show_status_bar_toast(renderer, "WiFi upload stopped");
}

#endif // BOARD_TYPE_M5_PAPER
