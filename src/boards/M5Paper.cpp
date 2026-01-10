#if defined(BOARD_TYPE_M5_PAPER)

#include "M5Paper.h"
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include "Renderer/M5GfxRenderer.h"
#include "controls/M5PaperButtonControls.h"
#include "controls/M5PaperTouchControls.h"
#include "battery/M5PaperBattery.h"
#include <esp_log.h>

static const char *TAG = "M5Paper";

void M5Paper::power_up()
{
    M5.begin();
}

void M5Paper::start_filesystem()
{
    ESP_LOGI(TAG, "Initializing SD card for M5Paper");

    // Add delay before SD card initialization to stabilize power
    delay(100);

    // M5Paper SD 카드 초기화: GPIO 4번 핀 사용, SPI, 25MHz
    // Retry up to 3 times with increasing delays
    bool sd_mounted = false;
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "SD Card mount retry %d/3", retry + 1);
            delay(500 * retry); // Increasing delay: 0ms, 500ms, 1000ms
        }

        if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
            sd_mounted = true;
            break;
        }
    }

    if (!sd_mounted) {
        ESP_LOGE(TAG, "SD Card initialization failed after 3 attempts!");
        ESP_LOGE(TAG, "Please check:");
        ESP_LOGE(TAG, "- SD card is inserted");
        ESP_LOGE(TAG, "- SD card is formatted as FAT32");
        ESP_LOGE(TAG, "- SD card contacts are clean");
        return;
    }

    ESP_LOGI(TAG, "SD Card mounted successfully!");

    // SD 카드 정보 출력
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    ESP_LOGI(TAG, "SD Card Size: %llu MB", cardSize);
    ESP_LOGI(TAG, "SD Card Type: %d", SD.cardType());
}

void M5Paper::stop_filesystem()
{
    // SD 라이브러리는 자동으로 처리되므로 특별한 작업 불필요
    ESP_LOGI(TAG, "SD Card filesystem stopped");
}

void M5Paper::prepare_to_sleep()
{
    // M5Unified handles this
}

Renderer *M5Paper::get_renderer()
{
    return new M5GfxRenderer();
}

ButtonControls *M5Paper::get_button_controls(QueueHandle_t ui_queue)
{
    return new M5PaperButtonControls(ui_queue);
}

TouchControls *M5Paper::get_touch_controls(Renderer *renderer, QueueHandle_t ui_queue)
{
    return new M5PaperTouchControls(ui_queue);
}

#endif