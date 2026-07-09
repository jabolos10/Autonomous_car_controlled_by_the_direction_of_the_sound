#include <WiFi.h>
#include <WebServer.h>

// Pins for L298N
int ENA = 20;
int IN1 = 14;
int IN2 = 13;
int ENB = 21;
int IN3 = 12;
int IN4 = 9;

WebServer server(80);

const char* ssid = "ESPcar";
const char* password = "12345678";

// Timer handling variables
unsigned long moveEndTime = 0;
bool isMoving = false;
bool waitingForForward = false;  // flag: are we waiting to drive straight after a turn

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

  if (waitingForForward) {
    // This is driving straight after a turn - 2.4s
    moveEndTime = millis() + 2400;
    waitingForForward = false;
  } else {
    // Normal forward drive - 3s
    moveEndTime = millis() + 3000;
  }
  isMoving = true;
}

void backward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  moveEndTime = millis() + 3000;
  isMoving = true;
  waitingForForward = false;
}

void left()
{
  // First turn left (0.6s)
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  moveEndTime = millis() + 600;
  isMoving = true;
  waitingForForward = true;  // Drive straight after the turn
}

void right()
{
  // First turn right (0.6s)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  moveEndTime = millis() + 600;
  isMoving = true;
  waitingForForward = true;  // Drive straight after the turn
}

void setup()
{
  Serial.begin(115200);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  digitalWrite(ENA, HIGH);
  digitalWrite(ENB, HIGH);

  stopMotors();

  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/forward", forward);
  server.on("/backward", backward);
  server.on("/left", left);
  server.on("/right", right);
  server.on("/stop", stopMotors);

  server.on("/", []()
  {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Car Control</title>
    <style>
        body {
            background: #1a1a2e;
            color: white;
            font-family: Arial;
            text-align: center;
            margin-top: 40px;
        }
        h1 {
            margin-bottom: 30px;
        }
        .controller {
            display: grid;
            grid-template-columns: 120px 120px 120px;
            grid-template-rows: 120px 120px 120px;
            gap: 15px;
            justify-content: center;
        }
        button {
            font-size: 40px;
            border: none;
            border-radius: 20px;
            background: #0f0f1a;
            color: white;
            cursor: pointer;
            transition: all 0.1s;
        }
        button:active {
            background: #00aaFF;
            transform: scale(0.95);
        }
        .forward { grid-column: 2; grid-row: 1; }
        .left    { grid-column: 1; grid-row: 2; }
        .stop    { grid-column: 2; grid-row: 2; background: #aa0000; font-size: 24px; }
        .right   { grid-column: 3; grid-row: 2; }
        .backward{ grid-column: 2; grid-row: 3; }
    </style>
</head>
<body>
    <h1>🤖 ESP32 Car Control</h1>
    <div class="controller">
        <button class="forward" onclick="send('/forward')">⬆️</button>
        <button class="left" onclick="send('/left')">⬅️</button>
        <button class="stop" onclick="send('/stop')">STOP</button>
        <button class="right" onclick="send('/right')">➡️</button>
        <button class="backward" onclick="send('/backward')">⬇️</button>
    </div>
    <script>
        function send(cmd) {
            fetch(cmd);
        }
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop()
{
  server.handleClient();

  // Checking the timer
  if (isMoving && millis() >= moveEndTime) {
    if (waitingForForward) {
      // Turn finished - drive straight
      Serial.println("Turn finished, driving straight for 2.4s");
      forward();  // this forward() call runs with waitingForForward = true
    } else {
      // End of move - stop
      stopMotors();
      Serial.println("Car stopped after timeout");
    }
  }

  delay(10);
}
