#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>

#define USE_MUTEX
#define USE_TASK_DELAY
// #define ENABLE_FIREBASE
// #define USE_QUEUE

#define SCREEN_WIDTH        128 // OLED display width, in pixels
#define SCREEN_HEIGHT       64 // OLED display height, in pixels

#define SIZE_INPUT          4
#define LED_BUILD_IN        2
#define SIZE_MESSAGE_QUEUE  10
#define INVERVAL_TIME       1000
#define WDT_TIMEOUT         60
#define DELAY               (10000 / portTICK_PERIOD_MS)


#ifdef ENABLE_FIREBASE
#include <FirebaseESP32.h>
#define API_KEY "API_KEY"
#define DATABASE_URL "DATABASE_URL"
// #define USER_EMAIL "USER_EMAIL"
// #define USER_PASSWORD "USER_PASSWORD"    
    FirebaseData fbdo;
#endif

struct Message {
	char name[SIZE_INPUT][10];
	uint16_t val[SIZE_INPUT] = {0, 0, 0, 0};
};

const static char *nameInput[SIZE_INPUT] = {"Socket1", "Socket2", "Socket3", "Socket4"};
const static uint8_t xPoint = 0, yPoint = 0;
static char str[30];

Message msg;

const char *ssidWifi = "WIFI_ID";
const char *passwordWifi = "WIFI_PASSWORD";

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Declaer wifi object
WiFiClient wifi;

// Declear handle mutex
#ifdef USE_MUTEX
SemaphoreHandle_t xMutex;
#endif

// Declear handle task
static TaskHandle_t xHandleReadDataTask = NULL, xHandleSendDataTask = NULL, xHandleDisplayTask = NULL;

#ifdef USE_QUEUE
static QueueHandle_t xMessageQueue = NULL;
#endif

void setDefaultDisplay();
void dataDisplay(Message *msgBuffer);
void readData();
void displayTask(void *param);
void sendDataTask(void *parameter);
void readDataTask(void *parameter);

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	// Serial.print("Wifi event: Executing on core ");
	// Serial.println(xPortGetCoreID());
	Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
	Serial.println("WiFi connected");
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	Serial.println("Disconnected from WiFi access point");
	Serial.print("WiFi lost connection. Reason: ");
	Serial.println(info.disconnected.reason);
	Serial.println("Trying to Reconnect");
	WiFi.begin(ssidWifi, passwordWifi);
}