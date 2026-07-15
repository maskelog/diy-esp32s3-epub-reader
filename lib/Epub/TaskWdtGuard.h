#pragma once

#ifndef UNIT_TEST
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Suspends the current task's TWDT subscription for the enclosing scope and
// restores it on destruction (only if the task was actually subscribed).
class TaskWdtGuard
{
public:
  TaskWdtGuard() : m_was_subscribed(esp_task_wdt_delete(xTaskGetCurrentTaskHandle()) == ESP_OK) {}
  ~TaskWdtGuard()
  {
    if (m_was_subscribed)
    {
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
  }
  TaskWdtGuard(const TaskWdtGuard &) = delete;
  TaskWdtGuard &operator=(const TaskWdtGuard &) = delete;

private:
  bool m_was_subscribed;
};
#else
// No-op for native unit-test builds.
class TaskWdtGuard
{
};
#endif
