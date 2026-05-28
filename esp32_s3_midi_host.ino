/*
=====================================================
ULTIMATE MIDI HOST V5.0 - ESP32-S3 WITH WEB INTERFACE
VERSÃO ESTÁVEL: WebServer síncrono + Web primeiro + 2 pedais de expressão
VERSÃO PUBLICA: 1.0 (CORREÇÃO DE DISPLAY PROGRAM CHANGE)
=====================================================
*/

#define MIDI_DEBUG 1

#if MIDI_DEBUG
#define DBG(x) Serial.print(x)
#define DBGLN(x) Serial.println(x)
#else
#define DBG(x)
#define DBGLN(x)
#endif

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>

#include <Preferences.h>
#include <ESP32Encoder.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Biblioteca MIDI Clássica (DIN 5 Pinos)
#include <MIDI.h>

// Bibliotecas USB Host MIDI
#include <ESP32_Host_MIDI.h>
#include <USBConnection.h>

// --- BIBLIOTECAS PARA A INTERFACE WEB (versão estável) ---
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Instancia o transporte físico de USB Host
USBConnection usbHost;
// Servidor Web síncrono (estável com USB Host MIDI)
WebServer server(80);

/*
=====================================================
OLED CONFIG (SPI MODE)
=====================================================
*/
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_MOSI 11
#define OLED_CLK 12
#define OLED_DC 9
#define OLED_CS 8
#define OLED_RESET 10

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// Função que gerencia a exibição da tela de abertura
void showSplashScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  // Moldura externa decorativa
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawRect(2, 2, 124, 60, SSD1306_WHITE);

  // Título Principal
  display.setTextSize(1);
  display.setCursor(10, 14);
  display.print("ULTIMATE MIDI HOST");

  // Versão com linha decorativa
  display.drawLine(15, 28, 113, 28, SSD1306_WHITE);
  display.setCursor(31, 34);
  // Mudou de 46 para 31 (margens iguais de 31px)
  display.print("VERSION 1.0");
  // Barra de carregamento fictícia (só pelo visual)
  display.drawRect(24, 48, 80, 6, SSD1306_WHITE);
  display.display();
  delay(500);
  // Animação de preenchimento da barra de load
  for (int w = 0; w <= 76; w += 4) {
    display.fillRect(26, 50, w, 2, SSD1306_WHITE);
    display.display();
    delay(40);  // Controla a velocidade do carregamento
  }

  delay(600);
  // Tempo final para o usuário ler a tela antes de liberar o MIDI
}

/*
=====================================================
ENCODER & TIMING
=====================================================
*/
#define ENCODER_PIN_A 4
#define ENCODER_PIN_B 5
#define ENCODER_BUTTON 6

ESP32Encoder menuEncoder;
unsigned long lastEditTime = 0;
bool pendingSave = false;
const unsigned long SAVE_DELAY = 2000;
bool refreshNeeded = false;

/*
=====================================================
USB MIDI & DIN
=====================================================
*/
HardwareSerial MIDISerial(1);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDISerial, MIDI_DIN);

/*
=====================================================
PREFERENCES & STORAGE STRUCTS
=====================================================
*/
Preferences prefs;
enum ScreenState { SCREEN_MONITOR,
                   SCREEN_MENU,
                   SCREEN_EDIT,
                   SCREEN_EXP_EDIT };
enum OperationMode { MODE_MONITOR,
                     MODE_REMAPPING };
enum CurveType { CURVE_LIN,
                 CURVE_LOG,
                 CURVE_ALG,
                 CURVE_SCV };
const char *curveNames[] = { "LIN", "LOG", "ALG", "SCV" };
enum EditField { EDIT_ENABLE,
                 EDIT_IN_CH,
                 EDIT_OUT_CH,
                 EDIT_IN_CC,
                 EDIT_OUT_CC,
                 EDIT_IN_VALUE1,
                 EDIT_OUT_VALUE1,
                 EDIT_IN_VALUE2,
                 EDIT_OUT_VALUE2 };
enum ExpField { EXP1_IN,
                EXP1_OUT,
                EXP1_CURVE,
                EXP2_IN,
                EXP2_OUT,
                EXP2_CURVE };
struct MidiRemap {
  bool enabled;
  byte inputChannel;
  byte outputChannel;
  byte inputCC;
  byte outputCC;
  byte inputValue1;
  byte outputValue1;
  byte inputValue2;
  byte outputValue2;
};

// Assinatura atualizada para forçar a Flash a limpar o lixo antigo de ranges travados
#define REMAP_SIGNATURE 0xA5B1
#define REMAP_VERSION 4

struct RemapStorage {
  uint16_t signature;
  uint8_t version;
  OperationMode operationMode;
  MidiRemap slots[10];
  byte exp1InputCC;
  byte exp1OutputCC;
  byte exp1Curve;
  byte exp2InputCC;
  byte exp2OutputCC;
  byte exp2Curve;
};

/*
=====================================================
GLOBALS
=====================================================
*/
RemapStorage remapStorage;
MidiRemap remapSlots[10];

ScreenState currentScreen = SCREEN_MONITOR;
OperationMode currentMode = MODE_REMAPPING;
EditField currentField = EDIT_ENABLE;
ExpField currentExpField = EXP1_IN;
int selectedItem = 0;
int menuOffset = 0;
#define TOTAL_MENU_ITEMS 12

byte exp1InputCC = 11;
byte exp1OutputCC = 11;
byte exp1Curve = CURVE_LIN;
byte exp2InputCC = 4;
byte exp2OutputCC = 4;
byte exp2Curve = CURVE_LIN;

char midiType[20] = "---";
byte midiChannel = 0;
int midiData1 = 0;
int midiData2 = 0;
long lastEncoderValue = 0;
// Variáveis de remapeamento isoladas para o OLED não ser sobrescrito por ruidos do loop
bool isRemapped = false;
byte remapInCC = 0;
byte remapInVal = 0;
byte remapOutCC = 0;
byte remapOutVal = 0;

/*
=====================================================
INTERFACE WEB - LAYOUT HTML/CSS/JS (PROGMEM)
=====================================================
*/
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ultimate MIDI Host - Dashboard</title>
    <style>
        :root { --bg: #121214; --card: #1a1a1e; --accent: #00adb5; --text: #eeeeee; --muted: #888888; --border: #2a2a30; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; }
        .container { max-width: 900px; margin: 0 auto; }
        header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid var(--border); padding-bottom: 15px; margin-bottom: 25px; }
        h1 { margin: 0; font-size: 24px; color: var(--accent); letter-spacing: 1px; }
        .global-control { background: var(--card); padding: 15px; border-radius: 8px; border: 1px solid var(--border); display: flex; gap: 20px; align-items: center; margin-bottom: 25px; }
        select, input { background: #222226; border: 1px solid var(--border); color: var(--text); padding: 8px 12px; border-radius: 4px; font-size: 14px; }
        select:focus, input:focus { border-color: var(--accent); outline: none; }
        .slot-grid { display: grid; grid-template-columns: 1fr; gap: 15px; margin-bottom: 25px; }
        .card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 15px; display: flex; flex-direction: column; gap: 12px; }
        .card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 8px; }
        .card-title { font-weight: bold; font-size: 16px; color: var(--accent); }
        .row { display: flex; flex-wrap: wrap; gap: 10px; }
        .field { flex: 1; min-width: 130px; display: flex; flex-direction: column; gap: 5px; font-size: 12px; color: var(--muted); }
        .field input { font-size: 14px; }
        .btn { background: var(--accent); border: none; color: #fff; padding: 12px 24px; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; width: 100%; transition: opacity 0.2s; }
        .btn:hover { opacity: 0.9; }
        .switch { position: relative; display: inline-block; width: 44px; height: 22px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #333; transition: .3s; border-radius: 22px; }
        .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
        input:checked + .slider { background-color: var(--accent); }
        input:checked + .slider:before { transform: translateX(22px); }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>ULTIMATE MIDI HOST V5.0</h1>
            <span style="color: var(--muted); font-size: 14px;">Painel de Controle Remoto</span>
        </header>

        <div class="global-control">
            <div class="field" style="flex:2;">
                <label style="color:var(--text); font-weight:bold;">Modo de Operação Geral:</label>
                <select id="globalMode">
                    <option value="0">MONITOR (Pass-Thru Direto)</option>
                    <option value="1">REMAPPING (Processamento de Regras)</option>
                </select>
            </div>
        </div>

        <h2 style="font-size: 18px; margin-bottom: 15px; border-left: 4px solid var(--accent); padding-left: 8px;">Pedais de Expressão Analog</h2>
        <div class="slot-grid" style="grid-template-columns: 1fr 1fr; gap: 20px;">
            <div class="card">
                <div class="card-header"><span class="card-title">EXP 1</span></div>
                <div class="row">
                    <div class="field"><label>CC Entrada</label><input type="number" id="exp1In" min="0" max="127"></div>
                    <div class="field"><label>CC Saída</label><input type="number" id="exp1Out" min="0" max="127"></div>
                    <div class="field">
                        <label>Curva</label>
                        <select id="exp1Crv">
                            <option value="0">Linear (LIN)</option>
                            <option value="1">Logarítmica (LOG)</option>
                            <option value="2">Exponencial (ALG)</option>
                            <option value="3">S-Curve (SCV)</option>
                        </select>
                    </div>
                </div>
            </div>
            <div class="card">
                <div class="card-header"><span class="card-title">EXP 2</span></div>
                <div class="row">
                    <div class="field"><label>CC Entrada</label><input type="number" id="exp2In" min="0" max="127"></div>
                    <div class="field"><label>CC Saída</label><input type="number" id="exp2Out" min="0" max="127"></div>
                    <div class="field">
                        <label>Curva</label>
                        <select id="exp2Crv">
                            <option value="0">Linear (LIN)</option>
                            <option value="1">Logarítmica (LOG)</option>
                            <option value="2">Exponencial (ALG)</option>
                            <option value="3">S-Curve (SCV)</option>
                        </select>
                    </div>
                </div>
            </div>
        </div>

        <h2 style="font-size: 18px; margin-top:25px; margin-bottom: 15px; border-left: 4px solid var(--accent); padding-left: 8px;">Slots de Remapeamento (Bancos 1 a 10)</h2>
        <form id="midiForm">
            <div class="slot-grid" id="slotsContainer"></div>
            <button type="button" class="btn" onclick="saveConfig()">GRAVAR CONFIGURAÇÕES NO HARDWARE</button>
        </form>
    </div>

    <script>
        let html = '';
        for(let i=0; i<10; i++) {
            html += `
            <div class="card">
                <div class="card-header">
                    <span class="card-title">SLOT B${i+1}</span>
                    <label class="switch">
                        <input type="checkbox" id="en_${i}">
                        <span class="slider"></span>
                    </label>
                </div>
                <div class="row">
                    <div class="field"><label>Canal IN</label><input type="number" id="ich_${i}" min="1" max="16"></div>
                    <div class="field"><label>Canal OUT</label><input type="number" id="och_${i}" min="1" max="16"></div>
                    <div class="field"><label>CC IN</label><input type="number" id="icc_${i}" min="0" max="127"></div>
                    <div class="field"><label>CC OUT</label><input type="number" id="occ_${i}" min="0" max="127"></div>
                </div>
                <div class="row">
                    <div class="field"><label>Min Valor IN</label><input type="number" id="iv1_${i}" min="0" max="127"></div>
                    <div class="field"><label>Max Valor IN</label><input type="number" id="iv2_${i}" min="0" max="127"></div>
                    <div class="field"><label>Min Valor OUT</label><input type="number" id="ov1_${i}" min="0" max="127"></div>
                    <div class="field"><label>Max Valor OUT</label><input type="number" id="ov2_${i}" min="0" max="127"></div>
                </div>
            </div>`;
        }
        document.getElementById('slotsContainer').innerHTML = html;
        window.onload = function() {
            fetch('/api/get_config')
            .then(response => response.json())
            .then(data => {
                document.getElementById('globalMode').value = data.mode;
                document.getElementById('exp1In').value = data.exp1In;
                document.getElementById('exp1Out').value = data.exp1Out;
                document.getElementById('exp1Crv').value = data.exp1Crv;
                document.getElementById('exp2In').value = data.exp2In;
                document.getElementById('exp2Out').value = data.exp2Out;
                document.getElementById('exp2Crv').value = data.exp2Crv;

                for(let i=0; i<10; i++) {
                    document.getElementById(`en_${i}`).checked = data.slots[i].en;
                    document.getElementById(`ich_${i}`).value = data.slots[i].ich;
                    document.getElementById(`och_${i}`).value = data.slots[i].och;
                    document.getElementById(`icc_${i}`).value = data.slots[i].icc;
                    document.getElementById(`occ_${i}`).value = data.slots[i].occ;
                    document.getElementById(`iv1_${i}`).value = data.slots[i].iv1;
                    document.getElementById(`iv2_${i}`).value = data.slots[i].iv2;
                    document.getElementById(`ov1_${i}`).value = data.slots[i].ov1;
                    document.getElementById(`ov2_${i}`).value = data.slots[i].ov2;
                }
            });
        };
        function saveConfig() {
            let config = {
                mode: parseInt(document.getElementById('globalMode').value),
                exp1In: parseInt(document.getElementById('exp1In').value),
                exp1Out: parseInt(document.getElementById('exp1Out').value),
                exp1Crv: parseInt(document.getElementById('exp1Crv').value),
                exp2In: parseInt(document.getElementById('exp2In').value),
                exp2Out: parseInt(document.getElementById('exp2Out').value),
                exp2Crv: parseInt(document.getElementById('exp2Crv').value),
                slots: []
            };
            for(let i=0; i<10; i++) {
                config.slots.push({
                    en: document.getElementById(`en_${i}`).checked,
                    ich: parseInt(document.getElementById(`ich_${i}`).value),
                    och: parseInt(document.getElementById(`och_${i}`).value),
                    icc: parseInt(document.getElementById(`icc_${i}`).value),
                    occ: parseInt(document.getElementById(`occ_${i}`).value),
                    iv1: parseInt(document.getElementById(`iv1_${i}`).value),
                    iv2: parseInt(document.getElementById(`iv2_${i}`).value),
                    ov1: parseInt(document.getElementById(`ov1_${i}`).value),
                    ov2: parseInt(document.getElementById(`ov2_${i}`).value)
                });
            }

            fetch('/api/save_config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            })
            .then(response => response.json())
            .then(res => {
                if(res.status === 'ok') alert('Configurações salvas com sucesso!');
                else alert('Erro ao salvar.');
            }).catch(err => alert('Erro na rede.'));
        }
    </script>
</body>
</html>
)rawliteral";

/*
=====================================================
SISTEMA DE MENSAGENS E CURVAS
=====================================================
*/
void setOLEDMessage(const char *type, byte channel, int d1, int d2) {
  strncpy(midiType, type, sizeof(midiType) - 1);
  midiType[sizeof(midiType) - 1] = '\0';
  midiChannel = channel;
  midiData1 = d1;
  midiData2 = d2;
  refreshNeeded = true;
}

byte applyCurve(byte value, byte type) {
  float x = value / 127.0f;
  float y = x;
  switch (type) {
    case CURVE_LOG: y = powf(x, 2.0f); break;
    case CURVE_ALG: y = powf(x, 0.5f); break;
    case CURVE_SCV: y = (1.0f - cosf(x * PI)) / 2.0f; break;
  }
  y = constrain(y, 0.0f, 1.0f);
  return (byte)(y * 127.0f);
}

/*
=====================================================
TELAS DE DESENHO (DRAW FUNCTIONS)
=====================================================
*/
void drawMonitorScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ULTIMATE MIDI HOST");
  display.setCursor(0, 10);
  display.print("MODE: ");
  display.println(currentMode == MODE_MONITOR ? "MONITOR" : "REMAPPING");
  display.drawLine(0, 20, 127, 20, SSD1306_WHITE);

  // Se houve remapeamento ativo detectado, isola as variáveis para não clonar dados brutos
  if (currentMode == MODE_REMAPPING && isRemapped) {
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("ORIGI CC:");
    display.print(remapInCC);
    display.print(" Val:");
    display.print(remapInVal);

    display.setCursor(0, 38);
    display.print("REMAP CC:");
    display.print(remapOutCC);
    display.print(" Val:");
    display.print(remapOutVal);

    display.setCursor(0, 54);
    display.print("CH: ");
    display.print(midiChannel);
  } else {
    // Modo comum ou pass-thru direto
    display.setTextSize(2);
    display.setCursor(0, 28);
    display.println(midiType);
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("CH:");
    display.print(midiChannel);
    display.setCursor(40, 56);
    display.print("D1:");
    display.print(midiData1);
    display.setCursor(84, 56);
    display.print("D2:");
    display.print(midiData2);
  }

  display.display();
}

void drawMenuScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("CONFIG MENU");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  if (selectedItem < menuOffset) menuOffset = selectedItem;
  if (selectedItem >= menuOffset + 5) menuOffset = selectedItem - 4;
  for (int i = 0; i < 5; i++) {
    int item = menuOffset + i;
    if (item >= TOTAL_MENU_ITEMS) break;
    int y = 14 + (i * 10);
    if (item == selectedItem) {
      display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    if (item < 10) {
      MidiRemap &slot = remapSlots[item];
      display.print("B");
      display.print(item + 1);
      display.print(slot.enabled ? " ON " : " OFF ");
      display.print(slot.inputCC);
      display.print(">");
      display.print(slot.outputCC);
    } else if (item == 10) {
      display.print("EXP PALS (WEB CTRL)");
    } else {
      display.print("EXIT");
    }
  }
  display.display();
}

void drawEditScreen() {
  display.clearDisplay();
  MidiRemap &slot = remapSlots[selectedItem];
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("EDIT B");
  display.println(selectedItem + 1);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  const int visibleLines = 5;
  int scrollOffset = (currentField >= visibleLines) ? currentField - visibleLines + 1 : 0;
  for (int i = 0; i < visibleLines; i++) {
    int field = scrollOffset + i;
    if (field > EDIT_OUT_VALUE2) break;
    int y = 14 + (i * 10);
    if (field == currentField) {
      display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    switch (field) {
      case EDIT_ENABLE:
        display.print("ENABLE:");
        display.print(slot.enabled ? "ON" : "OFF");
        break;
      case EDIT_IN_CH:
        display.print("IN CH:");
        display.print(slot.inputChannel);
        break;
      case EDIT_OUT_CH:
        display.print("OUT CH:");
        display.print(slot.outputChannel);
        break;
      case EDIT_IN_CC:
        display.print("IN CC:");
        display.print(slot.inputCC);
        break;
      case EDIT_OUT_CC:
        display.print("OUT CC:");
        display.print(slot.outputCC);
        break;
      case EDIT_IN_VALUE1:
        display.print("IN V1:");
        display.print(slot.inputValue1);
        break;
      case EDIT_OUT_VALUE1:
        display.print("OUT V1:");
        display.print(slot.outputValue1);
        break;
      case EDIT_IN_VALUE2:
        display.print("IN V2:");
        display.print(slot.inputValue2);
        break;
      case EDIT_OUT_VALUE2:
        display.print("OUT V2:");
        display.print(slot.outputValue2);
        break;
    }
  }
  display.display();
}

void drawExpScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("EXP PEDALS");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  for (int i = 0; i < 6; i++) {
    int y = 14 + (i * 8);
    if (i == currentExpField) {
      display.fillRect(0, y - 1, 128, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, y);
    switch (i) {
      case EXP1_IN:
        display.print("EXP1 IN:");
        display.print(exp1InputCC);
        break;
      case EXP1_OUT:
        display.print("EXP1 OUT:");
        display.print(exp1OutputCC);
        break;
      case EXP1_CURVE:
        display.print("EXP1 CRV:");
        display.print(curveNames[exp1Curve]);
        break;
      case EXP2_IN:
        display.print("EXP2 IN:");
        display.print(exp2InputCC);
        break;
      case EXP2_OUT:
        display.print("EXP2 OUT:");
        display.print(exp2OutputCC);
        break;
      case EXP2_CURVE:
        display.print("EXP2 CRV:");
        display.print(curveNames[exp2Curve]);
        break;
    }
  }
  display.display();
}

void updateOLED() {
  switch (currentScreen) {
    case SCREEN_MONITOR: drawMonitorScreen(); break;
    case SCREEN_MENU: drawMenuScreen(); break;
    case SCREEN_EDIT: drawEditScreen(); break;
    case SCREEN_EXP_EDIT: drawExpScreen(); break;
  }
  refreshNeeded = false;
}

/*
=====================================================
PERSISTENCE (LOAD/SAVE)
=====================================================
*/
void saveRemapSlots() {
  remapStorage.signature = REMAP_SIGNATURE;
  remapStorage.version = REMAP_VERSION;
  remapStorage.operationMode = currentMode;
  remapStorage.exp1InputCC = exp1InputCC;
  remapStorage.exp1OutputCC = exp1OutputCC;
  remapStorage.exp1Curve = exp1Curve;
  remapStorage.exp2InputCC = exp2InputCC;
  remapStorage.exp2OutputCC = exp2OutputCC;
  remapStorage.exp2Curve = exp2Curve;
  for (int i = 0; i < 10; i++) remapStorage.slots[i] = remapSlots[i];

  prefs.begin("midihost", false);
  prefs.putBytes("config", &remapStorage, sizeof(remapStorage));
  prefs.end();
  pendingSave = false;
  DBGLN("[STORAGE] CONFIG SAVED TO FLASH SUCCESSFULLY");
}

bool loadRemapSlots() {
  DBGLN("[STORAGE] Carregando slots de memoria...");
  prefs.begin("midihost", true);
  size_t len = prefs.getBytesLength("config");
  if (len != sizeof(remapStorage)) {
    prefs.end();
    return false;
  }
  prefs.getBytes("config", &remapStorage, sizeof(remapStorage));
  prefs.end();

  if (remapStorage.signature != REMAP_SIGNATURE || remapStorage.version != REMAP_VERSION) {
    return false;
  }

  currentMode = remapStorage.operationMode;
  exp1InputCC = remapStorage.exp1InputCC;
  exp1OutputCC = remapStorage.exp1OutputCC;
  exp1Curve = constrain(remapStorage.exp1Curve, 0, 3);
  exp2InputCC = remapStorage.exp2InputCC;
  exp2OutputCC = remapStorage.exp2OutputCC;
  exp2Curve = constrain(remapStorage.exp2Curve, 0, 3);
  for (int i = 0; i < 10; i++) remapSlots[i] = remapStorage.slots[i];

  return true;
}

/*
=====================================================
MIDI CORE PROCESSING ENGINE
=====================================================
*/
void processControlChange(byte channel, byte cc, byte value) {
  if (currentMode == MODE_REMAPPING) {
    bool mapped = false;
    // 1. ANÁLISE PRIORITÁRIA: Pedais de Expressão Globais Fixos
    if (cc == exp1InputCC || cc == exp2InputCC) {
      byte outCC = (cc == exp1InputCC) ? exp1OutputCC : exp2OutputCC;
      byte outCurve = (cc == exp1InputCC) ? exp1Curve : exp2Curve;
      byte mappedValue = applyCurve(value, outCurve);
      MIDI_DIN.sendControlChange(outCC, mappedValue, channel);

      isRemapped = true;
      midiChannel = channel;
      remapInCC = cc;
      remapInVal = value;
      remapOutCC = outCC;
      remapOutVal = mappedValue;
      refreshNeeded = true;

      String logRemap = "[MIDI IN CC REMAP] PEDAIS EXP -> Ch: " + String(channel) + " | CC: " + String(outCC) + " | Val: " + String(mappedValue);
      DBGLN(logRemap);
      return;
    }

    // 2. VARREDURA DOS SLOTS DE REMAPEAMENTO (B1 a B10)
    for (int i = 0; i < 10; i++) {
      MidiRemap &slot = remapSlots[i];
      if (!slot.enabled || channel != slot.inputChannel || cc != slot.inputCC) continue;
      // Se for botão (Ranges de entrada iguais na Web, ex: 127 e 127)
      if (slot.inputValue1 == slot.inputValue2) {
        if (value != slot.inputValue1) continue;
      } else {
        if (value < slot.inputValue1 || value > slot.inputValue2) continue;
      }

      int mappedValue;
      if (slot.inputValue1 == slot.inputValue2) {
        mappedValue = slot.outputValue2;
        // Assume diretamente a saída do botão fixo
      } else {
        mappedValue = map(value, slot.inputValue1, slot.inputValue2, slot.outputValue1, slot.outputValue2);
      }
      mappedValue = constrain(mappedValue, 0, 127);

      MIDI_DIN.sendControlChange(slot.outputCC, mappedValue, slot.outputChannel);
      // Trava os dados remapeados nas variáveis isoladas do OLED
      isRemapped = true;
      midiChannel = channel;
      remapInCC = cc;
      remapInVal = value;
      remapOutCC = slot.outputCC;
      remapOutVal = mappedValue;
      refreshNeeded = true;
      mapped = true;

      String logRemap = "[MIDI IN CC REMAP] SAIDA DIN -> Ch: " + String(channel) + " | CC: " + String(slot.outputCC) + " | Val: " + String(mappedValue);
      DBGLN(logRemap);
      break;
    }

    // Se passou pelas regras e não se enquadrou em nenhum remapeamento
    if (!mapped) {
      isRemapped = false;
      setOLEDMessage("CC IN", channel, cc, value);
      MIDI_DIN.sendControlChange(cc, value, channel);
    }
  } else {
    // Modo Monitor Puro (Pass-Thru)
    isRemapped = false;
    setOLEDMessage("CC IN", channel, cc, value);
    MIDI_DIN.sendControlChange(cc, value, channel);
  }
}

void processNoteMessage(bool isNoteOn, byte channel, byte note, byte velocity) {
  isRemapped = false;
  // Desativa o layout de remapeamento no OLED para mensagens Note
  if (isNoteOn) {
    setOLEDMessage("NOTE ON", channel, note, velocity);
    MIDI_DIN.sendNoteOn(note, velocity, channel);
  } else {
    setOLEDMessage("NOTE OFF", channel, note, velocity);
    MIDI_DIN.sendNoteOff(note, velocity, channel);
  }
}

void handleControlChange(byte channel, byte cc, byte value) {
  if (cc == 0 || cc == 32) return;
  // Filtra ruídos Bank Select comuns de pedaleiras
  DBG("[MIDI IN CC BRUTO] Entrou via DIN -> Ch: ");
  DBG(channel);
  DBG(" | CC: ");
  DBG(cc);
  DBG(" | Val: ");
  DBGLN(value);
  processControlChange(channel, cc, value);
}

// =====================================================
// CORREÇÃO: PROG CHANGE ATUALIZA O DISPLAY CORRETAMENTE
// =====================================================
void handleProgramChange(byte channel, byte program) {
  isRemapped = false; // Desativa visual do remap CC
  setOLEDMessage("P. CHANGE", channel, program, 0); // Modifica strings e seta 'refreshNeeded' para true
  MIDI_DIN.sendProgramChange(program, channel); // Propaga a mensagem física
  
  // Força atualização imediata caso esteja travado no loop de refresh
  if (currentScreen == SCREEN_MONITOR) {
    drawMonitorScreen();
  }
}

void handleNoteOn(byte channel, byte note, byte velocity) {
  processNoteMessage(true, channel, note, velocity);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  processNoteMessage(false, channel, note, velocity);
}

void processUSBHostMIDI() {
  midiHandler.task();
  if (!midiHandler.getQueue().empty()) {
    for (const auto &ev : midiHandler.getQueue()) {
      if (ev.statusCode == 0xF8) continue;
      // Descarta relógio MIDI de sincronismo (Clock)

      uint8_t tipoMsg = ev.statusCode & 0xF0;
      uint8_t canal = (ev.statusCode & 0x0F) + 1;

      switch (tipoMsg) {
        case 0x90:
          if (ev.velocity7 > 0) processNoteMessage(true, canal, ev.noteNumber, ev.velocity7);
          else processNoteMessage(false, canal, ev.noteNumber, 0);
          break;
        case 0x80:
          processNoteMessage(false, canal, ev.noteNumber, ev.velocity7);
          break;
        case 0xB0:
          if (ev.noteNumber == 0 || ev.noteNumber == 32) continue;
          DBG("[MIDI IN CC BRUTO] Entrou via USB Host -> Ch: ");
          DBG(canal);
          DBG(" | CC: ");
          DBG(ev.noteNumber);
          DBG(" | Val: ");
          DBGLN(ev.velocity7);
          processControlChange(canal, ev.noteNumber, ev.velocity7);
          break;
        case 0xC0:
          isRemapped = false;
          setOLEDMessage("P. CHANGE", canal, ev.noteNumber, 0);
          MIDI_DIN.sendProgramChange(ev.noteNumber, canal);
          break;
      }
    }
    midiHandler.clearQueue();
  }
}

/*
=====================================================
INPUTS (ENCODER & BUTTON)
=====================================================
*/
void triggerEdit() {
  lastEditTime = millis();
  pendingSave = true;
  refreshNeeded = true;
}

void processEncoderRotation() {
  long value = menuEncoder.getCount();
  long pos = value / 2;
  if (pos == lastEncoderValue) return;
  int dir = (pos > lastEncoderValue) ? 1 : -1;
  lastEncoderValue = pos;
  if (currentScreen == SCREEN_MONITOR) {
    currentMode = (dir > 0) ? MODE_REMAPPING : MODE_MONITOR;
    triggerEdit();
  } else if (currentScreen == SCREEN_MENU) {
    selectedItem = constrain(selectedItem + dir, 0, TOTAL_MENU_ITEMS - 1);
    refreshNeeded = true;
  } else if (currentScreen == SCREEN_EDIT) {
    MidiRemap &slot = remapSlots[selectedItem];
    switch (currentField) {
      case EDIT_ENABLE:
        if (dir != 0) slot.enabled = !slot.enabled;
        break;
      case EDIT_IN_CH: slot.inputChannel = constrain(slot.inputChannel + dir, 1, 16); break;
      case EDIT_OUT_CH: slot.outputChannel = constrain(slot.outputChannel + dir, 1, 16);
        break;
      case EDIT_IN_CC: slot.inputCC = constrain(slot.inputCC + dir, 0, 127); break;
      case EDIT_OUT_CC: slot.outputCC = constrain(slot.outputCC + dir, 0, 127);
        break;
      case EDIT_IN_VALUE1: slot.inputValue1 = constrain(slot.inputValue1 + dir, 0, 127); break;
      case EDIT_OUT_VALUE1: slot.outputValue1 = constrain(slot.outputValue1 + dir, 0, 127);
        break;
      case EDIT_IN_VALUE2: slot.inputValue2 = constrain(slot.inputValue2 + dir, 0, 127); break;
      case EDIT_OUT_VALUE2: slot.outputValue2 = constrain(slot.outputValue2 + dir, 0, 127);
        break;
    }
    triggerEdit();
  } else if (currentScreen == SCREEN_EXP_EDIT) {
    switch (currentExpField) {
      case EXP1_IN: exp1InputCC = constrain(exp1InputCC + dir, 0, 127);
        break;
      case EXP1_OUT: exp1OutputCC = constrain(exp1OutputCC + dir, 0, 127); break;
      case EXP1_CURVE: exp1Curve = constrain(exp1Curve + dir, 0, 3);
        break;
      case EXP2_IN: exp2InputCC = constrain(exp2InputCC + dir, 0, 127); break;
      case EXP2_OUT: exp2OutputCC = constrain(exp2OutputCC + dir, 0, 127);
        break;
      case EXP2_CURVE: exp2Curve = constrain(exp2Curve + dir, 0, 3); break;
    }
    triggerEdit();
  }
}

void processButton() {
  static bool buttonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  bool reading = digitalRead(ENCODER_BUTTON);
  if (reading != buttonState && (millis() - lastDebounceTime) > 50) {
    buttonState = reading;
    lastDebounceTime = millis();
    if (buttonState == LOW) {
      if (currentScreen == SCREEN_MONITOR) currentScreen = SCREEN_MENU;
      else if (currentScreen == SCREEN_MENU) {
        if (selectedItem == 11) {
          currentScreen = SCREEN_MONITOR;
          selectedItem = 0;
        } else if (selectedItem == 10) {
          currentScreen = SCREEN_EXP_EDIT;
          currentExpField = EXP1_IN;
        } else {
          currentScreen = SCREEN_EDIT;
          currentField = EDIT_ENABLE;
        }
      } else if (currentScreen == SCREEN_EDIT) {
        currentField = (EditField)((int)currentField + 1);
        if (currentField > EDIT_OUT_VALUE2) {
          currentField = EDIT_ENABLE;
          currentScreen = SCREEN_MENU;
        }
      } else if (currentScreen == SCREEN_EXP_EDIT) {
        currentExpField = (ExpField)((int)currentExpField + 1);
        if (currentExpField > EXP2_CURVE) {
          currentExpField = EXP1_IN;
          currentScreen = SCREEN_MENU;
        }
      }
      refreshNeeded = true;
    }
  }
}

/*
 =====================================================
 CONFIGURAÇÃO DO SERVIDOR WEB (VERSÃO SÍNCRONA ESTÁVEL)
 =====================================================
 */
void setupWebInterface() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("Ultimate_MIDI_Config", "12345678", 1, 0, 4);
  delay(500);
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", index_html);
  });
  server.on("/api/get_config", HTTP_GET, []() {
    JsonDocument doc;
    doc["mode"] = (int)currentMode;
    doc["exp1In"] = exp1InputCC;
    doc["exp1Out"] = exp1OutputCC;
    doc["exp1Crv"] = exp1Curve;
    doc["exp2In"] = exp2InputCC;
    doc["exp2Out"] = exp2OutputCC;
    doc["exp2Crv"] = exp2Curve;

    JsonArray slotsArr = doc["slots"].to<JsonArray>();
    for (int i = 0; i < 10; i++) {
      JsonObject sObj = slotsArr.add<JsonObject>();
      sObj["en"] = remapSlots[i].enabled;
      sObj["ich"] = remapSlots[i].inputChannel;
      sObj["och"] = remapSlots[i].outputChannel;
      sObj["icc"] = remapSlots[i].inputCC;
      sObj["occ"] = remapSlots[i].outputCC;
      sObj["iv1"] = remapSlots[i].inputValue1;
      sObj["iv2"] = remapSlots[i].inputValue2;
      sObj["ov1"] = remapSlots[i].outputValue1;
      sObj["ov2"] = remapSlots[i].outputValue2;
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/save_config", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"status\":\"error\"}");
      return;
    }
    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      server.send(400, "application/json", "{\"status\":\"error\"}");
      return;
    }

    JsonObject root = doc.as<JsonObject>();
    currentMode = (OperationMode)root["mode"].as<int>();
    exp1InputCC = root["exp1In"].as<byte>();
    exp1OutputCC = root["exp1Out"].as<byte>();
    exp1Curve = constrain(root["exp1Crv"].as<byte>(), 0, 3);
    exp2InputCC = root["exp2In"].as<byte>();
    exp2OutputCC = root["exp2Out"].as<byte>();
    exp2Curve = constrain(root["exp2Crv"].as<byte>(), 0, 3);

    JsonArray slotsArr = root["slots"].as<JsonArray>();
    for (int i = 0; i < 10; i++) {
      JsonObject sObj = slotsArr[i];
      remapSlots[i].enabled = sObj["en"].as<bool>();
      remapSlots[i].inputChannel = constrain(sObj["ich"].as<byte>(), 1, 16);
      remapSlots[i].outputChannel = constrain(sObj["och"].as<byte>(), 1, 16);
      remapSlots[i].inputCC = constrain(sObj["icc"].as<byte>(), 0, 127);
      remapSlots[i].outputCC = constrain(sObj["occ"].as<byte>(), 0, 127);
      remapSlots[i].inputValue1 = constrain(sObj["iv1"].as<byte>(), 0, 127);
      remapSlots[i].inputValue2 = constrain(sObj["iv2"].as<byte>(), 0, 127);
      remapSlots[i].outputValue1 = constrain(sObj["ov1"].as<byte>(), 0, 127);
      remapSlots[i].outputValue2 = constrain(sObj["ov2"].as<byte>(), 0, 127);
    }
    lastEditTime = millis();
    pendingSave = true;
    refreshNeeded = true;
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    DBGLN("[WEB] Nova Configuração salva na memoria com sucesso.");
  });

  server.begin();
}

/*
 =====================================================
 SETUP
 =====================================================
 */
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);

  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  menuEncoder.attachHalfQuad(ENCODER_PIN_A, ENCODER_PIN_B);
  menuEncoder.clearCount();

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    while (true) delay(100);
  }

  showSplashScreen();
  display.clearDisplay();
  display.display();

  if (!loadRemapSlots()) {
    for (int i = 0; i < 10; i++) {
      remapSlots[i] = { false, 1, 1, (byte)i, (byte)i, 0, 0, 127, 127 };
    }
    saveRemapSlots();
  }

  setupWebInterface();

  midiHandler.addTransport(&usbHost);
  usbHost.begin();
  midiHandler.begin();

  MIDISerial.begin(31250, SERIAL_8N1, 18, 17);
  MIDI_DIN.begin(MIDI_CHANNEL_OMNI);
  MIDI_DIN.turnThruOff();

  MIDI_DIN.setHandleControlChange(handleControlChange);
  MIDI_DIN.setHandleProgramChange(handleProgramChange);
  MIDI_DIN.setHandleNoteOn(handleNoteOn);
  MIDI_DIN.setHandleNoteOff(handleNoteOff);

  updateOLED();
}

/*
=====================================================
LOOP PRINCIPAL (MAX PERF - ZERO LATENCY)
=====================================================
*/
void loop() {
  MIDI_DIN.read();
  processUSBHostMIDI();
  server.handleClient();

  processEncoderRotation();
  processButton();

  if (pendingSave && (millis() - lastEditTime > SAVE_DELAY)) {
    saveRemapSlots();
  }

  if (refreshNeeded) {
    updateOLED();
  }
}
