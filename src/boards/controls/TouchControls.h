#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Actions.h"

class Renderer;

// TODO - we should move the rendering out of this class so that it's only doing the touch detection
class TouchControls
{
protected:
    QueueHandle_t m_ui_queue;
public:
  TouchControls(QueueHandle_t ui_queue) : m_ui_queue(ui_queue) {};
  // draw the controls on the screen
  virtual void render(Renderer *renderer) {}
  // show the touched state
  virtual void renderPressedState(Renderer *renderer, UIAction action, bool state = true) {}
  virtual void run() {};

  void send_action(UIAction action)
  {
      xQueueSend(m_ui_queue, &action, 0);
  }
};