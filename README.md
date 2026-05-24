# Ultimate MIDI Host V1.0 🎛️⚡
> **By Otsedom Tec — Technologies for Musicians**

O **Ultimate MIDI Host V1.0** é um dispositivo de alto desempenho baseado no microcontrolador **ESP32-S3**, projetado para atuar como uma central inteligente de roteamento e remapeamento de mensagens MIDI para músicos. 

Ele combina as capacidades de **USB Host MIDI** (permitindo plugar controladores USB diretamente nele sem precisar de um computador) com conexões tradicionais **MIDI DIN de 5 pinos**, processamento de pedais de expressão analógicos e uma interface Web embarcada para configuração em tempo real.

---

## ✨ Características Principais

* **USB Host MIDI Nativo:** Conecte teclados, pedais e controladores USB diretamente na porta USB do ESP32-S3.
* **Interface MIDI DIN Clássica:** Entrada e Saída física (I/O) via conectores DIN 5 pinos com optoacoplador, operando na velocidade padrão de 31250 bps.
* **Matriz de Remapeamento Inteligente:** 10 Slots de remapeamento configuráveis diretamente para filtrar, transformar e redirecionar canais, números de Control Change (CC) e limitar valores de mínimo e máximo.
* **Dual Expressão:** Suporte para até 2 pedais de expressão analógicos para conversão de potenciômetro para mensagens CC configuráveis.
* **Interface Física Integrada:** Tela OLED (SSD1306) com animação de inicialização personalizada (*Splash Screen*) da **Otsedom Tec** e navegação fluida por Encoder rotativo.
* **Interface Web Wi-Fi:** WebServer síncrono integrado de alta estabilidade para configuração visual rápida de todos os slots e parâmetros salvos diretamente na memória Flash (`Preferences`).

---

## 🛠️ Arquitetura de Hardware

* **Controlador Principal:** ESP32-S3 (Suporta USB Host Nativo).
* **Display:** OLED SSD1306 128x64 I2C.
* **Controle:** Encoder Rotativo com botão de clique integrado (Pull-Up interno).
* **Entrada de Expressão:** Pinos ADC configurados para leitura estável de pedais de expressão.

---

## 💻 Estrutura do Software

O firmware foi projetado em C++ (Arduino IDE / PlatformIO) utilizando uma arquitetura que prioriza a baixa latência no processamento MIDI através da separação de tarefas:

* `esp1.ino`: Arquivo principal contendo as interrupções de hardware, lógica do menu no display OLED e callbacks de recepção MIDI.
* **WebServer Síncrono:** Configurado para rodar de forma leve, garantindo que as requisições HTTP da interface Web não gerem travamentos (*jitter*) nas mensagens de clock e notas MIDI.
* **Persistência de Dados:** Uso da biblioteca `Preferences` para gravação não volátil das configurações com delay de proteção (`SAVE_DELAY = 2000`) para evitar o desgaste prematuro da memória Flash do chip.

---

## 🚀 Como Instalar e Compilar

### Pré-requisitos
Certifique-se de ter as seguintes bibliotecas instaladas na sua IDE:
1. `Adafruit_SSD1306` & `Adafruit_GFX` (Para o Display OLED)
2. `ESP32Encoder` (Para leitura estável do encoder)
3. `MIDI Library` (Para gerenciamento da interface DIN de 5 pinos)
4. `ArduinoJson` (Para serializar/deserializar os dados vindos da Interface Web)
5. `ESP32_Host_MIDI` (Para o ecossistema de USB Host)

### Passos para Compilação:
1. Abra o arquivo `esp1.ino` na Arduino IDE.
2. Selecione a placa correta: **ESP32S3 Dev Module** (Certifique-se de que a opção *USB CDC On Boot* está configurada de acordo com seu hardware).
3. Conecte o ESP32-S3 ao computador.
4. Compile e faça o upload do código.

---

## 🌐 Utilizando a Interface Web

1. Ao ligar o aparelho, o ESP32 iniciará uma rede Wi-Fi própria (ou se conectará à rede configurada).
2. Acesse o endereço IP exibido na tela do monitor serial ou no display usando seu navegador.
3. Na interface gráfica, você poderá ligar/desligar slots individuais usando os botões deslizantes (*Toggle Switches*).
4. Altere os canais de entrada/saída, os números de CC e os ranges de valores conforme sua necessidade.
5. Clique em **"GRAVAR

## 🛠️ Arquitetura de Hardware e Pinagem

Para garantir o funcionamento correto e sem ruídos, todas as conexões devem seguir o mapeamento de pinos do ESP32-S3 detalhado abaixo:

```text
                     +---------------------------------------+
                     |          ESP32-S3 DEV MODULE          |
                     +---------------------------------------+
                        |   |   |   |    |   |    |   |   |
     +------------------+   |   |   |    |   |    |   |   +------------------+
     |                      |   |   |    |   |    |   |                      |
+----+----+                 |   |   |    |   |    |   +----+----+        +---+---+
| 5V  GND |                 |   |   |    |   |    |        | 3.3V|        |  5V   |
+----+----+                 |   |   |    |   |    |        +--+--+        +---+---+
     |                      |   |   |    |   |    |           |               |
[FONTE DE ENERGIA]          |   |   |    |   |    +-----------|-------+       |
(GND comum para todos)      |   |   |    |   |                |       |       |
                            |   |   |    |   +---------+      |       |       |
    +-----------------------+   |   |    |             |      |       |       |
    |                           |   |    |             |      |       |       |
+---+---+                       |   |    |          +--+------+--+    |       |
| PINO  |                       |   |    |          | OLED       |    |       |
| 18 17 |                       |   |    |          | SSD1306    |    |       |
+---+---+                       |   |    |          | SDA=41     |    |       |
    |                           |   |    |          | SCL=42     |    |       |
[CIRCUITO MIDI DIN]             |   |    |          +------------+    |       |
(Optoacoplador e Jacks)         |   |    |                            |       |
                                |   |    +--------------------+       |       |
       +------------------------+   |                         |       |       |
       |                            |                         |       |       |
+------+------+                     |                  +------+------+ |       |
| PINO        |                     |                  | PINO        | |       |
| 11  12  13  |                     |                  | A0     A1   | |       |
+------+------+                     |                  +------+------+ |       |
       |                            |                         |        |       |
[ENCODER ROTATIVO]                  |                  [PEDAL EXPR.1 & 2]      |
(A, B e Botão SW)                   |                  (Potenciômetros)        |
                                    |                                          |
                                    +------------------------------------------+
                                                                               |
                                                                        [PORTA USB HOST]

📋 Lista de Conexões Pino a Pino

    Display OLED (SSD1306 I2C)

        VCC ➡️ 3.3V do ESP32-S3

        GND ➡️ GND comum

        SDA ➡️ GPIO 41

        SCL ➡️ GPIO 42

    Encoder Rotativo

        Pino A (CLK) ➡️ GPIO 11

        Pino B (DT) ➡️ GPIO 12

        Botão (SW) ➡️ GPIO 13 (Utiliza o resistor interno de Pull-Up)

        GND ➡️ GND comum

    Conexões MIDI DIN (5 Pinos)

        TX (Saída MIDI) ➡️ GPIO 17 (Via resistor de 220Ω para o pino 4 do conector)

        RX (Entrada MIDI) ➡️ GPIO 18 (Vindo do pino de saída do Optoacoplador 6N138)

    Pedais de Expressão Analógicos (Potenciômetros de 10kΩ)

        Extremidade 1 (VCC) ➡️ 3.3V do ESP32-S3 (Nunca ligar no 5V!)

        Extremidade 2 (GND) ➡️ GND comum

        Cursor Central (Sinal):

            Pedal 1 ➡️ Entrada Analógica ADC (Ex: GPIO 1)

            Pedal 2 ➡️ Entrada Analógica ADC (Ex: GPIO 2)

    Porta USB Host (Conexão OTG para Controladores)

        USB D- (Fio Branco) ➡️ GPIO 19

        USB D+ (Fio Verde) ➡️ GPIO 20

        USB VBUS (Fio Vermelho) ➡️ Pino de 5V (V5 / Vin) para alimentar os controladores

        GND (Fio Preto) ➡️ GND comum

    ⚠️ IMPORTANTE: Todos os pontos de terra (GND) devem estar estritamente interconectados no mesmo barramento para evitar flutuações de sinal analógico ou loops de terra. Certifique-se de que sua fonte forneça pelo menos 1.5A a 2A para suprir os controladores USB externos conectados.
