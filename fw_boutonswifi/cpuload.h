// CPU load measuring utility
#pragma once

#include "fraise.h"
#include <string>

#ifndef CPULOAD_H
#define CPULOAD_H

class CpuLoad {
private:
    absolute_time_t sensor_time;
    absolute_time_t reset_time;
    unsigned int count_us = 0;
    std::string name;
    unsigned int sensor_count = 0;
public:
    CpuLoad(std::string n): name(n) {}
    void start() {
        sensor_time = get_absolute_time();
    }
    void stop() {
        count_us += absolute_time_diff_us(sensor_time, get_absolute_time());
        sensor_count++;
    }
    float round(float f) {
        return 0.1 * (int)(f * 10);
    }
    float get_load() {
        int delta_us = absolute_time_diff_us(reset_time, get_absolute_time());
        float load = (100.0 * count_us) / delta_us;
        float fps = (sensor_count * 1000000.0f) / delta_us;
        fraise_printf("l %s %f %d %f\n", name.c_str(), round(load), count_us / sensor_count, round(fps));
        return load;
    }
    void reset() {
        count_us = 0;
        sensor_count = 0;
        reset_time = get_absolute_time();
    }
};
#endif

