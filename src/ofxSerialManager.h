#pragma once

// --------------------------------------------------
// 環境によって分岐
// Arduino IDE/PlatformIO等: ARDUINO が定義されている前提
// openFrameworks: TARGET_OF を手動で定義してもいいし、
//                 プロジェクト設定で-DTARGET_OFみたいにしてもよい
// --------------------------------------------------

#ifdef ARDUINO
  #include <Arduino.h>
#else
  // openFrameworksの場合は ofMain.h の中に ofSerial が含まれる
  // もし含まれていないなら適宜 include
  #include "ofMain.h"
#endif

// コールバックの型定義
// payloadはバイナリもあり得るので char* と長さをわたす
typedef void (*SerialCallback)(const char* payload, int length);

class ofxSerialManager {
public:
  // コンストラクタ(複数インスタンス可)
  ofxSerialManager();

  // リスナー登録
  bool addListener(const char* cmd, SerialCallback callback);
  
  // Arduino版とOF版で口を変える
#ifdef ARDUINO
  // Arduinoの場合は setup( Stream* ) でシリアルを受けとる
  void setup(Stream* s);
#else
  // openFrameworksの場合は ofSerial のポインタや参照でもOK
  // 例として内部の ofSerial を開くメソッドを用意する
  bool setupOF(const std::string &portName, int baud);
#endif

  // 更新処理(読み取り・コールバック呼び出し)
  void update();
    
  // 1byte処理するメソッド（updateで使う）
  void processIncomingByte(char c);

  // 送信(<cmd>:<payload> の形にエスケープして送る)
  void send(const char* cmd, const unsigned char* data, int length);
  // 文字列だけ送りたい場合のシンプルオーバーロード
  void send(const char* cmd, const char* str);
    
  bool isInitialized();
  void close();

    // シリアルの参照などを返す
#ifdef ARDUINO
  Stream* getSerial() { return serial; }
#else
  ofSerial& getSerial() { return serial; }
#endif
    
private:
  // リスナーを保持する構造体
  struct CommandEntry {
    const char* cmd;
    SerialCallback callback;
  };

  static const int MAX_LISTENERS = 32;
  CommandEntry listeners[MAX_LISTENERS];
  int listenerCount;

  // 受信用バッファ
  static const int BUFFER_SIZE = 256;
  char rxBuffer[BUFFER_SIZE];
  int rxLen;

  // 直前に '\' が来たかどうか
  bool escapeNext;

  // Arduino/OF で違うシリアルハンドルを持つ
#ifdef ARDUINO
  Stream* serial;
#else
  ofSerial serial;
#endif

  // 内部処理: 1バイト読み取り
  // ArduinoとopenFrameworksで実装が違う
  int readByte();

  // 内部処理: 1バイト送信
  void writeByte(unsigned char c);

  // 内部処理: コマンド実行
  void execCmd(const char* cmdline);

  // 内部処理: バッファをリセット
  void resetBuffer();

  // 内部処理: ペイロードのエスケープ処理(送信時)
  // ':' と '\\' と '\n' はバックスラッシュでエスケープ
  // 他にも入れたければ増やす
  void writeEscaped(const unsigned char* data, int length);

  // 内部処理: ペイロード部分のエスケープ解除(受信時)
  // バッファ内で直接やってもいいが、ここでは単純化
  int unescapePayload(char* payload, int inLen);
};
