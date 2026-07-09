#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>

#define LED_PIN 38
#define MIC_PIN_1 1      // lewy mikrofon GPIO1
#define MIC_PIN_2 2      // prawy mikrofon GPIO2
#define MIC_PIN_3 4      // przedni mikrofon GPIO4 (ADC1_3)
#define MIC_PIN_4 10     // tylny mikrofon GPIO10 (ADC1_9)

// Piny dla L298N
int ENA = 20;
int IN1 = 14;
int IN2 = 13;
int ENB = 21;
int IN3 = 12;
int IN4 = 9;

Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// === KONFIGURACJA WiFi ===
const char* ssid = "ESP32_Microfon";
const char* password = "12345678";

// Ustawienia dla mikrofonow
const int MIN_READING = 50;
const int MAX_READING = 350;
const int SOUND_THRESHOLD = 200;

// Zmienne dla trybu skokowego
enum State { COLLECTING, SHOWING, EXECUTING };
State currentState = COLLECTING;

unsigned long stateStartTime = 0;
const unsigned long COLLECT_DURATION = 5000;  // 5 sekund zbierania
const unsigned long SHOW_DURATION = 2000;     // 2 sekundy wyswietlania
const unsigned long EXECUTE_DURATION = 3000;  // 3 sekundy jazdy (forward/backward)

// Zmienne do przechowywania max wartości
int maxValues[4] = {0, 0, 0, 0};
String lastDirection = "none";
int lastRed = 0, lastGreen = 0, lastBlue = 0;

// Zmienne dla timera auta
unsigned long moveEndTime = 0;
bool isMoving = false;
bool waitingForForward = false;

// ===== FUNKCJE STEROWANIA AUTEM =====
void stopMotors()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  isMoving = false;
  waitingForForward = false;
}

void forward()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  moveEndTime = millis() + EXECUTE_DURATION;
  isMoving = true;
  waitingForForward = false;
  Serial.println("🚗 CAR: GOING FORWARD (3s)");
}

void backward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  moveEndTime = millis() + EXECUTE_DURATION;
  isMoving = true;
  waitingForForward = false;
  Serial.println("🚗 CAR: GOING BACKWARD (3s)");
}

void left()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  moveEndTime = millis() + 600;
  isMoving = true;
  waitingForForward = true;
  Serial.println("🚗 CAR: TURNING LEFT AND GOING FORWARD");
}

void right()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  moveEndTime = millis() + 600;
  isMoving = true;
  waitingForForward = true;
  Serial.println("🚗 CAR: TURNING RIGHT AND GOING FORWARD");
}

void executeDirection(String direction)
{
  if (direction == "up") {
    forward();
  } else if (direction == "down") {
    backward();
  } else if (direction == "left") {
    left();
  } else if (direction == "right") {
    right();
  }
}

// ===== FUNKCJE MIKROFONOW =====
String getDirectionFromMax() {
  if (maxValues[0] > SOUND_THRESHOLD &&
      maxValues[0] > maxValues[1] &&
      maxValues[0] > maxValues[2] &&
      maxValues[0] > maxValues[3]) {
    return "left";
  }
  else if (maxValues[1] > SOUND_THRESHOLD &&
           maxValues[1] > maxValues[0] &&
           maxValues[1] > maxValues[2] &&
           maxValues[1] > maxValues[3]) {
    return "right";
  }
  else if (maxValues[2] > SOUND_THRESHOLD &&
           maxValues[2] > maxValues[0] &&
           maxValues[2] > maxValues[1] &&
           maxValues[2] > maxValues[3]) {
    return "up";
  }
  else if (maxValues[3] > SOUND_THRESHOLD &&
           maxValues[3] > maxValues[0] &&
           maxValues[3] > maxValues[1] &&
           maxValues[3] > maxValues[2]) {
    return "down";
  }
  return "none";
}

void resetMaxValues() {
  for (int i = 0; i < 4; i++) {
    maxValues[i] = 0;
  }
}

// Strona HTML (ta sama co wczesniej)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 - Four Microphone System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial;
            text-align: center;
            margin: 0;
            padding: 0;
            background: #1a1a2e;
            color: white;
            overflow: hidden;
            height: 100vh;
        }
        .container {
            max-width: 1400px;
            margin: auto;
            height: 100%;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
        }

        .top-bar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 15px;
            margin-bottom: 0;
            padding: 0 20px;
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            z-index: 200;
        }

        h1 {
            margin: 0;
            margin-left: 20px;
            font-size: 22px;
            flex: 1;
            text-align: left;
        }

        .right-panel {
            display: flex;
            align-items: center;
            gap: 20px;
            background: #16213e;
            padding: 8px 20px;
            border-radius: 15px;
        }

        .led {
            width: 40px;
            height: 40px;
            border-radius: 20px;
            background: #00ff88;
            transition: all 0.1s;
            box-shadow: 0 0 10px rgba(0,255,136,0.3);
        }

        .direction-box {
            font-size: 16px;
            font-weight: bold;
            background: #0f0f1a;
            padding: 8px 15px;
            border-radius: 10px;
            min-width: 150px;
        }

        .state-box {
            font-size: 12px;
            background: #0f0f1a;
            padding: 5px 10px;
            border-radius: 10px;
            margin-top: 5px;
        }

        .arrow-left {
            position: fixed;
            left: 20px;
            top: 50%;
            transform: translateY(-50%);
            font-size: 70px;
            background: #0f0f1a;
            padding: 20px;
            border-radius: 20px;
            transition: all 0.2s;
            opacity: 0.3;
            z-index: 100;
        }

        .arrow-up {
            position: fixed;
            left: 50%;
            top: 20px;
            transform: translateX(-50%);
            font-size: 70px;
            background: #0f0f1a;
            padding: 20px;
            border-radius: 20px;
            transition: all 0.2s;
            opacity: 0.3;
            z-index: 100;
        }

        .arrow-right {
            position: fixed;
            right: 20px;
            top: 50%;
            transform: translateY(-50%);
            font-size: 70px;
            background: #0f0f1a;
            padding: 20px;
            border-radius: 20px;
            transition: all 0.2s;
            opacity: 0.3;
            z-index: 100;
        }

        .arrow-down {
            position: fixed;
            left: 50%;
            bottom: 20px;
            transform: translateX(-50%);
            font-size: 70px;
            background: #0f0f1a;
            padding: 20px;
            border-radius: 20px;
            transition: all 0.2s;
            opacity: 0.3;
            z-index: 100;
        }

        .arrow-active {
            background: #2a2a4e;
            opacity: 1;
            box-shadow: 0 0 20px rgba(0,255,136,0.3);
        }

        .arrow-left.arrow-active { transform: translateY(-50%) scale(1.1); }
        .arrow-up.arrow-active { transform: translateX(-50%) scale(1.1); }
        .arrow-right.arrow-active { transform: translateY(-50%) scale(1.1); }
        .arrow-down.arrow-active { transform: translateX(-50%) scale(1.1); }

        .mics-layout {
            display: flex;
            gap: 20px;
            justify-content: center;
            align-items: center;
            width: 80%;
            margin: -10px auto 0 auto;
            padding: 0 100px;
        }

        .col-left, .col-right, .col-center {
            flex: 1;
            display: flex;
        }

        .col-left { justify-content: flex-end; }
        .col-right { justify-content: flex-start; }
        .col-center { flex-direction: column; gap: 20px; }

        .mic-card {
            background: #16213e;
            border-radius: 10px;
            padding: 12px;
            transition: all 0.2s;
            width: 100%;
            display: flex;
            flex-direction: column;
        }

        .mic-card.active-left { box-shadow: 0 0 20px rgba(255,0,0,0.5); }
        .mic-card.active-up { box-shadow: 0 0 20px rgba(0,255,0,0.5); }
        .mic-card.active-right { box-shadow: 0 0 20px rgba(0,0,255,0.5); }
        .mic-card.active-down { box-shadow: 0 0 20px rgba(255,255,0,0.5); }

        canvas {
            background: #0f0f1a;
            border-radius: 8px;
            margin: 8px 0;
            width: 100%;
            height: 140px;
        }
        .value { font-size: 32px; font-weight: bold; margin: 5px; }
        .label { font-size: 14px; margin: 5px; }

        .mic-title-left { font-size: 18px; margin: 5px; color: #ff6666; }
        .mic-title-up { font-size: 18px; margin: 5px; color: #66ff66; }
        .mic-title-right { font-size: 18px; margin: 5px; color: #6666ff; }
        .mic-title-down { font-size: 18px; margin: 5px; color: #ffff66; }

        .value-left { color: #ff6666; font-size: 32px; }
        .value-up { color: #66ff66; font-size: 32px; }
        .value-right { color: #6666ff; font-size: 32px; }
        .value-down { color: #ffff66; font-size: 32px; }

        @media (max-width: 768px) {
            .arrow-left, .arrow-right, .arrow-up, .arrow-down { font-size: 45px; padding: 12px; }
            .mics-layout { gap: 10px; padding: 0 60px; width: 90%; margin: -40px auto 0 auto; }
            canvas { height: 120px; }
            .value { font-size: 24px; }
            .mic-title-left, .mic-title-up, .mic-title-right, .mic-title-down { font-size: 14px; }
            h1 { font-size: 18px; margin-left: 10px; }
            .right-panel { gap: 10px; padding: 5px 15px; }
            .led { width: 30px; height: 30px; }
            .direction-box { font-size: 12px; min-width: 120px; }
        }
    </style>
</head>
<body>
    <div class="arrow-left" id="arrowLeft">⬅️</div>
    <div class="arrow-up" id="arrowUp">⬆️</div>
    <div class="arrow-right" id="arrowRight">➡️</div>
    <div class="arrow-down" id="arrowDown">⬇️</div>

    <div class="container">
        <div class="top-bar">
            <h1>🎤 Four Microphone System 🎤</h1>
            <div class="right-panel">
                <div class="led" id="led"></div>
                <div class="direction-box" id="directionLabel">🎯 Direction: ---</div>
            </div>
        </div>

        <div class="mics-layout">
            <div class="col-left">
                <div class="mic-card" id="cardLeft">
                    <div class="mic-title-left">🎙️ LEFT MIC (GPIO1)</div>
                    <div class="label">Current value:</div>
                    <div class="value value-left" id="value1">0</div>
                    <canvas id="canvas1"></canvas>
                    <div class="label" id="status1">🔇 Silent</div>
                </div>
            </div>

            <div class="col-center">
                <div class="mic-card" id="cardUp">
                    <div class="mic-title-up">⬆️ FRONT MIC (GPIO4)</div>
                    <div class="label">Current value:</div>
                    <div class="value value-up" id="value3">0</div>
                    <canvas id="canvas3"></canvas>
                    <div class="label" id="status3">🔇 Silent</div>
                </div>
                <div class="mic-card" id="cardDown">
                    <div class="mic-title-down">⬇️ BACK MIC (GPIO10)</div>
                    <div class="label">Current value:</div>
                    <div class="value value-down" id="value4">0</div>
                    <canvas id="canvas4"></canvas>
                    <div class="label" id="status4">🔇 Silent</div>
                </div>
            </div>

            <div class="col-right">
                <div class="mic-card" id="cardRight">
                    <div class="mic-title-right">🎤 RIGHT MIC (GPIO2)</div>
                    <div class="label">Current value:</div>
                    <div class="value value-right" id="value2">0</div>
                    <canvas id="canvas2"></canvas>
                    <div class="label" id="status2">🔇 Silent</div>
                </div>
            </div>
        </div>
        <div class="state-box" id="stateBox">Status: ---</div>
    </div>

    <script>
        const canvas1 = document.getElementById('canvas1');
        const ctx1 = canvas1.getContext('2d');
        let values1 = new Array(100).fill(0);

        const canvas2 = document.getElementById('canvas2');
        const ctx2 = canvas2.getContext('2d');
        let values2 = new Array(100).fill(0);

        const canvas3 = document.getElementById('canvas3');
        const ctx3 = canvas3.getContext('2d');
        let values3 = new Array(100).fill(0);

        const canvas4 = document.getElementById('canvas4');
        const ctx4 = canvas4.getContext('2d');
        let values4 = new Array(100).fill(0);

        const arrowLeft = document.getElementById('arrowLeft');
        const arrowUp = document.getElementById('arrowUp');
        const arrowRight = document.getElementById('arrowRight');
        const arrowDown = document.getElementById('arrowDown');

        const cardLeft = document.getElementById('cardLeft');
        const cardUp = document.getElementById('cardUp');
        const cardRight = document.getElementById('cardRight');
        const cardDown = document.getElementById('cardDown');

        function resizeCanvases() {
            canvas1.width = canvas1.clientWidth;
            canvas1.height = canvas1.clientHeight;
            canvas2.width = canvas2.clientWidth;
            canvas2.height = canvas2.clientHeight;
            canvas3.width = canvas3.clientWidth;
            canvas3.height = canvas3.clientHeight;
            canvas4.width = canvas4.clientWidth;
            canvas4.height = canvas4.clientHeight;
        }
        window.addEventListener('resize', resizeCanvases);
        resizeCanvases();

        function drawGraph(canvas, ctx, values, color) {
            if(!canvas.width || !canvas.height) return;
            ctx.fillStyle = '#0f0f1a';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            if(values.length < 2) return;
            const step = canvas.width / (values.length - 1);
            const maxVal = 4095;
            ctx.beginPath();
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            for(let i = 0; i < values.length; i++) {
                const x = i * step;
                const y = canvas.height - (values[i] / maxVal * canvas.height);
                if(i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
        }

        function updateData() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('value1').innerText = data.raw1;
                    values1.push(data.raw1);
                    if(values1.length > 100) values1.shift();
                    drawGraph(canvas1, ctx1, values1, '#ff6666');

                    document.getElementById('value2').innerText = data.raw2;
                    values2.push(data.raw2);
                    if(values2.length > 100) values2.shift();
                    drawGraph(canvas2, ctx2, values2, '#6666ff');

                    document.getElementById('value3').innerText = data.raw3;
                    values3.push(data.raw3);
                    if(values3.length > 100) values3.shift();
                    drawGraph(canvas3, ctx3, values3, '#66ff66');

                    document.getElementById('value4').innerText = data.raw4;
                    values4.push(data.raw4);
                    if(values4.length > 100) values4.shift();
                    drawGraph(canvas4, ctx4, values4, '#ffff66');

                    const status1 = data.raw1 > 200 ? "🔊 SOUND!" : "🔇 Silent";
                    const status2 = data.raw2 > 200 ? "🔊 SOUND!" : "🔇 Silent";
                    const status3 = data.raw3 > 200 ? "🔊 SOUND!" : "🔇 Silent";
                    const status4 = data.raw4 > 200 ? "🔊 SOUND!" : "🔇 Silent";
                    document.getElementById('status1').innerText = status1;
                    document.getElementById('status2').innerText = status2;
                    document.getElementById('status3').innerText = status3;
                    document.getElementById('status4').innerText = status4;

                    document.getElementById('stateBox').innerHTML = `📡 ${data.state}`;

                    arrowLeft.classList.remove('arrow-active');
                    arrowUp.classList.remove('arrow-active');
                    arrowRight.classList.remove('arrow-active');
                    arrowDown.classList.remove('arrow-active');
                    cardLeft.classList.remove('active-left', 'active-up', 'active-right', 'active-down');
                    cardUp.classList.remove('active-left', 'active-up', 'active-right', 'active-down');
                    cardRight.classList.remove('active-left', 'active-up', 'active-right', 'active-down');
                    cardDown.classList.remove('active-left', 'active-up', 'active-right', 'active-down');

                    let directionText = "---";
                    if (data.direction === "left") {
                        arrowLeft.classList.add('arrow-active');
                        cardLeft.classList.add('active-left');
                        directionText = "⬅️ LEFT";
                    } else if (data.direction === "right") {
                        arrowRight.classList.add('arrow-active');
                        cardRight.classList.add('active-right');
                        directionText = "➡️ RIGHT";
                    } else if (data.direction === "up") {
                        arrowUp.classList.add('arrow-active');
                        cardUp.classList.add('active-up');
                        directionText = "⬆️ FRONT";
                    } else if (data.direction === "down") {
                        arrowDown.classList.add('arrow-active');
                        cardDown.classList.add('active-down');
                        directionText = "⬇️ BACK";
                    }

                    document.getElementById('directionLabel').innerHTML = `🎯 ${directionText}`;
                    document.getElementById('led').style.backgroundColor = data.ledColor;
                });
        }

        setInterval(updateData, 100);
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  // inicjalizacja LED RGB
  strip.begin();
  strip.setBrightness(50);
  strip.show();

  // inicjalizacja pinow silnikow
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  digitalWrite(ENA, HIGH);
  digitalWrite(ENB, HIGH);
  stopMotors();

  // uruchom WiFi
  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/data", []() {
    int micValue_1 = analogRead(MIC_PIN_1);
    int micValue_2 = analogRead(MIC_PIN_2);
    int micValue_3 = analogRead(MIC_PIN_3);
    int micValue_4 = analogRead(MIC_PIN_4);

    String currentStateStr;
    String directionToShow = "none";
    String ledColorToShow = "rgb(0,0,0)";

    if (currentState == COLLECTING) {
      currentStateStr = "🎙️ COLLECTING DATA... (" + String((COLLECT_DURATION - (millis() - stateStartTime)) / 1000) + "s)";

      if (micValue_1 > maxValues[0]) maxValues[0] = micValue_1;
      if (micValue_2 > maxValues[1]) maxValues[1] = micValue_2;
      if (micValue_3 > maxValues[2]) maxValues[2] = micValue_3;
      if (micValue_4 > maxValues[3]) maxValues[3] = micValue_4;

      directionToShow = "none";
      ledColorToShow = "rgb(0,0,0)";
      strip.setPixelColor(0, strip.Color(20, 20, 20));
      strip.show();

    } else if (currentState == SHOWING) {
      currentStateStr = "✨ RESULT: " + String((SHOW_DURATION - (millis() - stateStartTime)) / 1000 + 1) + "s";
      directionToShow = lastDirection;

      if (lastDirection == "left") {
        ledColorToShow = "rgb(" + String(lastRed) + ", 0, 0)";
        strip.setPixelColor(0, strip.Color(lastRed, 0, 0));
      } else if (lastDirection == "right") {
        ledColorToShow = "rgb(0, 0, " + String(lastBlue) + ")";
        strip.setPixelColor(0, strip.Color(0, 0, lastBlue));
      } else if (lastDirection == "up") {
        ledColorToShow = "rgb(0, " + String(lastGreen) + ", 0)";
        strip.setPixelColor(0, strip.Color(0, lastGreen, 0));
      } else if (lastDirection == "down") {
        ledColorToShow = "rgb(255, 255, 0)";
        strip.setPixelColor(0, strip.Color(255, 255, 0));
      } else {
        ledColorToShow = "rgb(0,0,0)";
        strip.setPixelColor(0, strip.Color(0, 0, 0));
      }
      strip.show();

    } else { // EXECUTING
      currentStateStr = "🚗 AUTO EXECUTING... (" + String((EXECUTE_DURATION - (millis() - stateStartTime)) / 1000 + 1) + "s)";
      directionToShow = lastDirection;

      if (lastDirection == "left") {
        ledColorToShow = "rgb(" + String(lastRed) + ", 0, 0)";
      } else if (lastDirection == "right") {
        ledColorToShow = "rgb(0, 0, " + String(lastBlue) + ")";
      } else if (lastDirection == "up") {
        ledColorToShow = "rgb(0, " + String(lastGreen) + ", 0)";
      } else if (lastDirection == "down") {
        ledColorToShow = "rgb(255, 255, 0)";
      } else {
        ledColorToShow = "rgb(0,0,0)";
      }
    }

    String json = "{";
    json += "\"raw1\":" + String(micValue_1) + ",";
    json += "\"raw2\":" + String(micValue_2) + ",";
    json += "\"raw3\":" + String(micValue_3) + ",";
    json += "\"raw4\":" + String(micValue_4) + ",";
    json += "\"state\":\"" + currentStateStr + "\",";
    json += "\"direction\":\"" + directionToShow + "\",";
    json += "\"ledColor\":\"" + ledColorToShow + "\"";
    json += "}";

    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Server started");
  Serial.println("Connect to WiFi: ESP32_Microfon, password: 12345678");
  Serial.println("Open browser and go to: 192.168.4.1");

  currentState = COLLECTING;
  stateStartTime = millis();
  resetMaxValues();
}

void loop() {
  server.handleClient();

  // obsluga timera auta (jazda)
  if (isMoving && millis() >= moveEndTime) {
    if (waitingForForward) {
      Serial.println("Turn finished, going forward for 3s");
      forward();
    } else {
      stopMotors();
      Serial.println("Car stoped after time");
    }
  }

  // obsluga stanow mikrofonow
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10) {
    lastCheck = millis();
    unsigned long now = millis();

    if (currentState == COLLECTING && (now - stateStartTime) >= COLLECT_DURATION) {
      String direction = getDirectionFromMax();
      lastDirection = direction;

      lastRed = map(maxValues[0], 0, 4095, 0, 255);
      lastGreen = map(maxValues[2], 0, 4095, 0, 255);
      lastBlue = map(maxValues[1], 0, 4095, 0, 255);

      resetMaxValues();

      Serial.print("Result - direction: ");
      Serial.println(lastDirection);

      // AUTOMATYCZNE WYKONANIE KOMENDY
      if (lastDirection != "none") {
        Serial.println("🚗 EXECUTING THE COMMAND AUTOMATICALY!");
        executeDirection(lastDirection);
        currentState = EXECUTING;
      } else {
        currentState = SHOWING;
      }
      stateStartTime = now;

    }
    else if (currentState == SHOWING && (now - stateStartTime) >= SHOW_DURATION) {
      currentState = COLLECTING;
      stateStartTime = now;
      resetMaxValues();
      Serial.println("Start new collection cycle...");
    }
    else if (currentState == EXECUTING && (now - stateStartTime) >= EXECUTE_DURATION) {
      currentState = COLLECTING;
      stateStartTime = now;
      resetMaxValues();
      Serial.println("Execution finished, start new collection cycle...");
    }
  }

  delay(5);
}
