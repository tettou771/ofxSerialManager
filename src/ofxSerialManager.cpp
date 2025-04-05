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
bool ofxSerialManager::setupOF(const std::string& portName, int baud) {
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
#ifdef ARDUINO
    int availableBytes = serial->available();
#else
    int availableBytes = serial.isInitialized() ? serial.available() : 0;
#endif
    if (availableBytes <= 0) return;

    // rxBuffer の残り領域に収まる量だけ読む
    int toRead = availableBytes;
    if (rxLen + toRead >= BUFFER_SIZE) {
        toRead = BUFFER_SIZE - rxLen - 1; // ヌル終端のために1バイト余裕
    }
    if (toRead <= 0) return;

    char tempBuffer[BUFFER_SIZE];
#ifdef ARDUINO
    int n = serial->readBytes(tempBuffer, toRead);
#else
    int n = serial.readBytes(tempBuffer, toRead);
#endif
    for (int i = 0; i < n; i++){
        processIncomingByte(tempBuffer[i]);
    }
}

void ofxSerialManager::processIncomingByte(char c) {
    if (escapeNext) {
        if (rxLen < BUFFER_SIZE - 1) {
            rxBuffer[rxLen++] = c;
            rxBuffer[rxLen] = '\0';
        }
        escapeNext = false;
    } else {
        if (c == '\\') {
            escapeNext = true;
        } else if (c == '\n') {
            if (rxLen > 0) {
                rxBuffer[rxLen] = '\0';
                execCmd(rxBuffer, rxLen);
            }
            resetBuffer();
        } else {
            if (rxLen < BUFFER_SIZE - 1) {
                rxBuffer[rxLen++] = c;
                rxBuffer[rxLen] = '\0';
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
  } else {
    return -1;
  }
#else
  // openFrameworks版
  if (serial.isInitialized()) {
    int b = serial.readByte();
    if (b == OF_SERIAL_NO_DATA) {
      return -1;  // データ無し
    }
    if (b == OF_SERIAL_ERROR) {
      return -1;  // エラー扱い
    }
    return b & 0xFF;  // 正常読み取り
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
    serial->flush();  // 送信完了を待つ
    delayMicroseconds(100);
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
void ofxSerialManager::execCmd(const char* cmdline, int length) {
  // cmdlineを "<cmd>:<payload>" に分割
  char temp[BUFFER_SIZE];
  memcpy(temp, cmdline, length); // 注: バイナリの可能性を考慮して、strncpyは使わない
  temp[sizeof(temp) - 1] = '\0';

  // ':' を探す
  char* sep = strchr(temp, ':');
  if (!sep) {
    // ':'がないならpayloadは空
    // コマンドだけASCII化(非表示文字を除去)して実行
    // ここでは雑にやる
    for (char* p = temp; *p; p++) {
      if (!isprint((unsigned char)*p)) {
        *p = '\0';  // そこで終端しちゃう
        break;
      }
    }
    // リスナー探し
    for (int i = 0; i < listenerCount; i++) {
      if (strcmp(listeners[i].cmd, temp) == 0) {
        listeners[i].callback("", 0);
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

  int payloadLen = length - (payloadPart - temp);  // 受信行の残り分(エスケープ前)
  // (本当は、最初に readByte() で読んだトータル長を渡した方が正確)

  // unescapePayload() が返してきた最終バイナリ長を保持
  int actualLen = unescapePayload(payloadPart, payloadLen);

  // コールバックを呼ぶ
  for (int i = 0; i < listenerCount; i++) {
    if (strcmp(listeners[i].cmd, cmdPart) == 0) {
      // payloadPart に実際のバイナリが書き込まれている
      // actualLen がその長さ
      listeners[i].callback(payloadPart, actualLen);
    }
  }

  // 見つからなければエラー返す？
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
    int cmdLen = strlen(cmd);
    // worst-case: すべてのバイトがエスケープ必要なら長さは倍になる
    int maxMessageSize = cmdLen + 1 + length * 2 + 1;
    unsigned char* messageBuffer = new unsigned char[maxMessageSize];
    int pos = 0;
    
    // コマンド部分
    for (int i = 0; i < cmdLen; i++){
        messageBuffer[pos++] = (unsigned char)cmd[i];
    }
    messageBuffer[pos++] = ':';
    
    // ペイロード部分：エスケープ処理を含む
    for (int i = 0; i < length; i++){
        unsigned char c = data[i];
        if (c == ':' || c == '\\' || c == '\n') {
            messageBuffer[pos++] = '\\';
            if (c == '\n') {
                messageBuffer[pos++] = 'n';
                continue;
            }
        }
        messageBuffer[pos++] = c;
    }
    messageBuffer[pos++] = '\n';
    
#ifdef ARDUINO
    serial->write(messageBuffer, pos);
    serial->flush();  // 送信完了を待つ
    delayMicroseconds(100);
#else
    if (serial.isInitialized()){
        serial.writeBytes(messageBuffer, pos);
    }
#endif
    
    delete[] messageBuffer;
}

// 文字列版
void ofxSerialManager::send(const char* cmd, const char* str) {
  if (!str) {
    // nullなら長さ0として扱う
    unsigned char dummy[1] = { 0 };
    send(cmd, dummy, 0);
    return;
  }
  send(cmd, (const unsigned char*)str, strlen(str));
}

// payloadなし
void ofxSerialManager::send(const char* cmd) {
    send(cmd, "");
}

// 初期化されているかどうか
bool ofxSerialManager::isInitialized() {
#ifdef ARDUINO
  return serial;
#else
  return serial.isInitialized();
#endif
}

void ofxSerialManager::close() {
#ifdef ARDUINO
  return;
#else
  return serial.close();
#endif
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
// "?" があれば ? をそのままバイナリとして取り出す。
// ただし '\n' は本物の改行に戻す。
// "\x01" のようなものはバイトコードとみなして 0x01 などにする
// --------------------------------------
int ofxSerialManager::unescapePayload(char* payload, int inLen) {
  // inLen は "payloadPart" の文字数（改行除いた長さ）を渡す
  // (改行で区切った時点での長さとか)
  // 出力バッファは payload自身を使いまわす
  // 返すのは「エスケープ展開後の実際のバイナリの長さ」
  int writePos = 0;
  bool esc = false;

  for (int i = 0; i < inLen; i++) {
    unsigned char c = (unsigned char)payload[i];
    if (!esc) {
      if (c == '\\') {
        esc = true;  // 次の1文字をエスケープ対象に
      } else {
        payload[writePos++] = c;
      }
    } else {
      // エスケープ中
      if (c == 'n') {
        payload[writePos++] = '\n';
      } else if (c == 'x') {
        // \x で始まる → 次の2文字をHEXとみなしてバイト化
        if (i + 2 < inLen) {
          char hex[3];
          hex[0] = payload[i + 1];
          hex[1] = payload[i + 2];
          hex[2] = '\0';
          unsigned char val = (unsigned char)strtol(hex, NULL, 16);
          payload[writePos++] = val;
          i += 2;  // 2文字ぶん消費
        }
      } else {
        // それ以外はそのまま
        payload[writePos++] = c;
      }
      esc = false;
    }
  }

  // ここではヌル終端は付けずに「長さ」で管理する想定
  return writePos;  // 実際のサイズ
}
