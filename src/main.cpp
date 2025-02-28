#include <Arduino.h>
#include <M5Unified.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <config.h>
#include <WebServer.h>

HardwareSerial GPSRaw(2);
TinyGPSPlus gps;
WiFiClient wificlient;
PubSubClient mqttclient(wificlient);
M5Canvas canvas(&M5.Lcd);

int cnt=0;

void setup() {
  //本体初期化
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  
  //ディスプレイ初期設定
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0,0);

  canvas.setTextWrap(false);
  canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());

  //WiFi初期設定
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  M5.Lcd.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0,0);
  M5.Lcd.println("\nWiFi connected");
  M5.Lcd.print(WiFi.localIP());

  //GPS初期設定
  GPSRaw.begin(9600, SERIAL_8N1, 33, 32);

  //MQTT初期設定
  mqttclient.setServer(mqtt_server, 1883);

}

void recconect(){
  M5.Lcd.println("\nConnecting to MQTT");
  int retryCount = 0;
  const int maxRetries = 5;

  while (!mqttclient.connected() && retryCount < maxRetries) {
    if (mqttclient.connect(mqtt_deviceId, mqtt_user, mqtt_password)) {
      M5.Lcd.print("Connected to MQTT");
    }else{
      M5.Lcd.printf("failed Connection, \nrc=", mqttclient.state());
      M5.Lcd.printf("try again in 5 seconds (%d/%d)\n)", retryCount + 1, maxRetries);
      retryCount++;
      delay(5000);
    }
  }
  if (retryCount == maxRetries) {
    M5.Lcd.println("MQTT connection failed.\nCheck settings.");
  }
}


char* gpsDateTime(TinyGPSPlus *gps) {
  static char p_datetime[50];

  snprintf(p_datetime, 50, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", gps->date.year(), gps->date.month(), gps->date.day(), gps->time.hour(), gps->time.minute(), gps->time.second());
    M5.Lcd.printf("%s\n", p_datetime);
    return p_datetime;
}

void loop() {
  if (!mqttclient.connected()) {
    recconect();
  }
  mqttclient.loop();

  char payload[100];
  char topic[50];
  char* p_datetime = NULL;

  //GPSデータ取得
  canvas.setTextSize(2);
  canvas.setCursor(0, 2);
  canvas.printf("### GPS TEST %d\n", cnt++);
  while (GPSRaw.available() > 0) {
    if(gps.encode(GPSRaw.read())) {
      break;
    }
  }

  if (gps.location.isValid()) {
    //取得したGPSデータをメモリ描画領域に表示
    canvas.printf("LAT: %.6f\n", gps.location.lat());
    canvas.printf("LNG: %.6f\n", gps.location.lng());
    canvas.printf("ALT: %.2f \n", gps.altitude.meters());

    //GPSデータをメモリに格納
    p_datetime = gpsDateTime(&gps);
    snprintf(payload, sizeof(payload), "{\"lat\":\"%.6f\",\"lng\":\"%.6f\",\"alt\":\"%.2f\",\"gpstime\":\"%s\"}", 
    gps.location.lat(), gps.location.lng(), gps.altitude.meters(), p_datetime);

    //MQTTトピックをメモリに格納
    snprintf(topic, sizeof(topic), "/%s/%s", mqtt_deviceId, mqtt_topic);

    //MQTT送信
    mqttclient.publish(topic, payload);

  } else {
    canvas.print("GPS Signal Lost");
  }

  canvas.pushSprite(&M5.Lcd, 0, 46);

  delay(1000);
}
