#include "ofxSerialManager.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// --------------------------------------
// コンストラクタ
// --------------------------------------
ofxSerialManager::ofxSerialManager() {
  listenerCount = 0;
  resetBuffer();
  escapeNext = false;

#ifdef ARDUINO
  serial = nullptr;
#endif
}

// --------------------------------------
// リスナー登録
// --------------------------------------
bool ofxSerialManager::addListener(const char* cmd, SerialCallback callback) {
  if (listenerCount >= MAX_LISTENERS) {
    return false;
  }
  listeners[listenerCount].cmd = cmd;
  listeners[listenerCount].callback = callback;
  listenerCount++;
  return true;
}

// --------------------------------------
// Arduino版: Stream* をセット
// --------------------------------------
#ifdef ARDUINO
void ofxSerialManager::setup(Stream* s) {
  serial = s;
  resetBuffer();
}
#else
// --------------------------------------
// openFrameworks版: シリアルポートを開く例
// --------------------------------------
bool ofxSerialManager::setupOF(const std::string &portName, int baud) {
  // ofSerialの open や setup のやり方は環境で異なる
  // ここではざっくり例
  bool ret = serial.setup(portName, baud);
  resetBuffer();
  return ret;
}
#endif

// --------------------------------------
// update
// --------------------------------------
void ofxSerialManager::update() {
  // 受信できるだけ読み取る
  while (true) {
    int b = readByte(); // -1ならデータなし
    if (b < 0) {
      break; // もう無い
    }

    unsigned char c = (unsigned char)b;

    // バイナリも含め受け取るけど、「改行がメッセージ区切り」。
    // ただし '\' でエスケープされてる場合はペイロードの一部にする。
    if (escapeNext) {
      // 前の文字が '\' だったので、これは何であれペイロード文字として扱う
      if (rxLen < BUFFER_SIZE - 1) {
        rxBuffer[rxLen++] = (char)c;
        rxBuffer[rxLen] = '\0';
      }
      escapeNext = false;
    }
    else {
      if (c == '\\') {
        // 次の文字をエスケープ扱いにする
        escapeNext = true;
      }
      else if (c == '\n') {
        // 改行が来たので1行(1コマンド)確定
        if (rxLen > 0) {
          rxBuffer[rxLen] = '\0';
          execCmd(rxBuffer);
        }
        resetBuffer();
      }
      else {
        // コマンド部分は非表示文字を無視してOK
        // ただしペイロード中はバイナリでも入れたい可能性あるが、
        // 簡単のため、まずはコマンド部分だけASCIIチェックしよう

        // コロンが出るまでを「コマンド部」、それ以降を「ペイロード部」として
        // ざっくりと格納して後でexecCmd()で処理する形にする

        if (rxLen < BUFFER_SIZE - 1) {
          rxBuffer[rxLen++] = (char)c;
          rxBuffer[rxLen] = '\0';
        }
      }
    }
  }
}

// --------------------------------------
// 内部処理: シリアルから1バイト読み取り
// --------------------------------------
int ofxSerialManager::readByte() {
#ifdef ARDUINO
  if (!serial) return -1;
  if (serial->available() > 0) {
    return serial->read();
  }
  else {
    return -1;
  }
#else
  // openFrameworks版
  if (serial.isInitialized()) {
    int b = serial.readByte();
    if (b == OF_SERIAL_NO_DATA) {
      return -1; // データ無し
    }
    if (b == OF_SERIAL_ERROR) {
      return -1; // エラー扱い
    }
    return b & 0xFF; // 正常読み取り
  }
  return -1;
#endif
}

// --------------------------------------
// 内部処理: シリアルへ1バイト送信
// --------------------------------------
void ofxSerialManager::writeByte(unsigned char c) {
#ifdef ARDUINO
  if (serial) {
    serial->write(c);
  }
#else
  if (serial.isInitialized()) {
    serial.writeByte(c);
  }
#endif
}

// --------------------------------------
// コマンド実行
// --------------------------------------
void ofxSerialManager::execCmd(const char* cmdline) {
  // cmdlineを "<cmd>:<payload>" に分割
  // コマンド部はASCII可視文字以外を無視するとしたければトリミング
  char temp[BUFFER_SIZE];
  strncpy(temp, cmdline, sizeof(temp));
  temp[sizeof(temp)-1] = '\0';

  // ':' を探す
  char* sep = strchr(temp, ':');
  if (!sep) {
    // ':'がないならpayloadは空
    // コマンドだけASCII化(非表示文字を除去)して実行
    // ここでは雑にやる
    for (char* p = temp; *p; p++) {
      if (!isprint((unsigned char)*p)) {
        *p = '\0'; // そこで終端しちゃう
        break;
      }
    }
    // リスナー探し
    for (int i = 0; i < listenerCount; i++) {
      if (strcmp(listeners[i].cmd, temp) == 0) {
        listeners[i].callback("", 0);
        return;
      }
    }
    // 見つからなければエラー的に
    // ここでは何もしないか、send("error", temp)してもいい
    return;
  }

  *sep = '\0';
  char* cmdPart = temp;
  char* payloadPart = sep + 1;

  // cmdPartの不可視文字削除
  {
    int writePos = 0;
    for (int readPos = 0; cmdPart[readPos] != '\0'; readPos++) {
      if (isprint((unsigned char)cmdPart[readPos])) {
        cmdPart[writePos++] = cmdPart[readPos];
      }
    }
    cmdPart[writePos] = '\0';
  }

  // payloadPartをエスケープ解除
  // （今回は実行時にまとめてやる。実際にはupdate中にやってもOK）
  unescapePayload(payloadPart);

  // コールバック呼び出し
  for (int i = 0; i < listenerCount; i++) {
    if (strcmp(listeners[i].cmd, cmdPart) == 0) {
      listeners[i].callback(payloadPart, strlen(payloadPart));
      return;
    }
  }

  // 見つからなければエラー的に
  // send("error", cmdPart)など
}

// --------------------------------------
// バッファリセット
// --------------------------------------
void ofxSerialManager::resetBuffer() {
  rxBuffer[0] = '\0';
  rxLen = 0;
  escapeNext = false;
}

// --------------------------------------
// 送信
//  (1) cmd:string
//  (2) data:バイナリ, length指定
// --------------------------------------
void ofxSerialManager::send(const char* cmd, const unsigned char* data, int length) {
  // まず cmd
  // コマンド部分は基本的に可視文字のみを想定。ここではノーチェックでそのまま送る
  // <cmd>:<payload>\n の形
  if (!cmd) return;

  // cmd:
  for (size_t i = 0; i < strlen(cmd); i++) {
    writeByte((unsigned char)cmd[i]);
  }
  writeByte(':');

  // payload (エスケープして送る)
  writeEscaped(data, length);

  // 改行(メッセージ区切り)
  writeByte('\n');
}

// 文字列版
void ofxSerialManager::send(const char* cmd, const char* str) {
  if (!str) {
    // nullなら長さ0として扱う
    unsigned char dummy[1] = {0};
    send(cmd, dummy, 0);
    return;
  }
  send(cmd, (const unsigned char*)str, strlen(str));
}

// --------------------------------------
// payload送信用のエスケープ処理
// ':' と '\' と '\n' をエスケープ
// --------------------------------------
void ofxSerialManager::writeEscaped(const unsigned char* data, int length) {
  for (int i = 0; i < length; i++) {
    unsigned char c = data[i];
    if (c == ':' || c == '\\' || c == '\n') {
      writeByte('\\');
      // 特別に '\n' を 'n' で表現するなど
      if (c == '\n') {
        writeByte('n');
        continue;
      }
    }
    writeByte(c);
  }
}

// --------------------------------------
// payload受信時のエスケープ解除
// 「\x」 があれば x をそのままバイナリとして取り出す。
// ただし '\n' は本物の改行に戻す。
// --------------------------------------
void ofxSerialManager::unescapePayload(char* payload) {
  int len = strlen(payload);
  int writePos = 0;
  bool esc = false;
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)payload[i];
    if (!esc) {
      if (c == '\\') {
        // 次の文字をエスケープ扱い
        esc = true;
      } else {
        payload[writePos++] = c;
      }
    } else {
      // エスケープ中
      if (c == 'n') {
        payload[writePos++] = '\n';
      } else {
        // それ以外ならそのまま
        payload[writePos++] = c;
      }
      esc = false;
    }
  }
  payload[writePos] = '\0';
}