#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Actions.h"

class ButtonControls
{
protected:
    QueueHandle_t m_ui_queue;
public:
    ButtonControls(QueueHandle_t ui_queue) : m_ui_queue(ui_queue) {}
    virtual void run() {};
    virtual bool did_wake_from_deep_sleep() = 0;
    virtual UIAction get_deep_sleep_action() = 0;
    virtual void setup_deep_sleep() = 0;
    void send_action(UIAction action)
    {
        xQueueSend(m_ui_queue, &action, 0);
    }
};