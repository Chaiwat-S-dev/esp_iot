#include <Arduino.h>
#include "config.h"

void setup() {
	Serial.begin(115200);
	delay(1000);

	for (uint8_t i = 0; i < SIZE_INPUT; i++) {
		memcpy(msg.name[i], nameInput[i], sizeof(msg.name[i]));
		Serial.printf("check keep input name: %s\n", msg.name[i]);
	}

	WiFi.begin(ssidWifi, passwordWifi);
	Serial.print(F("Waiting connect wifi..."));
	while (WiFi.status() != WL_CONNECTED) {
	   delay(500);
	   Serial.print(F("."));
	}
	Serial.println(F("Wifi Connected"));

	pinMode(LED_BUILD_IN, OUTPUT);

	// // delete old config
	// WiFi.disconnect(true);
	// delay(1000);

	// //prepare function for Event
	// WiFi.onEvent(WiFiStationConnected, SYSTEM_EVENT_STA_CONNECTED);
	// WiFi.onEvent(WiFiGotIP, SYSTEM_EVENT_STA_GOT_IP);
	// WiFi.onEvent(WiFiStationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);

	// Serial.printf("Wifi SSID: %s, PASSWORD: %s\n", ssidWifi, passwordWifi);
	// WiFi.begin(ssidWifi, passwordWifi);  
	// Serial.println("Wait for WiFi... ");
	
#ifdef ENABLE_FIREBASE
	Firebase.begin(DATABASE_URL, API_KEY);
#endif
	/* create Mutex */
#ifdef USE_MUTEX
	xMutex = xSemaphoreCreateMutex();
#endif

#ifdef USE_QUEUE
	xMessageQueue = xQueueCreate(SIZE_MESSAGE_QUEUE, sizeof(Message));
#endif

	xTaskCreatePinnedToCore(
	    displayTask,           /* Task function. */
	    "displayTask",         /* name of task. */
	    4096,                              /* Stack size of task */
	    NULL,                              /* parameter of the task */
	    4,                                 /* priority of the task */
	    &xHandleDisplayTask,
	    0);                                /* Task handle to keep track of created task */
	
	xTaskCreatePinnedToCore(
		readDataTask,					/* Task function. */
		"readDataTask", 				/* name of task. */
		4096,								/* Stack size of task */
		NULL,								/* parameter of the task */
		4,									/* priority of the task */
		&xHandleReadDataTask,
		1);					/* Task handle to keep track of created task */
	
	// delay for waiting display task init
	vTaskDelay(1000 / portTICK_PERIOD_MS);

	xTaskCreatePinnedToCore(
		sendDataTask,   					/* Task function. */
		"sendDataTask", 					/* name of task. */
		16384,			   					/* Stack size of task */
		NULL,			   					/* parameter of the task */
		0,			   						/* priority of the task */
		&xHandleSendDataTask,
		0);			   		/* Task handle to keep track of created task */

#ifndef USE_TASK_DELAY
	disableCore0WDT();
#endif

#ifdef WDT_TIMEOUT
	esp_task_wdt_init(WDT_TIMEOUT, true);
	esp_task_wdt_add(xHandleDisplayTask);
	esp_task_wdt_add(xHandleSendDataTask);
	esp_task_wdt_add(xHandleReadDataTask);
#endif

	vTaskDelete(NULL);
}

void loop() {
}

void displayTask(void *param) {
	Serial.print("Display task: Executing on core ");
	Serial.println(xPortGetCoreID());
	setDefaultDisplay();
	Message msgBuffer;
#ifndef USE_TASK_DELAY
	static uint32_t prev = 0, track = 0;
#endif

	for (;;) {
#ifndef USE_TASK_DELAY
		if (millis()-prev > INVERVAL_TIME) {
    		prev = millis();
#endif
#ifdef WDT_TIMEOUT
			esp_task_wdt_reset();
#endif
			
#ifdef USE_MUTEX
			Serial.println("Display Task gains key");
			while(xSemaphoreTake(xMutex, portMAX_DELAY) != pdTRUE) {
				Serial.println("Display waiting key");
				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
#endif
			// vTaskSuspend(xHandleSendDataTask);
			// Serial.println("pause the other core");
#ifdef USE_QUEUE
			if( xQueuePeek(xMessageQueue, &msgBuffer, ( TickType_t ) 10 ) ) {
				Serial.println("Display data");
#else
			msgBuffer = msg;
#endif
				dataDisplay(&msgBuffer);
#ifdef USE_QUEUE
			}
			else
				Serial.println("Display not recieve queue data");
#endif
			// Serial.println("resume the other core");
		   	// vTaskResume(xHandleSendDataTask);
#ifdef USE_MUTEX
			Serial.println("Display PriorityTask releases key");
			xSemaphoreGive(xMutex);
#endif

#ifdef USE_TASK_DELAY
		// vTaskDelay(DELAY);
		vTaskDelay(2500 / portTICK_PERIOD_MS);
#else
			Serial.printf("Time Usage display task: %d\n",	millis() - prev);
		}
#endif
		
	}
	vTaskDelete(NULL);
}

void sendDataTask(void *parameter) {
	Serial.print("Send data task: Executing on core ");
	Serial.println(xPortGetCoreID());
	unsigned long previousMillis = 0;
	unsigned long interval = 30000;
#ifndef USE_TASK_DELAY
	static uint32_t prev = 0;
#endif
	UBaseType_t uxHighWaterMark;
	Message msgBuffer;
	for (;;) {
#ifndef USE_TASK_DELAY
		if (millis()-prev > INVERVAL_TIME) {
    		prev = millis();
#endif
#ifdef WDT_TIMEOUT
			esp_task_wdt_reset();
#endif
			// Serial.println("Low PriorityTask running");
			digitalWrite(LED_BUILD_IN, !digitalRead(LED_BUILD_IN));
#ifdef USE_MUTEX
			xSemaphoreTake(xMutex, portMAX_DELAY);
			Serial.println("Send data task gains key");
#endif
			unsigned long currentMillis = millis();
			// if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
			if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval)) {
				Serial.print(millis());
				Serial.print("Reconnecting to WiFi...");
				WiFi.disconnect();
#ifdef USE_QUEUE
				xQueueReceive(xMessageQueue, &msgBuffer, (TickType_t)0);
				for (uint8_t i = 0; i < SIZE_INPUT; i++)
					Serial.printf("Send data to server %s : %d\n", msgBuffer.name[i], msgBuffer.val[i]);
#endif
				Serial.println(WiFi.reconnect() ? "Connected" : "Failed");
				previousMillis = currentMillis;
			}
			else {
#ifdef USE_QUEUE
				if(xQueueReceive(xMessageQueue, &msgBuffer, (TickType_t)10) == pdPASS) {
					Serial.println(F("Send data task pop Queue"));
#else
				msgBuffer = msg;
				for (uint8_t i = 0; i < SIZE_INPUT; i++)
					Serial.printf("Send data to server %s : %d\n", msgBuffer.name[i], msgBuffer.val[i]);
#endif
#ifdef ENABLE_FIREBASE
					if(Firebase.ready()){
						FirebaseJson json;
						for (uint8_t i = 0; i < SIZE_INPUT; i++)
							json.set(msgBuffer.name[i], msgBuffer.val[i]);

						Serial.printf("Push json... %s\n", Firebase.pushJSON(fbdo, "/test/esp", json) ? "ok" : fbdo.errorReason().c_str());
					}
#endif
#ifdef USE_QUEUE					
				}
				else
					Serial.println(F("Send data task get empty queue"));
#endif
			}
			// vTaskSuspend(xHandleReadDataTask);
			
			// uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
			// Serial.printf("check local stack size end fn: %d\n", uxHighWaterMark);
			// vTaskResume(xHandleReadDataTask);
#ifdef USE_MUTEX
			Serial.println("Send data task releases key");
			xSemaphoreGive(xMutex);
#endif
			
#ifdef USE_TASK_DELAY
		// vTaskDelay(DELAY);
		vTaskDelay(5000 / portTICK_PERIOD_MS);
#else
			Serial.printf("Time Usage low task: %d\n",	millis() - prev);
		}
#endif

	}
	vTaskDelete(NULL);
}

void readDataTask(void *parameter) {
	Serial.print("Read data task: Executing on core ");
	Serial.println(xPortGetCoreID());

#ifndef USE_TASK_DELAY
	static uint32_t prev = 0;
#endif

	for (;;) {
#ifndef USE_TASK_DELAY
		if (millis()-prev > INVERVAL_TIME) {
    		prev = millis();
#endif
#ifdef WDT_TIMEOUT
			esp_task_wdt_reset();
#endif
			// Serial.println("readDataTask gains key");
#ifdef USE_MUTEX
			// xSemaphoreTake( xMutex, portMAX_DELAY );
#endif
			// Serial.println("readDataTask is running");
			readData();
			// Serial.println("readDataTask releases key");
#ifdef USE_MUTEX
			// xSemaphoreGive( xMutex );
#endif
#ifdef USE_TASK_DELAY
			vTaskDelay(DELAY);
#else
		}
#endif

	}
	vTaskDelete(NULL);
}

void readData() {
	for (uint8_t i = 0; i < SIZE_INPUT; i++) {
		msg.val[i] = random(0, 1023);
		Serial.printf("Check keep data input: %d\n", msg.val[i]);
	}

#ifdef USE_QUEUE
	if (xQueueIsQueueFullFromISR(xMessageQueue)) {
		Serial.println(F("Queue is full"));
		Message msgBuffer;
		xQueueReceive(xMessageQueue, &msgBuffer, (TickType_t) 0);
		Serial.printf("Pop Queue\n");
	}
	if (xQueueSend(xMessageQueue, (void *) &msg, (TickType_t) 0) != pdPASS) {
		Serial.println(F("Queue is full then cannot push queue"));
	}
#endif

}

void setDefaultDisplay() {
	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
		Serial.println(F("SSD1306 allocation failed"));
		for (;;);
	}
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);

	for (uint8_t i = 0; i < SIZE_INPUT; i++) {
		uint8_t curser_y = yPoint + (10 * i);
		display.setCursor(xPoint, curser_y);
		sprintf(str, "Value read [%d] = ", i);
		display.print(str);
		display.display();
		memset(str, 0, strlen(str));
	}
}

void dataDisplay(Message *msgBuffer) {
	for (uint8_t i = 0; i < SIZE_INPUT; i++) {
		uint8_t curser_y = yPoint + (10 * i);
		display.fillRect(100, curser_y, 28, 10, BLACK);
		display.display();
		display.setCursor(100, curser_y);
		sprintf(str, "%d", msgBuffer->val[i]);
		display.print(str);
		display.display();
		memset(str, 0, strlen(str));
	}
}