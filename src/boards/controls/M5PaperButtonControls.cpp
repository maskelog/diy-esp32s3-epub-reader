#include "M5PaperButtonControls.h"
#include <M5Unified.h>

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
    return false;
}

UIAction M5PaperButtonControls::get_deep_sleep_action()
{
    return NONE;
}

void M5PaperButtonControls::setup_deep_sleep()
{
}