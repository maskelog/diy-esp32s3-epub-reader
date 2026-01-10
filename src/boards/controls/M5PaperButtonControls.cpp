#include "M5PaperButtonControls.h"
#include <M5Unified.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

// M5Paper button GPIOs
#define M5PAPER_BTN_A GPIO_NUM_37
#define M5PAPER_BTN_B GPIO_NUM_38
#define M5PAPER_BTN_C GPIO_NUM_39

M5PaperButtonControls::M5PaperButtonControls(QueueHandle_t ui_queue) : ButtonControls(ui_queue)
{
}

void M5PaperButtonControls::run()
{
    M5.update();
    if (M5.BtnA.wasPressed())
    {
        send_action(DOWN);
    }
    if (M5.BtnB.wasPressed())
    {
        send_action(SELECT);
    }
    if (M5.BtnC.wasPressed())
    {
        send_action(UP);
    }
}

bool M5PaperButtonControls::did_wake_from_deep_sleep()
{
    auto wake_cause = esp_sleep_get_wakeup_cause();
    return wake_cause == ESP_SLEEP_WAKEUP_EXT0;
}

UIAction M5PaperButtonControls::get_deep_sleep_action()
{
    if (!did_wake_from_deep_sleep())
    {
        return NONE;
    }
    // Woke up from Button B (SELECT)
    return SELECT;
}

void M5PaperButtonControls::setup_deep_sleep()
{
    // M5Paper buttons are active LOW (pressed = LOW, released = HIGH)
    // Use EXT0 wakeup for single button - Button B (middle/SELECT)
    // EXT0 allows waking on LOW level for a single GPIO
    gpio_num_t wakeup_gpio = M5PAPER_BTN_B;
    
    rtc_gpio_init(wakeup_gpio);
    rtc_gpio_set_direction(wakeup_gpio, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(wakeup_gpio);
    rtc_gpio_pulldown_dis(wakeup_gpio);
    
    // Enable EXT0 wakeup on Button B press (LOW level = 0)
    esp_sleep_enable_ext0_wakeup(wakeup_gpio, 0);
}