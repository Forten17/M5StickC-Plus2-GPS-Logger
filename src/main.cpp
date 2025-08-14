#include <Arduino.h>
#include <M5Unified.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <config.h>
#include <WebServer.h>
#include <Preferences.h>

HardwareSerial GPSRaw(2);
TinyGPSPlus gps;
WiFiClient wificlient;
PubSubClient mqttclient(wificlient);
M5Canvas canvas(&M5.Lcd);
WebServer server(80);
Preferences preferences;

int cnt=0; //GPS受信秒数の計測用変数

String ssid;
String password;

//WiFi接続のタイムアウト管理用関数
bool connecting = false;
unsigned long connectStart = 0;
const unsigned long CONNECT_TIMEOUT = 10000;

//GPSデータ更新間隔の管理用関数
unsigned long lastUpdateTime = 0;
const long interval = 5000;

//MQTT接続のタイムアウト管理用関数
unsigned long lastMqttReconnectAttempt = 0;
const long mqttReconnectInterval = 5000;

// --- HTML群 ---
// ホームページ
const char html[] PROGMEM =R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>WiFi接続設定</title>
  <style>
    body {
      font-family: sans-serif;
      padding: 20px;
    }
    label {
      display: block;
      margin-top: 1em;
    }
    input {
      width: 100%;
      padding: 8px;
      font-size: 1em;
      box-sizing: border-box;
    }
    button {
      margin-top: 1.5em;
      width: 100%;
      padding: 10px;
      font-size: 1.1em;
      border: none;
      cursor: pointer;
    }
    button:disabled {
      cursor: default;
    }
  </style>
</head>
<body>
  <h1>WiFi接続設定</h1>
  <form id="wifiForm">
    <label for="ssid">SSID</label>
    <input type="text" id="ssid" name="ssid" placeholder="SSID を入力" autocomplete="off">

    <label for="password">パスワード</label>
    <input type="password" id="password" name="password" placeholder="パスワードを入力" autocomplete="off">

    <button type="submit" id="submitBtn" disabled>送信</button>
  </form>

  <script>
    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('password');
    const submitBtn = document.getElementById('submitBtn');
    const form = document.getElementById('wifiForm');

    // placeholderのクリア･復元、および送信ボタン有効化
    [ssidInput, passInput].forEach(input => {
      const ph = input.placeholder;
      input.addEventListener('focus', () => input.placeholder = '');
      input.addEventListener('blur', () => {
        if (!input.value) input.placeholder = ph;
      });
      input.addEventListener('input', () => {
        const ok = ssidInput.value.trim() && passInput.value.trim();
        submitBtn.disabled = !ok;
        submitBtn.style.backgroundColor = ok ? '#ff7f50' : '#808080';
        submitBtn.style.color = ok ? '#ffffff' : '#000000';
      });
    });

    document.getElementById('wifiForm')
      .addEventListener('submit', e => {
        e.preventDefault();
        const ssid = encodeURIComponent(ssidInput.value);
        const pwd  = encodeURIComponent(passInput.value);

        fetch(`/submit?ssid=${ssid}&password=${pwd}` , {
          redirect: 'follow'
          })
          .then(resp => {
          if (!resp.ok) {
           throw new Error(`HTTP error! status: ${resp.status}`);
           }
           window.location.href = `/submit?ssid=${ssid}&password=${pwd}`;
          })
          .catch(error => {
          document.body.innerHTML = 
          '<h1>送信エラー</h1><p> 入力内容を確認してください </p>';
          console.error(error);
          });
          });
  </script>
</body>
</html>
)rawliteral";

//接続中のページ
const char connect_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>接続中……</title>
  </head>
<body>
  <h1>接続情報を送信しました</h1>
  <p>デバイスをWiFiへ接続しています...</p>
  <script>
  const startTs = Date.now();
    function checkStatus() {
    fetch(`/status?ts=${Date.now()}`)
    .then(res => res.json())
    .then(data => {
      if (data.connected) {
        location.href = '/done';
      } else if (data.done) {
        location.href = '/fail';
      } else {
        setTimeout(checkStatus, 1000);
      }
    })
    .catch(() => location.href = '/fail');
    }
    checkStatus();
    </script>
</body>
</html>
)rawliteral";

// 接続完了ページ
const char done_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>接続完了</title>
  <style>
    body { font-family: sans-serif; padding: 20px; text-align: center; }
    h1 { color: #4CAF50; }
  </style>
</head>
<body>
  <h1>WiFiへの接続完了</h1>
  <p>デバイスは次のネットワークに接続されました：</p>
  <p>SSID :<strong>%SSID%</strong></p>
  <p>IPアドレス :<strong>%IP%</strong></p>
</body>
</html>
)rawliteral";

//接続失敗ページ
const char fail_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>接続失敗</title>
  <style>
    body { font-family: sans-serif; padding: 20px; text-align: center; }
    h1 { color: #f44339; }
  </style>
</head>
<body>
  <h1>接続失敗</h1>
  <p>接続先の情報を確認してください。</p>
</body>
</html>
)rawliteral";

//APモード立ち上げ処理
void startAPMode() {
  Serial.println("[WiFi] Starting AP mode...");
  M5.Lcd.clear(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("WiFi Setup Mode\n");
  M5.Lcd.printf("Acccess to\n%s\n", AP_SSID);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(100);

  String ip_address = "http://" + WiFi.softAPIP().toString();
  M5.Lcd.print(ip_address);
  Serial.printf("[WiFi] AP Started. IP: %s\n", ip_address.c_str());
  server.begin();
}

//PreferencesからのWiFi情報読み出しと接続処理
bool connectToSavedWifi() {
  preferences.begin("wifi-creds", false);

  // PreferencesからSSIDとパスワードを読み出す
  String saved_ssid = preferences.getString("ssid", "");
  String saved_pass = preferences.getString("password", "");

  preferences.end();

  if (saved_ssid.length() > 0) {
    Serial.printf("[WiFi] Found saved credentials. SSID: %s\n", saved_ssid.c_str());
    Serial.println("[WiFi] Attempting to connect to saved WiFi...");

    M5.Lcd.clear(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("Connecting to\n%s", saved_ssid.c_str());
    
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - startTime > 10000) {
        Serial.println("\n[WiFi] Connection failed (timeout).");
        M5.Lcd.println("\nFailed!");
        WiFi.disconnect();
        return false;
      }
      M5.Lcd.print(".");
      Serial.print(".");
      delay(500);
    }

    String ip_address = WiFi.localIP().toString();
    Serial.printf("\n[WiFi] Connected! IP: %s\n", ip_address.c_str());
    M5.Lcd.printf("\nConnected!\nIP: %s", ip_address.c_str());
    ssid = saved_ssid; // 接続成功したSSIDをグローバル変数に保持
    return true;
  }
  
  Serial.println("[WiFi] No saved credentials found.");
  return false;
}

// ---ハンドラ関数群 ---
// ホームページのハンドラ
void handleRoot() {
  server.send_P(200, "text/html", html);
}

// submitページのハンドラ
void handleSubmit() {
  ssid = server.arg("ssid");
  password = server.arg("password");

  server.send_P(200, "text/html", connect_html);

  //ディスプレイに接続先情報を表示
  M5.Lcd.clear(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("Connect to\n%s\n", ssid.c_str());

  WiFi.begin(ssid.c_str(), password.c_str());
  connectStart = millis();
  connecting = true;
}

//接続中のページハンドラ
void handleStatus() {
  bool done = false, ok = false;
  if (connecting) {
    //接続成功判定
    if (WiFi.status() == WL_CONNECTED) {
      done = true; ok = true;
      //タイムアウト判定
    } else if (millis() - connectStart >= CONNECT_TIMEOUT) {
      done = true; ok = false;
    }
    else {
      M5.Lcd.print(".");
    }
    //接続完了判定
    if (done) connecting = false;
  }
  String json = String("{\"connected\":") + (ok ? "true" : "false")
              + String(",\"done\":") + (done ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

//接続完了ページのハンドラ
void handleDone() {
  String page = FPSTR(done_html);
  page.replace("%SSID%", ssid);
  page.replace("%IP%", WiFi.localIP().toString());
  server.send(200, "text/html", page);

  M5.Lcd.clear(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("Connected to\n%s", ssid.c_str());

  Serial.println("[Preferences] Saving WiFi credentials...");
  M5.Lcd.println("\nSaving WiFi info...");

  // Preferencesに接続情報を保存
  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();

  Serial.println("[Preferences] Credentials saved.");
  M5.Lcd.println("Saved!");
  delay(1000); // メッセージ表示のための遅延

}

//接続失敗ページのハンドラ
void handleFail() {
  server.send(200, "text/html", fail_html);

  M5.Lcd.clear(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print("\nConnection failed!");
  M5.Lcd.print("\nCheck your password\nTRY AGAIN");
}

// 未定義ページのハンドラ
void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

//MQTTクライアントの再接続関数
void reconnect(){
  Serial.println("[MQTT] Connecting to MQTT...");
  M5.Lcd.println("\nConnecting to MQTT...");

  //接続処理
  if (mqttclient.connect(mqtt_deviceId, mqtt_user, mqtt_password)) {
    Serial.println("[MQTT] Connected!");
    Serial.printf("[MQTT] Conntected to %s\n", mqtt_server);
    M5.Lcd.println("MQTT Connected!");
  } else {
    Serial.printf("[MQTT] Connection failed, rc=%d\n", mqttclient.state());
    M5.Lcd.printf("failed, rc=%d.\nTry again in 5s", mqttclient.state());
  }
}

//GPSデータの取得関数
char* gpsDateTime(TinyGPSPlus *gps) {
  static char p_datetime[50];

  snprintf(p_datetime, 50, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", gps->date.year(), gps->date.month(), gps->date.day(), gps->time.hour(), gps->time.minute(), gps->time.second());
    M5.Lcd.printf("%s\n", p_datetime);
    return p_datetime;
}

void setup() {
  //本体初期化
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n[System] Starting device...");
  
  //ディスプレイ初期設定
  M5.Lcd.setRotation(1);
  canvas.createSprite(M5.Lcd.width(), M5.Lcd.height());

  //エンドポイント設定
  server.on("/", HTTP_GET, handleRoot);         //ホームページへのアクセス時の処理
  server.on("/submit", HTTP_GET, handleSubmit); //フォーム送信時の処理
  server.on("/status", HTTP_GET, handleStatus); //接続確認の処理
  server.on("/done", HTTP_GET, handleDone);     //接続完了時の処理
  server.on("/fail", HTTP_GET, handleFail);     //接続失敗時の処理
  server.onNotFound(handleNotFound);            //未定義のページへのアクセス時の処理

  if (!connectToSavedWifi()) {
    //接続失敗時、APモードを起動する
    startAPMode();
  } 

  //GPS初期設定
  GPSRaw.begin(115200, SERIAL_8N1, 33, 32);

  //MQTT初期設定
  mqttclient.setServer(mqtt_server, 1883);
 }

void loop() {
  //Webサーバークライアントの実行
  server.handleClient();

  //WiFi接続後にMQTTへの接続処理を開始
  if (WiFi.status() == WL_CONNECTED) {
    
    //MQTTとの通信失敗時の再接続処理
    if (!mqttclient.connected()) {
      unsigned long now = millis();
    
      if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
        lastMqttReconnectAttempt = now;
        reconnect();
      }
    } else {
      mqttclient.loop();
    }

  //GPSデータの取得とMQTTへの送信処理
  if (mqttclient.connected()) {
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime >= interval) {
        lastUpdateTime = currentTime;
        
        char payload[200];
        char topic[64];
        const char* p_datetime = NULL;
        
        //GPSデータ取得時のディスプレイ更新
        canvas.setTextSize(2);
        canvas.setCursor(0, 2);
        canvas.printf("### GPS TEST %d\n", cnt++);
        Serial.println("[GPS] GPS Signal Receiving...");

        while (GPSRaw.available() > 0) {
          if(gps.encode(GPSRaw.read())) {
            break;
          }
        }
        
        if (gps.location.isValid()) {
          Serial.println("GPS Signal Received!");
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
      }
    }
  }
}