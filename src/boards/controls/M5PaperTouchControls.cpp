#include "M5PaperTouchControls.h"
#include <M5Unified.h>
#include <esp_log.h>

static const char *TAG = "M5Touch";

M5PaperTouchControls::M5PaperTouchControls(QueueHandle_t ui_queue) : TouchControls(ui_queue)
{
}

void M5PaperTouchControls::run()
{
    // Note: M5.update() is called by M5PaperButtonControls::run()
    // Don't call it again here to avoid consuming events twice
    
    auto touch_detail = M5.Touch.getDetail();
    if (touch_detail.wasPressed())
    {
        int x = touch_detail.x;
        int y = touch_detail.y;
        int width = M5.Display.width();
        
        ESP_LOGE(TAG, "Touch detected at x=%d, y=%d, width=%d", x, y, width);
        
        if (x < width / 3)
        {
            ESP_LOGE(TAG, "Touch zone: LEFT -> UP");
            send_action(UP);
        }
        else if (x > width * 2 / 3)
        {
            ESP_LOGE(TAG, "Touch zone: RIGHT -> DOWN");
            send_action(DOWN);
        }
        else
        {
            ESP_LOGE(TAG, "Touch zone: CENTER -> SELECT");
            send_action(SELECT);
        }
    }
}

void M5PaperTouchControls::render(Renderer *renderer)
{
}

void M5PaperTouchControls::renderPressedState(Renderer *renderer, UIAction action, bool state)
{
}