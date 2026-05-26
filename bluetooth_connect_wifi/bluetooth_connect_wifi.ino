#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <PubSubClient.h> // NEW

Preferences preferences;

// --- MQTT 设置 ---
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_state = "myhome/esp32/light/state";
const char* mqtt_topic_cmd = "myhome/esp32/light/cmd";

const int LED_PIN = 2; // ESP32板载LED
const int BUTTON_PIN = 0; // ESP32板载BOOT按钮

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool lightState = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
// -----------------

// 定义蓝牙的 Service 和 Characteristic UUID (你可以随便用工具生成新的)
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

bool shouldRestart = false; // 收到密码后重启的标志位
bool g_wifiConnected = false;

// 蓝牙接收数据的回调函数
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        Serial.println("收到蓝牙数据: " + rxValue);
        
        // 查找分隔符 '|'
        int separatorIndex = rxValue.indexOf('|');
        if (separatorIndex > 0) {
          String ssid = rxValue.substring(0, separatorIndex);
          String password = rxValue.substring(separatorIndex + 1);
          
          Serial.println("解析成功 -> SSID: " + ssid + ", 密码: " + password);
          
          // 存入 NVS 闪存
          preferences.begin("wifi_config", false);
          preferences.putString("ssid", ssid);
          preferences.putString("password", password);
          preferences.end();
          
          Serial.println("已保存，准备重启...");
          shouldRestart = true; // 告诉主循环准备重启
        }
      }
    }
};

void setupBLE() {
  BLEDevice::init("ESP32-配网"); // 蓝牙名称
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("蓝牙已开启，等待连接...");
}

void connectWifi()
{
 // 初始化 NVS 并读取 WiFi 信息
  preferences.begin("wifi_config", true); // true 表示只读
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();

  if (savedSSID != "") {
    Serial.println("尝试连接已保存的 WiFi: " + savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    // 尝试连接 10 秒
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi 连接成功! IP 地址: ");
      Serial.println(WiFi.localIP());
      g_wifiConnected =  true; // 成功联网，退出 setup，正常进入 loop
      return;
    } else {
      Serial.println("\n连接失败，准备开启蓝牙配网...");
    }
  } else {
    Serial.println("没有找到保存的 WiFi 信息，准备开启蓝牙配网...");
  }
  g_wifiConnected =  false;
}

// --- MQTT 回调与重连 ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String messageTemp;
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    messageTemp += (char)payload[i];
  }
  Serial.println();

  if (String(topic) == mqtt_topic_cmd) {
    if (messageTemp == "ON") {
      lightState = true;
      digitalWrite(LED_PIN, HIGH);
      mqttClient.publish(mqtt_topic_state, "ON");
    }
    else if (messageTemp == "OFF") {
      lightState = false;
      digitalWrite(LED_PIN, LOW);
      mqttClient.publish(mqtt_topic_state, "OFF");
    }
  }
}

void mqttReconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.publish(mqtt_topic_state, lightState ? "ON" : "OFF");
      mqttClient.subscribe(mqtt_topic_cmd);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
// -----------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW); // 默认关灯

  // 检查是否在启动时按住了 BOOT 按键
  // 如果按住了，则清空 NVS 中保存的 WiFi 账号密码
  // if (digitalRead(BUTTON_PIN) == LOW) {
  //   Serial.println("检测到 BOOT 按键被长按，正在清除已保存的 WiFi 配置...");
  //   preferences.begin("wifi_config", false);
  //   preferences.clear(); // 清除这个命名空间下的所有内容
  //   preferences.end();
  //   Serial.println("清除成功！现在进入蓝牙配网模式...");
    
  //   // 为了给用户一点视觉反馈，让LED闪烁几下
  //   for(int i=0; i<3; i++){
  //     digitalWrite(LED_PIN, HIGH); delay(200);
  //     digitalWrite(LED_PIN, LOW); delay(200);
  //   }
  // }

  connectWifi();
  if(!g_wifiConnected) { // 如果没存过密码，或者连不上，就开启蓝牙
    setupBLE();
  } else {
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
  }
}

void loop() {
  // 如果收到新密码，延迟一小会儿重启，让蓝牙有时间回复ACK
  if (shouldRestart) {
    delay(1000); 
    ESP.restart(); 
  }
  if(!g_wifiConnected)
  {
    delay(1000);
    return;
  }

  // --- MQTT 业务代码 ---
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  // 处理物理按钮 (带消抖)
  int reading = digitalRead(BUTTON_PIN);
  if (reading == LOW && lastButtonState == HIGH) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      lightState = !lightState;
      digitalWrite(LED_PIN, lightState ? HIGH : LOW);
      Serial.println("Physical button pressed! Publishing new state.");
      mqttClient.publish(mqtt_topic_state, lightState ? "ON" : "OFF");
      lastDebounceTime = millis();
    }
  }
  lastButtonState = reading;
  // ---------------------

}