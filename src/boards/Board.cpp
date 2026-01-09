#include <esp_log.h>
#include "Board.h"
#if !defined(BOARD_TYPE_M5_PAPER)
#include "PaperS3.h"
#include <SDCard.h>
#include "battery/ADCBattery.h"
#else
#include "M5Paper.h"
#include "battery/M5PaperBattery.h"
#endif

Board *Board::factory()
{
#if defined(BOARD_TYPE_M5_PAPER)
  return new M5Paper();
#else
  return new PaperS3();
#endif
}

void Board::start_filesystem()
{
#if !defined(BOARD_TYPE_M5_PAPER)
  ESP_LOGI("main", "Using SDCard");
  // initialise the SDCard
  sdcard = new SDCard("/fs", SD_CARD_PIN_NUM_MISO, SD_CARD_PIN_NUM_MOSI, SD_CARD_PIN_NUM_CLK, SD_CARD_PIN_NUM_CS);
#endif
}

void Board::stop_filesystem()
{
#if !defined(BOARD_TYPE_M5_PAPER)
  delete sdcard;
#endif
}

Battery *Board::get_battery()
{
#if defined(BOARD_TYPE_M5_PAPER)
  return new M5PaperBattery();
#elif defined(BATTERY_ADC_CHANNEL)
  return new ADCBattery(BATTERY_ADC_CHANNEL);
#else
  return nullptr;
#endif
}

TouchControls *Board::get_touch_controls(Renderer *renderer, QueueHandle_t ui_queue)
{
  return new TouchControls(ui_queue);
}
