#pragma once

#include "boards/controls/TouchControls.h"

class M5PaperTouchControls : public TouchControls
{
public:
    M5PaperTouchControls(QueueHandle_t ui_queue);
    void run() override;
    void render(Renderer *renderer) override;
    void renderPressedState(Renderer *renderer, UIAction action, bool state = true) override;
};