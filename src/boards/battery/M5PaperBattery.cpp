#include "M5PaperBattery.h"
#include <M5Unified.h>

M5PaperBattery::M5PaperBattery() {}

void M5PaperBattery::setup() {
    // M5.Power is managed by M5.begin()
}

float M5PaperBattery::get_voltage()
{
    return M5.Power.getBatteryVoltage();
}

int M5PaperBattery::get_percentage()
{
    return M5.Power.getBatteryLevel();
}