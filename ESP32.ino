/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/smart-home-ebook/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include <Arduino.h>

#include <WiFi.h>
extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/timers.h"
}
#include <AsyncMqttClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define WIFI_SSID "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD "REPLACE_WITH_YOUR_PASSWORD"

// BROKER
#define MQTT_HOST IPAddress(XXX, XXX, X, XXX) //MQTT BROKER IP ADDRESS
/*for example:
#define MQTT_HOST IPAddress(192, 168, 1, 106)*/
#define MQTT_PORT 1883
#define BROKER_USER "REPLACE_WITH_BROKER_USERNAME"
#define BROKER_PASS "REPLACE_WITH_BROKER_PASSWORD"

//MQTT Publica Topicuri
#define MQTT_PUB_TEMP "esp/bme280/temperature"
#define MQTT_PUB_HUM "esp/bme280/humidity"
#define MQTT_PUB_PRES "esp/bme280/pressure"
#define MQTT_PUB_MOTION "esp/motion/notification"

//MQTT Subscribe Topics
#define MQTT_SUB_DIGITAL "esp/digital/#"

// BME280 I2C
Adafruit_BME280 bme;

// Variabile pentru citirile de la senzori
float temp;
float umid;
float pres;

// Variabile pentru miscare
const int motionSensor = 26; // PIR Motion Sensor
bool motionDetected = false;

// Indica cand se detecteaza miscara
void detectsMovement() {
  Serial.println("Miscare detectata!!!");
  motionDetected = true;
}

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

unsigned long previousMillis = 0;   // reține ultimul timp în milisecunde când a fost publicat un mesaj
const long interval = 10000;        // intervalul in milisecunde în care se publică valori 

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  //Abonare la topics
  // Abonare la topic MQTT_SUB_DIGITAL cand se conecteaza la broker
  uint16_t packetIdSub1 = mqttClient.subscribe(MQTT_SUB_DIGITAL, 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub1);
  
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  // Do whatever you want when you receive a message
  
  // Salveaza mesajul intr-o variabila
  String receivedMessage;
  for (int i = 0; i < len; i++) {
    Serial.println((char)payload[i]);
    receivedMessage += (char)payload[i];
  }
  // Salveaza topicul intr-o variabila String
  String receivedTopic = String(topic);  
  Serial.print("Received Topic: ");
  Serial.println(receivedTopic);

  // Verifica care GPIO se doreste sa se controleze
  int stringLen = receivedTopic.length();
  // Get the index of the last slash
  int lastSlash = receivedTopic.lastIndexOf("/");
  // Get the GPIO number (it's after the last slash "/")
  // esp/digital/GPIO
  String gpio = receivedTopic.substring(lastSlash+1, stringLen);
  Serial.print("DIGITAL GPIO: ");
  Serial.println(gpio);
  Serial.print("STATE: ");
  Serial.println(receivedMessage);

  // Check if it is DIGITAL
  if (receivedTopic.indexOf("digital") > 0) {
    //Set the specified GPIO as output
    pinMode(gpio.toInt(), OUTPUT);
    //Control the GPIO
    if (receivedMessage == "true"){
      digitalWrite(gpio.toInt(), HIGH);
    }
    else{
      digitalWrite(gpio.toInt(), LOW);
    }
  }  
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // PIR Motion Sensor mode INPUT_PULLUP
  pinMode(motionSensor, INPUT_PULLUP);
  // Set motionSensor pin as interrupt, assign interrupt function and set RISING mode
  attachInterrupt(digitalPinToInterrupt(motionSensor), detectsMovement, RISING);

  // Initializare senzor BME280 
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(BROKER_USER, BROKER_PASS);


  connectToWifi();
}

void loop() {
   if(motionDetected){ //daca se detectează mișcare
    // Publică un mesaj MQTT în topicul esp/motion
    uint16_t packetIdPub4 = mqttClient.publish(MQTT_PUB_MOTION, 1, true, "true");                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", MQTT_PUB_MOTION, packetIdPub4);
    Serial.println("Motion Detected");
    motionDetected = false;
  }

  unsigned long currentMillis = millis();
  // Publică un mesaj in topicuri la fiecare 10 secunde 
  if (currentMillis - previousMillis >= interval) {
    // Publică un mesaj in topicuri la fiecare 10 secunde
    previousMillis = currentMillis;

    // Citire noua al senszorului BME280
    temp = bme.readTemperature();
    hum = bme.readHumidity();
    pres = bme.readPressure()/100.0F;

    // Publica un mesaj MQTT in topic esp32/BME2800/temperature
    uint16_t packetIdPub1 = mqttClient.publish(MQTT_PUB_TEMP, 1, true, String(temp).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", MQTT_PUB_TEMP, packetIdPub1);
    Serial.printf("Message: %.2f \n", temp);

    // Publica un mesaj MQTT in topic esp32/BME2800/humidity
    uint16_t packetIdPub2 = mqttClient.publish(MQTT_PUB_HUM, 1, true, String(hum).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId %i: ", MQTT_PUB_HUM, packetIdPub2);
    Serial.printf("Message: %.2f \n", hum);

    // Publica un mesaj MQTT in topic esp32/BME2800/pressure
    uint16_t packetIdPub3 = mqttClient.publish(MQTT_PUB_PRES, 1, true, String(pres).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", MQTT_PUB_PRES, packetIdPub3);
    Serial.printf("Message: %.3f \n", pres);
  }
}
