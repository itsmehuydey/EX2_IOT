
#include <Adafruit_BMP085.h>
class BMPSensor{
  public:
    bool begin();
    float readTemperature();
    float readPressure();
    float readAltitude(float seaLevelhPa = 1013.25);

  private:
    Adafruit_BMP085 bmp;
};
