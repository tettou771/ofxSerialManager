ofxSerialManager

Author: tettou771 (tettou771@gmail.com)

概要

ofxSerialManager は Arduino と openFrameworks (OF) の両方で同じコードを使ってシリアル通信を扱えるようにしたクラスライブラリ。
	•	Arduinoの場合は Stream* を扱い、Serial や Serial1 などを渡せる。
	•	openFrameworksの場合は ofSerial を内部で使う。

メッセージ形式は、<cmd>:<payload>\n を1パケットとしてやり取りする。
ペイロード部分にはバイナリを含められるように、特定の文字 (':', '\', '\n') をバックスラッシュでエスケープして送信・受信する仕組みを用意している。

特徴
	•	複数インスタンス に対応 (シングルトンではない)
	•	コマンド名 (<cmd>) は可視ASCII文字を想定
	•	ペイロード (<payload>) はバイナリ送受信可能（内部的にエスケープで回避）
	•	addListener(cmd, callback) でコマンドごとのコールバック関数を登録できる
	•	\n をパケット区切りとし、\ によるエスケープで改行や特殊文字をペイロードに含められる
	•	Arduino / OF 両方で同じソースをビルドできる

使い方 (Arduino)
	1.	ofxSerialManager.h と ofxSerialManager.cpp を同じフォルダに配置し、#include "ofxSerialManager.h" を追加。
	2.	インスタンス生成 → setup(&Serial) で Arduino のシリアルを割り当て → addListener() でコマンドを登録 → loop() で定期的に update() を呼ぶ。

#include "ofxSerialManager.h"

ofxSerialManager serialManager;

void handleWrite(const char* payload, int length) {
  // payload は length バイトのバイナリデータ
  // 例: 文字列として確認したいなら一時バッファにコピーして終端を付ける
  char* buf = new char[length + 1];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  Serial.print("handleWrite got: ");
  Serial.println(buf);

  delete[] buf;
}

void setup() {
  Serial.begin(115200);
  serialManager.setup(&Serial);

  serialManager.addListener("write", handleWrite);
}

void loop() {
  serialManager.update();

  // 送信例:
  // serialManager.send("hello", "Hello from Arduino!");
  // → "hello:Hello from Arduino!\n" が送られる (内部でエスケープされる場合あり)
}

Arduino IDEのシリアルモニタでバイナリ送信について
	•	標準のシリアルモニタは**\ を1つ入力しても実際には2つに変換される**場合がある。
	•	そのため、バイナリを \x01 のように送るには \\x01 と2連バックスラッシュを書く必要がある場合が多い。
	•	例: 「0x01 バイトを送信したい」ときにはシリアルモニタに

write:\\x01

と入力して送る。

	•	途中に \0 (ヌルバイト) を含むようなペイロードを送る場合も、上記エスケープと二重バックスラッシュが必要。
	•	大量バイナリや制御文字を頻繁に送信したい場合は、TeraTerm / minicom / Pythonスクリプト等の専用ツールを使う方が扱いやすい。

使い方 (openFrameworks)
	1.	ofxSerialManager.h と ofxSerialManager.cpp を src フォルダなどにコピー。
	2.	#include "ofxSerialManager.h" を追加。
	3.	ofApp クラスなどでインスタンス生成 → setupOF(portName, baud) でポートを開く → update() で定期的に呼ぶ。

#include "ofMain.h"
#include "ofxSerialManager.h"

class ofApp : public ofBaseApp {
public:
    ofxSerialManager serialManager;

    static void handleWrite(const char* payload, int length) {
        // 受け取ったバイナリをログ出力
        ofLog() << "handleWrite length=" << length;
    }

    void setup(){
        serialManager.setupOF("/dev/tty.usbmodem12345", 115200);
        serialManager.addListener("write", handleWrite);
    }

    void update(){
        serialManager.update();
    }

    void keyPressed(int key){
        // 送信例
        string msg = "Hello from openFrameworks!";
        serialManager.send("info", msg.c_str());
    }
};

API概要
	•	ofxSerialManager()
コンストラクタ (複数インスタンス可)。 バッファ初期化など。
	•	bool addListener(const char* cmd, SerialCallback callback)
<cmd>（例: "write", "led" など）とそれを処理するコールバック関数を登録。
	•	SerialCallback は typedef void (*SerialCallback)(const char* payload, int length);
	•	登録数の上限 (MAX_LISTENERS) に達すると false を返す。
	•	setup() / setupOF()
	•	Arduino: void setup(Stream* s)
	•	例: serialManager.setup(&Serial);
	•	openFrameworks: bool setupOF(const std::string &portName, int baud)
	•	例: serialManager.setupOF("/dev/tty.usbmodemXXXX", 115200);
	•	void update()
シリアルポートから受信できるだけ読み取り、\n 区切りでコマンドを確定し、コールバックを呼び出す。
	•	void send(const char* cmd, const unsigned char* data, int length)
<cmd>:<payload>\n の形式で送信。
	•	ペイロード内に ':', '\', '\n' があればバックスラッシュでエスケープして送る。
	•	その結果、受信側では \n 区切りでパケット認識し、バックスラッシュを展開してバイナリに戻す。
	•	void send(const char* cmd, const char* str)
文字列版。 str を (const unsigned char*)str にキャストして length = strlen(str) で呼ぶだけ。

バイナリとエスケープ仕様
	•	送信時: ':'、 '\'、 '\n' を見つけたら \' を挿入してエスケープする。 '\n' は \\n のように送信される。
	•	受信時:
	•	\n までを1パケットとし、そこを <cmd>:<payload> に分割。
	•	<cmd> は可視ASCIIのみを想定。
	•	<payload> で \xHH（16進2桁）をバイナリ1バイトに戻したり、 \n を改行に戻したりする。
	•	Arduino IDEのシリアルモニタではバックスラッシュ入力が2個に増える場合があるので、\x01 と打つつもりなら実際は \\x01 と入力する必要があることが多い。

カスタマイズ (MAX_LISTENERS / BUFFER_SIZEなど)
	•	#define ではなくクラス内の static const int で宣言されているので、ヘッダファイルの

static const int MAX_LISTENERS = 32;
static const int BUFFER_SIZE = 256;

を書き換えることで調整できる。

	•	大きくしすぎるとメモリ（特にArduinoのSRAM）を圧迫するので注意。

注意事項
	•	コマンド名 (<cmd>) は制御文字や全角文字などは想定していない。
	•	途中に '\0' が含まれるようなペイロードは、文字列関数 (strlen など) と相性が悪いので注意。コールバックで length を頼りに使ってね。
	•	Arduinoの標準シリアルモニタはテキストメインなので、バイナリ送受にはちょっと面倒が多い。
	•	必要なら別のターミナルソフトや Python の pyserial などで送るとやりやすい。

以上のように使えば、ArduinoとopenFrameworksのどちらでも同じコードをビルド・実行し、同様のシリアルコマンド/バイナリやり取りを実現できる。バイナリを多用するときは、エスケープや文字列長さの扱いに気をつけてね！