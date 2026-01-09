#pragma once

#include "boards/battery/Battery.h"

class M5PaperBattery : public Battery
{
public:
    M5PaperBattery();
    void setup();
    float get_voltage();
    int get_percentage();
};