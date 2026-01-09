#pragma once

#include "boards/controls/ButtonControls.h"

class M5PaperButtonControls : public ButtonControls
{
public:
    M5PaperButtonControls(QueueHandle_t ui_queue);
    void run() override;
    bool did_wake_from_deep_sleep() override;
    UIAction get_deep_sleep_action() override;
    void setup_deep_sleep() override;
};