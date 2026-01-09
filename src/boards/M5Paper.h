#pragma once

#include "Board.h"
#include "Renderer/M5GfxRenderer.h"

class M5Paper : public Board
{
public:
  void power_up() override;
  void start_filesystem() override;
  void stop_filesystem() override;
  void prepare_to_sleep() override;
  Renderer *get_renderer() override;
  ButtonControls *get_button_controls(QueueHandle_t ui_queue) override;
  TouchControls *get_touch_controls(Renderer *renderer, QueueHandle_t ui_queue) override;
};