#include "bmp_sensor.h"

bool BMPSensor::begin() {
    return bmp.begin();
}

float BMPSensor::readTemperature() {
    return bmp.readTemperature();
}

float BMPSensor::readPressure() {
    return bmp.readPressure();
}

float BMPSensor::readAltitude(float seaLevelhPa) {
    return bmp.readAltitude(seaLevelhPa);
}
