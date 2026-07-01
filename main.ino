#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>

// Pinagem
#define RST_PIN   9
#define SS_PIN    10

#define BTN_UP    2
#define BTN_DOWN  3
#define BTN_OK    4

#define OLED_ADDR 0x3C

MFRC522 mfrc522(SS_PIN, RST_PIN);
SSD1306AsciiWire oled;

// Dados do cartao clonado
// MIFARE Classic 1K = 16 setores x 4 blocos x 16 bytes = 1024 bytes
byte cardDump[64][16];
bool cardLoaded = false;

// Estados
enum State { MENU, READING, READ_DONE, READ_FAIL, WRITING, WRITE_DONE, WRITE_FAIL, NO_CARD_ERR };
State state = MENU;
int menuIndex = 0;
const char* menuItems[2] = { "Ler cartao", "Gravar clone" };

bool lastUp = HIGH, lastDown = HIGH, lastOk = HIGH;

bool pressed(int pin, bool &lastState) {
  bool cur = digitalRead(pin);
  bool fired = (lastState == HIGH && cur == LOW);
  lastState = cur;
  return fired;
}

// Helpers de tela
void printHexByte(byte b) {
  if (b < 0x10) oled.print('0');
  oled.print(b, HEX);
}

void drawMenu() {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(F("MIFARE Clonador"));

  oled.setCursor(0, 1);
  oled.print(F("Cartao: "));
  if (cardLoaded) {
    for (byte i = 0; i < 4; i++) {
      printHexByte(cardDump[0][i]);
      oled.print(' ');
    }
  } else {
    oled.print(F("--"));
  }

  for (byte i = 0; i < 2; i++) {
    oled.setCursor(0, 3 + i);
    oled.print(i == menuIndex ? F("> ") : F("  "));
    oled.print(menuItems[i]);
  }
}

void drawReading() {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(F("Leitura"));
  oled.setCursor(0, 2);
  oled.print(F("Aproxime o cartao"));
  oled.setCursor(0, 3);
  oled.print(F("no leitor..."));
  oled.setCursor(0, 6);
  oled.print(F("OK = cancelar"));
}

void drawReadResult(bool ok) {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(ok ? F("Leitura OK!") : F("Falha na leitura"));

  if (ok) {
    oled.setCursor(0, 2);
    oled.print(F("UID: "));
    
    for (byte i = 0; i < 4; i++) {
      printHexByte(cardDump[0][i]);
      oled.print(' ');
    }
  } else {
    oled.setCursor(0, 2);
    oled.print(F("Setor c/ key"));
    oled.setCursor(0, 3);
    oled.print(F("diferente da padrao"));
  }

  oled.setCursor(0, 6);
  oled.print(F("OK = menu"));
}

void drawWriting() {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(F("Gravacao"));
  oled.setCursor(0, 2);
  oled.print(F("Aproxime o cartao"));
  oled.setCursor(0, 3);
  oled.print(F("MAGIC no leitor..."));
  oled.setCursor(0, 6);
  oled.print(F("OK = cancelar"));
}

void drawWriteResult(bool ok) {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(ok ? F("Gravacao OK!") : F("Falha na gravacao"));

  if (!ok) {
    oled.setCursor(0, 2);
    oled.print(F("Cartao nao e magic"));
    oled.setCursor(0, 3);
    oled.print(F("ou perdeu contato"));
  }

  oled.setCursor(0, 6);
  oled.print(F("OK = menu"));
}

void drawNoCard() {
  oled.clear();
  oled.setCursor(0, 0);
  oled.print(F("Nenhum cartao lido"));
  oled.setCursor(0, 2);
  oled.print(F("Leia um cartao"));
  oled.setCursor(0, 3);
  oled.print(F("antes de gravar"));
  oled.setCursor(0, 6);
  oled.print(F("OK = menu"));
}

// Lê o UID do cartão e salva na memória
bool readFullCard() {
  byte uidLen = mfrc522.uid.size;
  memset(cardDump[0], 0, 16);
  for (byte i = 0; i < uidLen && i < 4; i++) cardDump[0][i] = mfrc522.uid.uidByte[i];

  byte bcc = 0;
  for (byte i = 0; i < 4; i++) bcc ^= cardDump[0][i];
  cardDump[0][4] = bcc;
  cardDump[0][5] = mfrc522.uid.sak;

  Serial.print(F("UID capturado: "));
  for (byte i = 0; i < 4; i++) { Serial.print(cardDump[0][i], HEX); Serial.print(' '); }
  Serial.println();

  return true;
}

bool writeFullCard() {
  byte newUid[4] = { cardDump[0][0], cardDump[0][1], cardDump[0][2], cardDump[0][3] };
  if (!mfrc522.MIFARE_SetUid(newUid, (byte)4, true)) return false;
  mfrc522.PICC_HaltA();
  return true;
}

// Setup / Loop
void setup() {
  Serial.begin(9600);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  SPI.begin();
  mfrc522.PCD_Init();

  Wire.begin();
  oled.begin(&Adafruit128x64, OLED_ADDR);
  oled.setFont(Adafruit5x7);

  drawMenu();
}

void loop() {
  bool up = pressed(BTN_UP, lastUp);
  bool down = pressed(BTN_DOWN, lastDown);
  bool ok = pressed(BTN_OK, lastOk);
  delay(15);

  switch (state) {

    case MENU:
      if (up)   { menuIndex = (menuIndex + 1) % 2; drawMenu(); }
      if (down) { menuIndex = (menuIndex + 1) % 2; drawMenu(); }
      if (ok) {
        if (menuIndex == 0) {
          state = READING;
          drawReading();
        } else {
          if (!cardLoaded) {
            state = NO_CARD_ERR;
            drawNoCard();
          } else {
            mfrc522.PCD_Init();
            state = WRITING;
            drawWriting();
          }
        }
      }
      break;

    case READING:
      if (ok) { state = MENU; drawMenu(); break; }
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        bool ok2 = readFullCard();
        cardLoaded = ok2;
        state = ok2 ? READ_DONE : READ_FAIL;
        drawReadResult(ok2);
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
      break;

    case WRITING:
      if (ok) { state = MENU; drawMenu(); break; }
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        bool ok2 = writeFullCard();
        state = ok2 ? WRITE_DONE : WRITE_FAIL;
        drawWriteResult(ok2);
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
      break;

    case READ_DONE:
    case READ_FAIL:
    case WRITE_DONE:
    case WRITE_FAIL:
    case NO_CARD_ERR:
      if (ok) { state = MENU; drawMenu(); }
      break;
  }
}
