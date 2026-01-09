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
    
    // M5Paper SD 카드 초기화: GPIO 4번 핀 사용, SPI, 25MHz
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
        ESP_LOGE(TAG, "SD Card initialization failed!");
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