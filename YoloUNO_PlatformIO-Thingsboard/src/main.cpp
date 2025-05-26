#include <Arduino.h>
#define LIGHT_SENSOR_PIN GPIO_NUM_2
#define LIGHT_LED_PIN GPIO_NUM_1
#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12
#define MQ2_PIN GPIO_NUM_6
#define PIR_PIN GPIO_NUM_8
#define MQ135_PIN GPIO_NUM_17 
#define FAN_PIN GPIO_NUM_10
#define RS485_TX_PIN GPIO_NUM_43
#define RS485_RX_PIN GPIO_NUM_44
#define RS485_SERIAL_PORT 2


#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>
#include <time.h>
#include "app_scheduler.h"
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <MQUnifiedsensor.h>
#include "driver/uart.h"

constexpr char WIFI_SSID[] = "ACLAB";
constexpr char WIFI_PASSWORD[] = "ACLAB2023";
constexpr char TOKEN[] = "PxwcRolAPNKFmuHcKkeS";
// constexpr char TOKEN[] = "bMkdOyTYGWbyT2796kbo";
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

constexpr float CO2_THRESHOLD = 5000.0;      // ppm, giới hạn phơi nhiễm ngắn hạn trong công nghiệp  
constexpr float CO_THRESHOLD = 25.0;         // ppm, giới hạn an toàn nghiêm ngặt hơn cho nhà máy  
constexpr float ALCOHOL_THRESHOLD = 1000.0;  // ppm, ethanol trong môi trường sản xuất hóa chất  
constexpr float TOLUENE_THRESHOLD = 50.0;    // ppm, giới hạn nghiêm ngặt do độc tính cao  
constexpr float NH4_THRESHOLD = 25.0;        // ppm, ammonia có nguy cơ cao trong nhà máy  
constexpr float ACETONE_THRESHOLD = 500.0;   // ppm, phù hợp với môi trường sử dụng dung môi  
constexpr float TEMP_THRESHOLD = 45.0;       // °C, điều kiện khắc nghiệt trong nhà máy  
constexpr float HUMIDITY_THRESHOLD = 90.0;   // %, môi trường ẩm công nghiệp  
constexpr float MQ2_THRESHOLD = 2500.0;      // giá trị ADC, phát hiện khí dễ cháy  
constexpr float LIGHT_THRESHOLD = 300.0;     // giá trị ADC, ánh sáng tối thiểu cho an toàn lao động  


constexpr float W_CO2 = 0.3;
constexpr float W_CO = 0.25;
constexpr float W_ALCOHOL = 0.1;
constexpr float W_TOLUENE = 0.1;
constexpr float W_NH4 = 0.1;
constexpr float W_ACETONE = 0.1;
constexpr float W_TEMP = 0.1;
constexpr float W_HUMIDITY = 0.05;
constexpr float W_MQ2 = 0.05;
constexpr float W_LIGHT = 0.03;
constexpr float W_PIR = 0.02;

LiquidCrystal_I2C lcd(0x21, 16, 2);

char rs485Buffer[64];

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

// MQ135 Definitions
#define placa "ESP-32"
#define Voltage_Resolution 5
#define ADC_Bit_Resolution 12
#define RatioMQ135CleanAir 3.6
#define type "MQ-135"
MQUnifiedsensor MQ135(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ135_PIN, type);

uint32_t previousStateChange = 0;
constexpr int16_t telemetrySendInterval = 500U;

enum DisplayState
{
    DHT_20,
    Light,
    Alert
};
DisplayState currentDisplay = DHT_20;
uint32_t lastDisplaySwitch = 0;
const uint32_t DISPLAY_INTERVAL = 2000U; // 2 seconds
bool motionDetected = false;
uint32_t motionDisplayStart = 0;
const uint32_t MOTION_DISPLAY_DURATION = 2000U; // 2 seconds
volatile bool fanState = false;

constexpr char FAN_STATE_ATTR[] = "fanState";
constexpr char FAN_CONTROL_ATTR[] = "FAN";

constexpr std::array<const char *, 5U> SHARED_ATTRIBUTES_LIST = {
    LED_STATE_ATTR,
    BLINKING_INTERVAL_ATTR,
    LED_CONTROL_ATTR,
    FAN_STATE_ATTR,
    FAN_CONTROL_ATTR};

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

void updateFanState(bool newState)
{
    fanState = newState;
    digitalWrite(FAN_PIN, fanState);
    Serial.print("FAN state updated to: ");
    Serial.println(fanState);
    tb.sendAttributeData("fanState", fanState);
    tb.sendAttributeData("FAN", fanState ? "ON" : "OFF");
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

RPC_Response setFanSwitchState(const RPC_Data &data)
{
    Serial.println("Received Fan Switch state via RPC");
    bool newState = data;
    Serial.print("Fan switch state change: ");
    Serial.println(newState);
    updateFanState(newState);
    return RPC_Response("setFanSwitchValue", newState);
}

const std::array<RPC_Callback, 2U> callbacks = {
    RPC_Callback{"setLedSwitchValue", setLedSwitchState},
    RPC_Callback{"setFanSwitchValue", setFanSwitchState}};


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
        else if (strcmp(it->key().c_str(), FAN_STATE_ATTR) == 0)
{
    bool newState = it->value().as<bool>();
    updateFanState(newState);
}
else if (strcmp(it->key().c_str(), FAN_CONTROL_ATTR) == 0)
{
    String fanControl = it->value().as<String>();
    Serial.print("FAN control received: ");
    Serial.println(fanControl);
    if (fanControl == "ON")
    {
        updateFanState(true);
    }
    else if (fanControl == "OFF")
    {
        updateFanState(false);
    }
    else
    {
        Serial.println("Unknown FAN control value");
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

float calculateAlertScore(float co2, float co, float alcohol, float toluene, float nh4, float acetone,
                         float temperature, float humidity, int mq2Value, int lightValue, int pirValue)
{
    float alertScore = (W_CO2 * (co2 / CO2_THRESHOLD)) +
                       (W_CO * (co / CO_THRESHOLD)) +
                       (W_ALCOHOL * (alcohol / ALCOHOL_THRESHOLD)) +
                       (W_TOLUENE * (toluene / TOLUENE_THRESHOLD)) +
                       (W_NH4 * (nh4 / NH4_THRESHOLD)) +
                       (W_ACETONE * (acetone / ACETONE_THRESHOLD)) +
                       (W_TEMP * (temperature / TEMP_THRESHOLD)) +
                       (W_HUMIDITY * (humidity / HUMIDITY_THRESHOLD)) +
                       (W_MQ2 * (mq2Value / MQ2_THRESHOLD)) +
                       (W_LIGHT * (lightValue / LIGHT_THRESHOLD)) +
                       (W_PIR * pirValue);
    return alertScore;
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

    // Đọc và gửi dữ liệu cảm biến MQ135
MQ135.update();

// Đọc các giá trị khí
MQ135.setA(605.18); MQ135.setB(-3.937); // Cấu hình cho CO
float CO = MQ135.readSensor();

MQ135.setA(77.255); MQ135.setB(-3.18); // Cấu hình cho Alcohol
float Alcohol = MQ135.readSensor();

MQ135.setA(110.47); MQ135.setB(-2.862); // Cấu hình cho CO2
float CO2 = 410 + MQ135.readSensor();

MQ135.setA(44.947); MQ135.setB(-3.445); // Cấu hình cho Toluene
float Toluene = MQ135.readSensor();

MQ135.setA(102.2); MQ135.setB(-2.473); // Cấu hình cho NH4
float NH4 = MQ135.readSensor();

MQ135.setA(34.668); MQ135.setB(-3.369); // Cấu hình cho Acetone
float Acetone = MQ135.readSensor();

// Gửi dữ liệu MQ135 đến ThingsBoard
tb.sendTelemetryData("CO", CO);
tb.sendTelemetryData("Alcohol", Alcohol);
tb.sendTelemetryData("CO2", CO2);
tb.sendTelemetryData("Toluene", Toluene);
tb.sendTelemetryData("NH4", NH4);
tb.sendTelemetryData("Acetone", Acetone);

// Đánh giá chất lượng không khí
String airQuality = (CO2 > 425 || CO > 50 || Alcohol > 50 || Toluene > 50 || NH4 > 50 || Acetone > 50) ? "Poor" : "Good";
tb.sendTelemetryData("air_quality", airQuality.c_str());

// Hiển thị giá trị lên Serial Monitor
Serial.println("----------------------------------------------");
Serial.print("CO: "); Serial.print(CO); Serial.println(" ppm");
Serial.print("Alcohol: "); Serial.print(Alcohol); Serial.println(" ppm");
Serial.print("CO2: "); Serial.print(CO2); Serial.println(" ppm");
Serial.print("Toluene: "); Serial.print(Toluene); Serial.println(" ppm");
Serial.print("NH4: "); Serial.print(NH4); Serial.println(" ppm");
Serial.print("Acetone: "); Serial.print(Acetone); Serial.println(" ppm");
Serial.print("Air Quality: "); Serial.println(airQuality);
Serial.println("----------------------------------------------");

float alertScore = calculateAlertScore(CO2, CO, Alcohol, Toluene, NH4, Acetone,
                                      temperature, humidity, mq2Value, lightValue, pirValue);
Serial.print("Alert Score: ");
Serial.println(alertScore);
tb.sendTelemetryData("alert_score", alertScore);

String alertStatus;
if (alertScore > 1.0)
{
    alertStatus = "Danger";
}
else if (alertScore > 0.5)
{
    alertStatus = "Warning";
}
else
{
    alertStatus = "Safe";
}
Serial.print("Alert Status: ");
Serial.println(alertStatus);
tb.sendTelemetryData("alert_status", alertStatus.c_str());

}

void task_UpdateLCD()
{
    static float lastTemperature = 0.0;
    static float lastHumidity = 0.0;
    static int lastLightValue = 0;
    static String lastAlertStatus = "Safe"; // Thêm biến lưu trạng thái cảnh báo

    // Update sensor values if valid
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();
    if (!isnan(temperature) && !isnan(humidity))
    {
        lastTemperature = temperature;
        lastHumidity = humidity;
    }
    int lightValue = analogRead(LIGHT_SENSOR_PIN);
    if (lightValue >= 0 && lightValue <= 4095)
    {
        lastLightValue = lightValue;
    }
    // Cập nhật alertStatus từ ThingsBoard hoặc tính local
    int mq2Value = analogRead(MQ2_PIN);
    int pirValue = digitalRead(PIR_PIN);
    MQ135.update();
    MQ135.setA(605.18); MQ135.setB(-3.937); float CO = MQ135.readSensor();
    MQ135.setA(77.255); MQ135.setB(-3.18); float Alcohol = MQ135.readSensor();
    MQ135.setA(110.47); MQ135.setB(-2.862); float CO2 = 410 + MQ135.readSensor();
    MQ135.setA(44.947); MQ135.setB(-3.445); float Toluene = MQ135.readSensor();
    MQ135.setA(102.2); MQ135.setB(-2.473); float NH4 = MQ135.readSensor();
    MQ135.setA(34.668); MQ135.setB(-3.369); float Acetone = MQ135.readSensor();
    float alertScore = calculateAlertScore(CO2, CO, Alcohol, Toluene, NH4, Acetone,
                                          temperature, humidity, mq2Value, lightValue, pirValue);
    if (alertScore > 1.0)
        lastAlertStatus = "Danger";
    else if (alertScore > 0.5)
        lastAlertStatus = "Warning";
    else
        lastAlertStatus = "Safe";

    // Check for motion
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
            currentDisplay = DHT_20;
            lastDisplaySwitch = millis();
        }
    }
    else
    {
        // Alternate between DHT20, Light, and Alert every 2 seconds
        if (millis() - lastDisplaySwitch >= DISPLAY_INTERVAL)
        {
            lcd.clear();
            currentDisplay = (currentDisplay == DHT_20) ? Light : (currentDisplay == Light) ? Alert : DHT_20;
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
        else if (currentDisplay == Light)
        {
            lcd.setCursor(0, 0);
            lcd.print("Light Sensor");
            lcd.setCursor(0, 1);
            lcd.print("         lux");
            lcd.setCursor(5, 1);
            lcd.print(lastLightValue);
            lcd.print("  ");
        }
        else // Alert
        {
            lcd.setCursor(0, 0);
            lcd.print("Alert Status");
            lcd.setCursor(0, 1);
            lcd.print(lastAlertStatus);
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

void rs485_send(const char *data)
{
    Serial.print("RS485 Sending: ");
    Serial.println(data);
    uart_write_bytes(RS485_SERIAL_PORT, data, strlen(data));
    uart_wait_tx_done(RS485_SERIAL_PORT, 100 / portTICK_PERIOD_MS);
}

void task_RS485_Receive()
{
    static bool firstRun = true;
    if (firstRun)
    {
        Serial.println("RS485 Receive Task Started");
        firstRun = false;
    }
    int len = uart_read_bytes(RS485_SERIAL_PORT, (uint8_t *)rs485Buffer, sizeof(rs485Buffer) - 1, 20 / portTICK_PERIOD_MS);
    if (len > 0)
    {
        rs485Buffer[len] = '\0';
        Serial.print("RS485 Received (len=");
        Serial.print(len);
        Serial.print("): ");
        // In dữ liệu dạng chuỗi
        Serial.println(rs485Buffer);
        // In dữ liệu dạng hex để debug ký tự không in được
        Serial.print("Raw data (hex): ");
        for (int i = 0; i < len; i++)
        {
            Serial.print((uint8_t)rs485Buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        tb.sendTelemetryData("rs485_data", rs485Buffer);
    }
}

void task_RS485_SerialTest()
{
    static bool firstRun = true;
    if (firstRun)
    {
        Serial.println("RS485 Serial Test Task Started");
        firstRun = false;
    }
    if (Serial.available())
    {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0)
        {
            Serial.print("Sending to RS485: ");
            Serial.println(input);
            rs485_send(input.c_str());
        }
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
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);

    lcd.init();
    lcd.backlight();
    Serial.println("LCD initialized");

    // Cấu hình cảm biến MQ135
MQ135.setRegressionMethod(1); 
MQ135.init();

// Hiệu chỉnh cảm biến
Serial.println("Calibrating MQ135 please wait...");
float calcR0 = 0;
for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
    Serial.print(".");
}
MQ135.setR0(calcR0 / 10);
Serial.println(" done!");

// Kiểm tra lỗi hiệu chỉnh
if (isinf(calcR0)) {
    Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected). Please check your wiring and supply.");
}
if (calcR0 == 0) {
    Serial.println("Warning: Connection issue, R0 is zero (Analog pin with short circuit to ground). Please check your wiring and supply.");
}

const uart_config_t uart_config = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
    .source_clk = UART_SCLK_APB, // Thay UART_SCLK_DEFAULT bằng UART_SCLK_APB
};

uart_param_config(RS485_SERIAL_PORT, &uart_config);
uart_set_pin(RS485_SERIAL_PORT, RS485_TX_PIN, RS485_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(RS485_SERIAL_PORT, 256, 0, 0, NULL, 0);

    SCH_Init();
    SCH_Add_Task(task_InitWiFi, 0, 500);
    SCH_Add_Task(task_ThingsBoardConnect, 0, 1000);
    SCH_Add_Task(task_SendTelemetry, 0, telemetrySendInterval);
    SCH_Add_Task(task_UpdateLCD, 0, 100);
    SCH_Add_Task(task_ProcessTB, 0, 10);
    SCH_Add_Task(task_RS485_Receive, 0, 100); 
SCH_Add_Task(task_RS485_SerialTest, 0, 100);
}

void loop()
{
    SCH_Dispatch_Tasks();
}


