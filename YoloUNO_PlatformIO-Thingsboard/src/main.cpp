#define LIGHT_SENSOR_PIN GPIO_NUM_2
#define LIGHT_LED_PIN GPIO_NUM_1
#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12
#define MQ2_PIN GPIO_NUM_6
#define PIR_PIN GPIO_NUM_8
#define MQ135_PIN GPIO_NUM_17 //D8
#define LED_RGB_PIN 45
#define NUMPIXELS 1

#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include <MQ135.h>
#include "Wire.h"
#include <ArduinoOTA.h>
#include <time.h>
#include "app_scheduler.h"
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
// #include <bmp_sensor.h>
// #include <MQ135.h>
// #include <MFRC522.h>

constexpr char WIFI_SSID[] = "hcmut";
constexpr char WIFI_PASSWORD[] = "123321000";
// constexpr char WIFI_SSID[] = "iPhone";
// constexpr char WIFI_PASSWORD[] = "777888111000";
// constexpr char TOKEN[] = "qxu9tl8c2pv2pmbn781w";
constexpr char TOKEN[] = "bMkdOyTYGWbyT2796kbo";
constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr char NTP_SERVER[] = "pool.ntp.org";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

constexpr char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr char LED_MODE_ATTR[] = "ledMode";
constexpr char LED_STATE_ATTR[] = "ledState";
constexpr char LED_CONTROL_ATTR[] = "LED";

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;

LiquidCrystal_I2C lcd(0x21, 16, 2);

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange = 0;
constexpr int16_t telemetrySendInterval = 500U;

enum DisplayState
{
    DHT_20,
    MQ2,
    MQ135
};
DisplayState currentDisplay = DHT_20;
uint32_t lastDisplaySwitch = 0;
const uint32_t DISPLAY_INTERVAL = 2000U; // 2 seconds
bool motionDetected = false;
uint32_t motionDisplayStart = 0;
const uint32_t MOTION_DISPLAY_DURATION = 2000U; // 2 seconds

Adafruit_NeoPixel pixels(NUMPIXELS, LED_RGB_PIN, NEO_GRB + NEO_KHZ800);

#define ADC_VREF 3.3
#define ADC_RESOLUTION 4095.0
volatile bool mq135_warmed_up = false;
uint32_t mq135_warmup_start = 0;
const uint32_t MQ135_WARMUP_DURATION = 60000;

constexpr std::array<const char *, 3U> SHARED_ATTRIBUTES_LIST = {
    LED_STATE_ATTR,
    BLINKING_INTERVAL_ATTR,
    LED_CONTROL_ATTR};

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);
DHT20 dht20;

// Hàm lấy thời gian hiện tại
String getCurrentTime()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// Hàm cập nhật trạng thái LED
void updateLedState(bool newState)
{
    ledState = newState;
    digitalWrite(LED_PIN, ledState);
    Serial.print("LED state updated to: ");
    Serial.println(ledState);
    tb.sendAttributeData(LED_STATE_ATTR, ledState);
    tb.sendAttributeData(LED_CONTROL_ATTR, ledState ? "ON" : "OFF");
    attributesChanged = true;
}

// RPC callback
RPC_Response setLedSwitchState(const RPC_Data &data)
{
    Serial.println("Received Switch state via RPC");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    updateLedState(newState);
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
    RPC_Callback{"setLedSwitchValue", setLedSwitchState}};

// Shared attributes callback
void processSharedAttributes(const Shared_Attribute_Data &data)
{
    for (auto it = data.begin(); it != data.end(); ++it)
    {
        if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0)
        {
            const uint16_t new_interval = it->value().as<uint16_t>();
            if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX)
            {
                blinkingInterval = new_interval;
                Serial.print("Blinking interval is set to: ");
                Serial.println(new_interval);
            }
        }
        else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0)
        {
            bool newState = it->value().as<bool>();
            updateLedState(newState);
        }
        else if (strcmp(it->key().c_str(), LED_CONTROL_ATTR) == 0)
        {
            String ledControl = it->value().as<String>();
            Serial.print("LED control received: ");
            Serial.println(ledControl);
            if (ledControl == "ON")
            {
                updateLedState(true);
            }
            else if (ledControl == "OFF")
            {
                updateLedState(false);
            }
            else
            {
                Serial.println("Unknown LED control value");
            }
        }
    }
    attributesChanged = true;
}

const Shared_Attribute_Callback attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback attribute_shared_request_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());

// Các task cho scheduler
void task_InitWiFi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Connecting to AP ...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED)
        {
            delay(500);
            Serial.print(".");
        }
        Serial.println("Connected to AP");

        // NTP time synchronization after WiFi connection
        configTime(7 * 3600, 0, NTP_SERVER, "time.nist.gov"); // GMT+7
        Serial.println("Waiting for NTP time sync...");

        // Check if time is synced without using while loop
        if (time(nullptr) > 100000)
        {
            Serial.println("Time synced");
        }
        else
        {
            // You can add a timeout or handle this case if needed
            Serial.println("Failed to sync time, retrying...");
        }
    }
}

void task_ThingsBoardConnect()
{
    if (!tb.connected())
    {
        Serial.print("Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT))
        {
            Serial.println("Failed to connect");
            return;
        }
        tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
        Serial.println("Subscribing for RPC...");
        if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend()))
        {
            Serial.println("Failed to subscribe for RPC");
            return;
        }
        if (!tb.Shared_Attributes_Subscribe(attributes_callback))
        {
            Serial.println("Failed to subscribe for shared attribute updates");
            return;
        }
        Serial.println("Subscribe done");
        if (!tb.Shared_Attributes_Request(attribute_shared_request_callback))
        {
            Serial.println("Failed to request for shared attributes");
            return;
        }
    }
}

void task_SendTelemetry()
{
    // Đọc dữ liệu từ cảm biến DHT20
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();

    if (isnan(temperature) || isnan(humidity))
    {
        Serial.println("Failed to read from DHT20 sensor!");
        updateTemperatureLED(temperature);
    }
    else
    {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.print(" °C, Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");

        tb.sendTelemetryData("temperature", temperature);
        tb.sendTelemetryData("humidity", humidity);
    }

    // Gửi dữ liệu thời gian và thông tin mạng
    String currentTime = getCurrentTime();
    Serial.print("Current time sent: ");
    Serial.println(currentTime);

    tb.sendAttributeData("currentTime", currentTime.c_str());
    tb.sendAttributeData("rssi", WiFi.RSSI());
    tb.sendAttributeData("channel", WiFi.channel());
    tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid", WiFi.SSID().c_str());

    // Đọc và gửi dữ liệu cảm biến MQ2
    int mq2Value = analogRead(MQ2_PIN);
    if (mq2Value >= 0 && mq2Value <= 4095)
    {
        Serial.print("MQ-2 Gas Sensor Value: ");
        Serial.println(mq2Value);
        tb.sendTelemetryData("mq2_gas", mq2Value);
    }
    else
    {
        Serial.println("Invalid MQ-2 sensor value!");
    }

    // Cảm biến PIR
    int pirValue = digitalRead(PIR_PIN);
    Serial.print("PIR Motion Detected: ");
    Serial.println(pirValue);
    // tb.sendTelemetryData("motion_detected", pirValue);
    if (pirValue == 1)
    {
        tb.sendTelemetryData("motion_detected", "Deteched some one!");
    }
    else
    {
        tb.sendTelemetryData("motion_detected", "Nobody is deteched");
    }

    // Đọc và gửi dữ liệu cảm biến ánh sáng
    int lightValue = analogRead(LIGHT_SENSOR_PIN);
    if (lightValue >= 0 && lightValue <= 4095)
    {
        Serial.print("Light Sensor Value: ");
        Serial.println(lightValue);
        tb.sendTelemetryData("light_intensity", lightValue);

        // Điều khiển đèn bằng PWM: bật hết mức hoặc tắt
        if (lightValue < 100)
        {
            digitalWrite(LIGHT_LED_PIN, HIGH);
        }
        else
        {
            digitalWrite(LIGHT_LED_PIN, LOW);
        }
    }
    else
    {
        Serial.println("Invalid light sensor value!");
        digitalWrite(LIGHT_LED_PIN, LOW);
    }

    // if (mq135_warmed_up)
    // {
    //     int rawValue = 0;
    //     for (int i = 0; i < 10; i++)
    //     {
    //         rawValue += analogRead(MQ135_PIN);
    //         delay(10);
    //     }
    //     rawValue /= 10;
    //     float voltage = (rawValue / ADC_RESOLUTION) * ADC_VREF;
    //     float rzero = mq135_sensor.getRZero();
    //     float ppm = (isnan(temperature) || isnan(humidity)) ? mq135_sensor.getPPM() : mq135_sensor.getCorrectedPPM(temperature, humidity);
    //     float rs = mq135_sensor.getResistance();
    //     float ppm_percent = (ppm / 10000) * 100;
    //     float co_ppm = 116.6020682 * pow(rs / rzero, -2.769034857);
    //     float alcohol_ppm = 605.995 * pow(rs / rzero, -3.013);
    //     float toluene_ppm = 44.947 * pow(rs / rzero, -3.445);
    //     float nh4_ppm = 102.2 * pow(rs / rzero, -2.473);
    //     float acetone_ppm = 34.668 * pow(rs / rzero, -3.369);

    //     if (ppm >= 0 && !isnan(ppm) && rzero > 0)
    //     {
    //         Serial.print("MQ135 - CO2: ");
    //         Serial.print(ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_co2", ppm);
    //     }
    //     if (rzero > 0)
    //     {
    //         Serial.print("MQ135 - RZero: ");
    //         Serial.print(rzero);
    //         Serial.println(" kOhm");
    //         tb.sendTelemetryData("mq135_rzero", rzero);
    //     }
    //     if (voltage >= 0)
    //     {
    //         Serial.print("MQ135 - Voltage: ");
    //         Serial.print(voltage);
    //         Serial.println(" V");
    //         tb.sendTelemetryData("mq135_voltage", voltage);
    //     }
    //     Serial.print("MQ135 - CO2 Percent: ");
    //     Serial.print(ppm_percent);
    //     Serial.println(" %");
    //     tb.sendTelemetryData("mq135_co2_percent", ppm_percent);
    //     if (co_ppm >= 0 && !isnan(co_ppm))
    //     {
    //         Serial.print("MQ135 - CO: ");
    //         Serial.print(co_ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_co", co_ppm);
    //     }
    //     if (alcohol_ppm >= 0 && !isnan(alcohol_ppm))
    //     {
    //         Serial.print("MQ135 - Alcohol: ");
    //         Serial.print(alcohol_ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_alcohol", alcohol_ppm);
    //     }
    //     if (toluene_ppm >= 0 && !isnan(toluene_ppm))
    //     {
    //         Serial.print("MQ135 - Toluene: ");
    //         Serial.print(toluene_ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_toluene", toluene_ppm);
    //     }
    //     if (nh4_ppm >= 0 && !isnan(nh4_ppm))
    //     {
    //         Serial.print("MQ135 - NH4: ");
    //         Serial.print(nh4_ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_nh4", nh4_ppm);
    //     }
    //     if (acetone_ppm >= 0 && !isnan(acetone_ppm))
    //     {
    //         Serial.print("MQ135 - Acetone: ");
    //         Serial.print(acetone_ppm);
    //         Serial.println(" ppm");
    //         tb.sendTelemetryData("mq135_acetone", acetone_ppm);
    //     }
    // }
    // else
    // {
    //     Serial.println("MQ135 not warmed up yet");
    // }

}

void task_UpdateLCD()
{
    static float lastTemperature = 0.0;
    static float lastHumidity = 0.0;
    static int lastMQ2Value = 0;

    // Update sensor values if valid
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();
    if (!isnan(temperature) && !isnan(humidity))
    {
        lastTemperature = temperature;
        lastHumidity = humidity;
    }
    int mq2Value = analogRead(MQ2_PIN);
    if (mq2Value >= 0 && mq2Value <= 4095)
    {
        lastMQ2Value = mq2Value;
    }

    // Check for motion
    int pirValue = digitalRead(PIR_PIN);
    if (pirValue == HIGH && !motionDetected)
    {
        motionDetected = true;
        motionDisplayStart = millis();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Motion Detected!");
    }

    // Handle LCD display
    if (motionDetected)
    {
        if (millis() - motionDisplayStart >= MOTION_DISPLAY_DURATION)
        {
            motionDetected = false;
            lcd.clear();
            currentDisplay = DHT_20; // Reset to DHT20 display
            lastDisplaySwitch = millis();
        }
    }
    else
    {
        // Alternate between DHT20 and MQ2 every 2 seconds
        if (millis() - lastDisplaySwitch >= DISPLAY_INTERVAL)
        {
            lcd.clear();
            currentDisplay = (currentDisplay == DHT_20) ? MQ2 : DHT_20;
            lastDisplaySwitch = millis();
        }

        if (currentDisplay == DHT_20)
        {
            lcd.setCursor(0, 0);
            lcd.print("Temp:       C");
            lcd.setCursor(6, 0);
            lcd.print(lastTemperature, 1);
            lcd.print("  ");
            lcd.setCursor(0, 1);
            lcd.print("Humid:       %");
            lcd.setCursor(7, 1);
            lcd.print(lastHumidity, 1);
            lcd.print("  ");
        }
        else // MQ2
        {
            lcd.setCursor(0, 0);
            lcd.print("Gas Sensor      ");
            lcd.setCursor(0, 1);
            lcd.print("MQ2:         ppm");
            lcd.setCursor(5, 1);
            lcd.print(lastMQ2Value);
            lcd.print("  ");
        }
    }
}


void task_BlinkLED()
{
    if (millis() - previousStateChange >= blinkingInterval)
    {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
        tb.sendAttributeData(LED_STATE_ATTR, ledState);
        previousStateChange = millis();
    }
}

void task_ProcessTB()
{
    tb.loop();
    if (attributesChanged)
    {
        attributesChanged = false;
    }
}

void setup()
{
    Serial.begin(SERIAL_DEBUG_BAUD);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(LIGHT_LED_PIN, OUTPUT);
    digitalWrite(LIGHT_LED_PIN, LOW);
    Wire.begin(SDA_PIN, SCL_PIN);
    dht20.begin();
    pinMode(PIR_PIN, INPUT);
    pinMode(MQ135_PIN, INPUT);

    lcd.init();
    lcd.backlight();
    Serial.println("LCD initialized");

    SCH_Init();
    SCH_Add_Task(task_InitWiFi, 0, 500);
    SCH_Add_Task(task_ThingsBoardConnect, 0, 1000);
    SCH_Add_Task(task_SendTelemetry, 0, telemetrySendInterval);
    SCH_Add_Task(task_UpdateLCD, 0, 100);
    SCH_Add_Task(task_ProcessTB, 0, 10);
}

void loop()
{
    SCH_Dispatch_Tasks();
}

