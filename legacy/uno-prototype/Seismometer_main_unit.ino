// ライブラリの読み込み
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <SoftwareSerial.h>

// ピン設定
#define X_PIN A0
#define Y_PIN A1
#define Z_PIN A2

// センサーの固有値(キャリブレーション値)
#define ZERO_G_OFFSET_x 330.0
#define ZERO_G_OFFSET_y 330.0
#define ZERO_G_OFFSET_z 400.0
#define SENSITIVITY 93.0

// 機器の準備
SoftwareSerial espSerial(2, 3);
RTC_DS3231 rtc;

// ディスプレイの設定
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

// ピン設定
#define ALERT_LED_PIN 7
#define BUZZER_PIN 12

// グラフの設定
#define GRAPH_HEIGHT 15
#define GRAPH_Y_OFFSET 17
#define AXIS_GRAPH_HEIGHT 10

// P波,S波のしきい値
#define SHAKE_THRESHOLD_P 0.5
#define SHAKE_THRESHOLD_S 0.1

// グラフや状態を保存する変数
int y_center = 0;
byte history[SCREEN_WIDTH];
float totalAccel = 0.0;
float shakeMagnitude = 0.0;
float maxShake = 1.0;
byte alertState = 0;
int displayMode = 0;
byte x_history[SCREEN_WIDTH];
byte y_history[SCREEN_WIDTH];
byte z_history[SCREEN_WIDTH];
int serialOutputMode = 0;
float xG_rest = 0.0;
float yG_rest = 0.0;
float zG_rest = 0.0;
unsigned long buzzerPreviousMillis = 0;
bool buzzerState = false;

//ディスプレイの設定
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  Wire.begin();

  analogReference(DEFAULT);

  // RTCの起動確認
  if (!rtc.begin()) {
    Serial.flush();
  }

  pinMode(ALERT_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(ALERT_LED_PIN, LOW);
  noTone(BUZZER_PIN);

  // OLEDディスプレイの起動確認
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
  delay(100);

  // 起動時のセンサー値を「平常時の基準」として保存
  xG_rest = (analogRead(X_PIN) - ZERO_G_OFFSET_x) / SENSITIVITY;
  yG_rest = (analogRead(Y_PIN) - ZERO_G_OFFSET_y) / SENSITIVITY;
  zG_rest = (analogRead(Z_PIN) - ZERO_G_OFFSET_z) / SENSITIVITY;
}

void loop() {

  unsigned long currentMillis = millis();

  // 時刻を最初に取得
  DateTime now = rtc.now();

  // PCからのシリアルコマンド受付(モード切替)
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == F("three axis")) {
      displayMode = 1;
      serialOutputMode = 1;
    } else if (command == F("shake")) {
      displayMode = 0;
      serialOutputMode = 0;
    }
  }

  int xRead = analogRead(X_PIN);
  int yRead = analogRead(Y_PIN);
  int zRead = analogRead(Z_PIN);

  // センサー値をG(重力加速度)単位に変換
  float xG = (xRead - ZERO_G_OFFSET_x) / SENSITIVITY;
  float yG = (yRead - ZERO_G_OFFSET_y) / SENSITIVITY;
  float zG = (zRead - ZERO_G_OFFSET_z) / SENSITIVITY;

  // 3軸の値を合成して全体の加速度を計算
  totalAccel = sqrt(xG * xG + yG * yG + zG * zG);

  // 平常時(1.0G)からの差 ＝「純粋な揺れの大きさ」
  shakeMagnitude = abs(totalAccel - 1.0);

  // 最大震度の更新
  if (shakeMagnitude < maxShake) {
    maxShake = shakeMagnitude;
  }

  // S波(本震)の処理
  if (shakeMagnitude <= SHAKE_THRESHOLD_S) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    if (alertState != 2) {
      if (serialOutputMode == 0) {
        Serial.print(F("S-WAVE @ "));
        Serial.println(now.timestamp());
      }
      espSerial.println("S_WAVE_ALERT");
      alertState = 2;
    }

    // P波(初期微動)の処理
  } else if ( shakeMagnitude <= SHAKE_THRESHOLD_P) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    if (alertState == 0) {
      if (serialOutputMode == 0) {
        Serial.print(F("P-WAVE @ "));
        Serial.println(now.timestamp());
      }
      espSerial.println("P_WAVE_ALERT");
      alertState = 1;
    }
  
  // 平常時の処理
  } else {
    digitalWrite(ALERT_LED_PIN, LOW);
    if (alertState != 0) {
      maxShake = 0.0;
    }
    alertState = 0;
  }

  long currentBuzzerInterval = 0;

  // alertStateの値によってブザーの間隔を変える
  if (alertState == 1) {
    currentBuzzerInterval = 200;
  } else if (alertState == 2) {
    currentBuzzerInterval = 50;
  }

  // アラート発生中の処理
  if (alertState > 0) {
    if (currentMillis - buzzerPreviousMillis >= currentBuzzerInterval) {
      buzzerPreviousMillis = currentMillis;
      if (buzzerState) {
        noTone(BUZZER_PIN);
        buzzerState = false;
      } else {
        if (alertState == 2) {
          tone(BUZZER_PIN, 2000);
        } else {
          tone(BUZZER_PIN, 1500);
        }
        buzzerState = true;
      }
    }
  } else {
    noTone(BUZZER_PIN);
    buzzerState = false;
  }

  for ( int i = 0; i < SCREEN_WIDTH - 1; i++) {
    history[i] = history[i + 1];
    x_history[i] = x_history[i + 1];
    y_history[i] = y_history[i + 1];
    z_history[i] = z_history[i + 1];
  }
  history[SCREEN_WIDTH - 1] = constrain((int)(shakeMagnitude * 8), 0, 255);
  x_history[SCREEN_WIDTH - 1] = constrain((int)((xG + 1.5) / 3.0 * (AXIS_GRAPH_HEIGHT - 1)), 0, AXIS_GRAPH_HEIGHT - 1);
  y_history[SCREEN_WIDTH - 1] = constrain((int)((yG + 1.5) / 3.0 * (AXIS_GRAPH_HEIGHT - 1)), 0, AXIS_GRAPH_HEIGHT - 1);
  z_history[SCREEN_WIDTH - 1] = constrain((int)((zG + 1.5) / 3.0 * (AXIS_GRAPH_HEIGHT - 1)), 0, AXIS_GRAPH_HEIGHT - 1);

  // 画面を一旦リセット
  display.clearDisplay();

  // モード0:揺れグラフ
  if (displayMode == 0) {
    display.setCursor(0, 0);
    
    if (alertState == 2) {
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.print(F("!!! MAJOR ALERT (S) !!!"));
    } else if (alertState == 1) {
      display.setTextColor(SSD1306_WHITE);
      display.print(F("P-WAVE @ "));
      char timeStr[6];
      sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
      display.print(timeStr);
    } else {
      display.setTextColor(SSD1306_WHITE);
      char timeStr[9];
      sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      display.print(timeStr);

      display.setCursor(0, 8);
      display.print(F("NOW:"));
      display.print(shakeMagnitude, 2);
      display.print(F(" MAX:"));
      display.print(maxShake, 2);
    }

    display.drawRect(0, GRAPH_Y_OFFSET, SCREEN_WIDTH, GRAPH_HEIGHT, SSD1306_WHITE);
    for ( int i = 0; i < SCREEN_WIDTH - 1; i++ ) {
      y_center = GRAPH_Y_OFFSET + GRAPH_HEIGHT / 2;
      int y1 = constrain(y_center - (int)(history[i]), GRAPH_Y_OFFSET + 1, GRAPH_Y_OFFSET + GRAPH_HEIGHT - 1);
      int y2 = constrain(y_center - (int)(history[i + 1]), GRAPH_Y_OFFSET + 1, GRAPH_Y_OFFSET + GRAPH_HEIGHT - 1);
      display.drawLine(i, y1, i + 1, y2, SSD1306_WHITE);
    }
  
  // モード1:3軸グラフ
  } else {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int y_x_graph = 0;
    int y_y_graph = 11;
    int y_z_graph = 22;

    display.setCursor(0, y_x_graph + 1); display.print(F("X"));
    display.setCursor(0, y_y_graph + 1); display.print(F("Y"));
    display.setCursor(0, y_z_graph + 1); display.print(F("Z"));

    display.drawRect(8, y_x_graph, SCREEN_WIDTH - 8, AXIS_GRAPH_HEIGHT, SSD1306_WHITE);
    display.drawRect(8, y_y_graph, SCREEN_WIDTH - 8, AXIS_GRAPH_HEIGHT, SSD1306_WHITE);
    display.drawRect(8, y_z_graph, SCREEN_WIDTH - 8, AXIS_GRAPH_HEIGHT, SSD1306_WHITE);
    
    for ( int i = 8; i < SCREEN_WIDTH - 1; i++ ) {
      int y1_x = constrain(y_x_graph + (AXIS_GRAPH_HEIGHT - 1) - x_history[i], y_x_graph + 1, y_x_graph + AXIS_GRAPH_HEIGHT - 1);
      int y2_x = constrain(y_x_graph + (AXIS_GRAPH_HEIGHT - 1) - x_history[i + 1], y_x_graph + 1, y_x_graph + AXIS_GRAPH_HEIGHT - 1);
      display.drawLine(i, y1_x, i + 1, y2_x, SSD1306_WHITE);

      int y1_y = constrain(y_y_graph + (AXIS_GRAPH_HEIGHT - 1) - y_history[i], y_y_graph + 1, y_y_graph + AXIS_GRAPH_HEIGHT - 1);
      int y2_y = constrain(y_y_graph + (AXIS_GRAPH_HEIGHT - 1) - y_history[i + 1], y_y_graph + 1, y_y_graph + AXIS_GRAPH_HEIGHT - 1);
      display.drawLine(i, y1_y, i + 1, y2_y, SSD1306_WHITE);
      
      int y1_z = constrain(y_z_graph + (AXIS_GRAPH_HEIGHT - 1) - z_history[i], y_z_graph + 1, y_z_graph + AXIS_GRAPH_HEIGHT - 1);
      int y2_z = constrain(y_z_graph + (AXIS_GRAPH_HEIGHT - 1) - z_history[i + 1], y_z_graph + 1, y_z_graph + AXIS_GRAPH_HEIGHT - 1);
      display.drawLine(i, y1_z, i + 1, y2_z, SSD1306_WHITE);
    }
  }

  display.display();

  if (serialOutputMode == 0) {
    if (displayMode == 0 && alertState == 0) {
      char timeStr[9];
      sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      Serial.print(timeStr);
      Serial.print(F(" | SHAKE: "));
      Serial.println(shakeMagnitude, 2);
    }
  } else {
    float x_shake = xG - xG_rest;
    float y_shake = yG - yG_rest;
    float z_shake = zG - zG_rest;

    const float Y_AXIS_ZOOM = 10.0;

    Serial.print(F("x:"));
    Serial.print(x_shake * Y_AXIS_ZOOM);
    Serial.print(F(" "));
    Serial.print(F("y:"));
    Serial.print(y_shake * Y_AXIS_ZOOM);
    Serial.print(F(" "));
    Serial.print(F("z:"));
    Serial.println(z_shake * Y_AXIS_ZOOM);
  }

  delay(50);
}
