#include "M5PaperTouchControls.h"
#include <M5Unified.h>

M5PaperTouchControls::M5PaperTouchControls(QueueHandle_t ui_queue) : TouchControls(ui_queue)
{
}

void M5PaperTouchControls::run()
{
    M5.update();
    auto touch_detail = M5.Touch.getDetail();
    if (touch_detail.wasPressed())
    {
        if (touch_detail.x < M5.Display.width() / 3)
        {
            send_action(UP);
        }
        else if (touch_detail.x > M5.Display.width() * 2 / 3)
        {
            send_action(DOWN);
        }
        else
        {
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