# 🌱 Smart Environmental Monitoring System | IoT + CoreIoT + ESP32

🎓 This is a real-time IoT-based environmental monitoring and control system built for smart agricultural factories. It uses a combination of multiple environmental sensors, real-time telemetry via MQTT/Token, and a centralized dashboard on CoreIoT (ThingsBoard). The system ensures worker safety, fire risk detection, intrusion alerts, and supports remote control of devices such as fans and lights. 🚨🌡️🧠

---

## 🧩 Features

🧪 **Multi-Sensor Support**
- MQ-135: Detects CO, CO₂, NH₃, Toluene, Acetone
- MQ-2: Detects flammable gases & smoke
- DHT20: Monitors temperature & humidity
- LDR: Light intensity monitoring
- PIR: Human motion detection

⏱️ **Real-Time Multitasking with RTOS Scheduler**
- Modular sensor reading, LCD updates, telemetry pushing, and RPC control via periodic tasks

📡 **Cloud Connection via MQTT + WiFi**
- Sends telemetry to [ThingsBoard / CoreIoT](https://thingsboard.io/)
- Reconnects WiFi and MQTT automatically if connection is lost

🎮 **Remote Device Control via RPC**
- Turn lights and fans ON/OFF via dashboard buttons or rules

⚠️ **Smart Alert System**
- Calculates an `alertScore` from gas levels, temperature, humidity, light, and motion
- Categorizes system state into: `Safe`, `Warning`, or `Danger`
- Detects fire via MQ2 gas + temperature thresholds

💬 **LCD Display Interface**
- Displays temperature, humidity, light intensity, alerts, and motion detection on a 16x2 I2C LCD

🔌 **RS485 Modbus Communication**
- UART-based communication for future sensor/actuator expansion via RS485 protocol

---

## ⚙️ Built With

- `ESP32 (Yolo UNO)` as core microcontroller
- `MQUnifiedsensor` library for advanced gas calibration (MQ2/MQ135)
- `FreeRTOS-style software scheduler` for task management
- `Arduino ThingsBoard SDK` for MQTT communication
- `DHT20`, `LiquidCrystal_I2C`, `Adafruit_NeoPixel`, `Wire`, `ArduinoOTA`, `WiFi`, `driver/uart`

---

## 🗂️ Folder Structure

smart-iot-factory/
├── lib/
├── src/
│ ├── main.cpp
│ └── http_firmware_client.py
├── include/
├── test/
├── platformio.ini
├── README.md

## 🔐 Configuration

Make sure to fill in these values inside `main.cpp`:

```cpp
constexpr char WIFI_SSID[] = "YourWiFiName";
constexpr char WIFI_PASSWORD[] = "YourWiFiPassword";
constexpr char TOKEN[] = "YourThingsBoardDeviceToken";
constexpr char THINGSBOARD_SERVER[] = "your.iot.server.com";
```

## 📌 Reference
....

