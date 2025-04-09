
#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12

#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>
#include <time.h>
#include "app_scheduler.h"

constexpr char WIFI_SSID[] = "ACLAB";
constexpr char WIFI_PASSWORD[] = "ACLAB2023";
// constexpr char WIFI_SSID[] = "iPhone";
// constexpr char WIFI_PASSWORD[] = "777888111000";
constexpr char TOKEN[] = "qxu9tl8c2pv2pmbn781w";
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

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange = 0;
constexpr int16_t telemetrySendInterval = 1000U;

constexpr std::array<const char *, 3U> SHARED_ATTRIBUTES_LIST = {
    LED_STATE_ATTR,
    BLINKING_INTERVAL_ATTR,
    LED_CONTROL_ATTR
};

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);
DHT20 dht20;

// Hàm lấy thời gian hiện tại
String getCurrentTime() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

// Hàm cập nhật trạng thái LED
void updateLedState(bool newState) {
    ledState = newState;
    digitalWrite(LED_PIN, ledState);
    Serial.print("LED state updated to: ");
    Serial.println(ledState);
    tb.sendAttributeData(LED_STATE_ATTR, ledState);
    tb.sendAttributeData(LED_CONTROL_ATTR, ledState ? "ON" : "OFF");
    attributesChanged = true;
}

// RPC callback
RPC_Response setLedSwitchState(const RPC_Data &data) {
    Serial.println("Received Switch state via RPC");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    updateLedState(newState);
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
    RPC_Callback{"setLedSwitchValue", setLedSwitchState}
};

// Shared attributes callback
void processSharedAttributes(const Shared_Attribute_Data &data) {
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0) {
            const uint16_t new_interval = it->value().as<uint16_t>();
            if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX) {
                blinkingInterval = new_interval;
                Serial.print("Blinking interval is set to: ");
                Serial.println(new_interval);
            }
        } else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0) {
            bool newState = it->value().as<bool>();
            updateLedState(newState);
        } else if (strcmp(it->key().c_str(), LED_CONTROL_ATTR) == 0) {
            String ledControl = it->value().as<String>();
            Serial.print("LED control received: ");
            Serial.println(ledControl);
            if (ledControl == "ON") {
                updateLedState(true);
            } else if (ledControl == "OFF") {
                updateLedState(false);
            } else {
                Serial.println("Unknown LED control value");
            }
        }
    }
    attributesChanged = true;
}

const Shared_Attribute_Callback attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback attribute_shared_request_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());

// Các task cho scheduler
void task_InitWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Connecting to AP ...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("Connected to AP");
    }
}

void task_ThingsBoardConnect() {
    if (!tb.connected()) {
        Serial.print("Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
            Serial.println("Failed to connect");
            return;
        }
        tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
        Serial.println("Subscribing for RPC...");
        if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
            Serial.println("Failed to subscribe for RPC");
            return;
        }
        if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
            Serial.println("Failed to subscribe for shared attribute updates");
            return;
        }
        Serial.println("Subscribe done");
        if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
            Serial.println("Failed to request for shared attributes");
            return;
        }
    }
}

void task_SendTelemetry() {
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT20 sensor!");
    } else {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.print(" °C, Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
        tb.sendTelemetryData("temperature", temperature);
        tb.sendTelemetryData("humidity", humidity);
    }
    String currentTime = getCurrentTime();
    tb.sendAttributeData("currentTime", currentTime.c_str());
    Serial.print("Current time sent: ");
    Serial.println(currentTime);
    tb.sendAttributeData("rssi", WiFi.RSSI());
    tb.sendAttributeData("channel", WiFi.channel());
    tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid", WiFi.SSID().c_str());
}

void task_BlinkLED() {
    if (millis() - previousStateChange >= blinkingInterval) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
        tb.sendAttributeData(LED_STATE_ATTR, ledState);
        previousStateChange = millis();
    }
}

void task_ProcessTB() {
    tb.loop();
    if (attributesChanged) {
        attributesChanged = false;
    }
}

void setup() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Wire.begin(SDA_PIN, SCL_PIN);
    dht20.begin();

    configTime(7 * 3600, 0, NTP_SERVER, "time.nist.gov"); // GMT+7
    Serial.println("Waiting for NTP time sync...");
    while (time(nullptr) < 100000) {
        delay(100);
        Serial.print(".");
    }
    Serial.println("\nTime synced");

    SCH_Init();
    SCH_Add_Task(task_InitWiFi, 0, 500);             
    SCH_Add_Task(task_ThingsBoardConnect, 0, 1000);  
    SCH_Add_Task(task_SendTelemetry, 0, telemetrySendInterval); 
    //SCH_Add_Task(task_BlinkLED, 0, 100);              
    SCH_Add_Task(task_ProcessTB, 0, 10);              
}

void loop() {
    SCH_Dispatch_Tasks();
}
