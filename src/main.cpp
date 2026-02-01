#include <Arduino.h>
#include <M5Stack.h>

#include "ESPNowEz.h"

#define LGFX_AUTODETECT
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

#define HEART_BEAT_TIMING 1000 // 1000ms
#define TIMER_RESET_TIMING 60000 // 1min. = 60000ms

#define SEND_TEXT_LEN 5 // S11E
#define SHOW_TEXT_LEN 30 // この長さはM5の画面表示用なので適当でよい

CESPNowEZ espnow(0); // コントローラは必ず0

uint8_t device1MacAddr[] = { 0x98, 0xa3, 0x16, 0x90, 0x16, 0x90 }; // ID1
uint8_t device2MacAddr[] = { 0x98, 0xa3, 0x16, 0x8f, 0x7b, 0x20 }; // ID2
uint8_t device3MacAddr[] = { 0x98, 0xa3, 0x16, 0x8f, 0x7d, 0x70 }; // ID3

uint16_t motorSpeed[] = {5000, 9000, 12000, 15000, 18000, 21000, 24000, 30000};
int8_t motorSpeedIndex = 2;

// このM5のmacアドレス
// 3c:8a:1f:d7:69:bc

static LGFX screen;
static LGFX_Sprite canvas(&screen);

bool drawQueue_;
bool heartBeatQueue_;

uint16_t heartBeat_ = 0;

// タイマー関連、Controllerはハートビート向け
hw_timer_t* timer = nullptr;
uint32_t timerCount = 0;

uint32_t debugCount = 0;

// Controller -> Unity文字列
char sendText[SEND_TEXT_LEN];

// Unity -> Controller文字
char receivedChar;

// Cotroller -> Deviceデータ
ESPNOW_Con2DevData controllerData;

// Device -> Controllerデータ、未使用の予定
ESPNOW_Dev2ConData deviceData[3]; // 3台分

// MACアドレス表示用文字列、3行
char macAddrText[3][18]; // xx:xx:xx:xx:xx:xx + '\0'

// 命令画面表示用文字列、3行
char showText[3][SHOW_TEXT_LEN];

// モータースピードの描画
char motorSpeedText[SHOW_TEXT_LEN];

void SetMacAddrToStr();
void ConvertNum2Hex(char* str, uint8_t value);
void Draw();
void OnDataReceived(const uint8_t* mac_addr, const uint8_t* data, int data_len);
void HeartBeatProcess();
void IRAM_ATTR onTimer();

void setup() {
  // put your setup code here, to run once:
  M5.begin(); // 呼ばないと画面の大きさ等が正しく取得できない
  M5.Power.begin();

  // LCD関連
  screen.init();
  screen.setRotation(1);
  canvas.setColorDepth(8);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.createSprite(screen.width(), screen.height());

  // ESP-Now関連
  espnow.Initialize(OnDataReceived, nullptr);
  espnow.SetDeviceMacAddr(device1MacAddr); // ID1
  espnow.SetDeviceMacAddr(device2MacAddr); // ID2
  espnow.SetDeviceMacAddr(device3MacAddr); // ID3

  // MACアドレス48ビットを代入
  SetMacAddrToStr();

  sprintf(motorSpeedText, "Spd. %d", motorSpeed[motorSpeedIndex]);

  drawQueue_ = true;
  heartBeatQueue_ = false;

  timer = timerBegin(0, getApbFrequency() / 1000000, true); // 1usでカウントアップ
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000, true);  // 1000us=1msごとにonTimer関数が呼ばれる
  timerAlarmEnable(timer);

  Serial.begin(115200);
}

void loop() {
  // put your main code here, to run repeatedly:
  M5.update();

  // モータースピード選択処理
  if(M5.BtnA.wasPressed())
  {
    motorSpeedIndex--;
    if(motorSpeedIndex < 0)
    {
      motorSpeedIndex = 0;
    }
    sprintf(motorSpeedText, "Spd. %d", motorSpeed[motorSpeedIndex]);
    drawQueue_ = true;
  }

  if(M5.BtnB.wasPressed())
  {
    motorSpeedIndex++;
    if(motorSpeedIndex >= sizeof(motorSpeed)/sizeof(motorSpeed[0]))
    {
      motorSpeedIndex = sizeof(motorSpeed)/sizeof(motorSpeed[0]) - 1;
    }
    sprintf(motorSpeedText, "Spd. %d", motorSpeed[motorSpeedIndex]);
    drawQueue_ = true;
  }

  // 受信処理
  if(Serial.available() > 0)
  {
    receivedChar = Serial.read();
    switch(receivedChar)
    {
      case 'q':
        // Motor1 <-
        controllerData.cmd = 1;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(1, &controllerData, sizeof(controllerData)); // id:1に送る
        sprintf(showText[0], "%c, Mot.1 Left", receivedChar);
        drawQueue_ = true;
        break;
      case 'e':
        // Motor1 ->
        controllerData.cmd = 2;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(1, &controllerData, sizeof(controllerData));
        sprintf(showText[0], "%c, Mot.1 Right", receivedChar);
        drawQueue_ = true;
        break;
      case 'w':
        // Motor1 Stop
        controllerData.cmd = 0;
        controllerData.speed = 0; // 無視される
        espnow.Send(1, &controllerData, sizeof(controllerData));
        sprintf(showText[0], "%c, Mot.1 Stop", receivedChar);
        drawQueue_ = true;
        break;
        
      case 'a':
        // Motor2 <-
        controllerData.cmd = 1;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(2, &controllerData, sizeof(controllerData)); // id:2に送る
        sprintf(showText[1], "%c, Mot.2 Left", receivedChar);
        drawQueue_ = true;
        break;
      case 'd':
        // Motor2 ->
        controllerData.cmd = 2;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(2, &controllerData, sizeof(controllerData));
        sprintf(showText[1], "%c, Mot.2 Right", receivedChar);
        drawQueue_ = true;
        break;
      case 's':
        // Motor2 Stop
        controllerData.cmd = 0;
        controllerData.speed = 0;
        espnow.Send(2, &controllerData, sizeof(controllerData));
        sprintf(showText[1], "%c, Mot.2 Stop", receivedChar);
        drawQueue_ = true;
        break;
        
      case 'z':
        // Motor3 <-
        controllerData.cmd = 1;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(3, &controllerData, sizeof(controllerData)); // id:3に送る
        sprintf(showText[2], "%c, Mot.3 Left", receivedChar);
        drawQueue_ = true;
        break;
      case 'c':
        // Motor3 ->
        controllerData.cmd = 2;
        controllerData.speed = motorSpeed[motorSpeedIndex];
        espnow.Send(3, &controllerData, sizeof(controllerData));
        sprintf(showText[2], "%c, Mot.3 Right", receivedChar);
        drawQueue_ = true;
        break;
      case 'x':
        // Motor3 Stop
        controllerData.cmd = 0;
        controllerData.speed = 0;
        espnow.Send(3, &controllerData, sizeof(controllerData));
        sprintf(showText[2], "%c, Mot.3 Stop", receivedChar);
        drawQueue_ = true;
        break;

      default:
        // ありえないはず
        sprintf(showText[0], "%c", receivedChar);
        drawQueue_ = true;
        break;
    }
  }

  if(heartBeatQueue_)
  {
    heartBeatQueue_ = false;
    HeartBeatProcess();
  }

  if(drawQueue_)
  {
    drawQueue_ = false;

    Draw();
    canvas.pushSprite(0, 0);
  }
}

void Draw()
{
  canvas.fillScreen(TFT_NAVY);

  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&Font4); // 幅14高さ26
  canvas.setTextSize(1);

  canvas.setCursor(0, 0, &Font4);
  canvas.print(macAddrText[0]);
  canvas.setCursor(0, 30, &Font4);
  canvas.print(macAddrText[1]);
  canvas.setCursor(0, 60, &Font4);
  canvas.print(macAddrText[2]);

  // モータースピード描画
  canvas.setCursor(0, 100, &Font4);
  canvas.print(motorSpeedText);

  canvas.setCursor(0, 140, &Font4);
  canvas.print(showText[0]);
  canvas.setCursor(0, 170, &Font4);
  canvas.print(showText[1]);
  canvas.setCursor(0, 200, &Font4);
  canvas.print(showText[2]);

  canvas.setCursor(290, 232, &Font0);
  canvas.printf("%5d", heartBeat_);
}

void SetMacAddrToStr()
{
    for(int i = 0; i < 6; i++) // 6はMACアドレスの6つの数字
    {
      ConvertNum2Hex(&macAddrText[0][3 * i], device1MacAddr[i]); // 3 * iは「xx:」の3文字
      macAddrText[0][3 * i + 2] = ':';
      ConvertNum2Hex(&macAddrText[1][3 * i], device2MacAddr[i]);
      macAddrText[1][3 * i + 2] = ':';
      ConvertNum2Hex(&macAddrText[2][3 * i], device3MacAddr[i]);
      macAddrText[2][3 * i + 2] = ':';
    }
    macAddrText[0][17] = '\0'; // 最後の「:」は「\0」で置き換え 
    macAddrText[1][17] = '\0';
    macAddrText[2][17] = '\0';
}

void ConvertNum2Hex(char* str, uint8_t value)
{
  int digit1 = value % 16;
  int digit16 = value / 16;

  // 16^1から格納
  if(digit16 >= 10)
    str[0] = 'A' + (digit16 - 10); 
  else
    str[0] = '0' + digit16;

  if(digit1 >= 10)
    str[1] = 'A' + (digit1 - 10); 
  else
    str[1] = '0' + digit1;
}

// M5Stack Basicは2.x系の書き方で実装する
//void OnDataReceived(const esp_now_recv_info* info, const uint8_t* data, int data_len)
void OnDataReceived(const uint8_t* mac_addr, const uint8_t* data, int data_len)
{
  // 受信時の処理
  // 現在、未使用
}

void HeartBeatProcess()
{
  heartBeat_++;
  drawQueue_ = true;
}

void IRAM_ATTR onTimer()
{
  if(timerCount % HEART_BEAT_TIMING == 0)
  {
    heartBeatQueue_ = true;
  }

  timerCount++;
  if(timerCount == TIMER_RESET_TIMING) // 60000で1分、3600000で1時間
  {
    timerCount = 0;
  }
}
