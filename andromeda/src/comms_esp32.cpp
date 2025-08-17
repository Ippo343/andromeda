#ifdef ESP32

#include "comms.h"
#include <ESPAsyncWebServer.h>

const int LED_PIN = 2;  // Built-in LED on most ESP32 boards
TaskHandle_t Comms::webServerTaskHandle = nullptr;

Comms::Comms(MissionControl& missionControl) :
  server(80),
  mc(missionControl)
{
}

bool Comms::setup()
{
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off

  // ESP32 supports hostname setting properly
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Andromeda");

  // Configure static IP for ESP32
  IPAddress local_IP(192, 168, 1, 232);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Log.errorln("Failed to configure static IP");
  }

  // Enable auto reconnect
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);  // don’t store creds in flash

  // Attach WiFi event handler
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case SYSTEM_EVENT_STA_DISCONNECTED:
        Log.warningln("WiFi lost connection, attempting reconnect...");
        digitalWrite(LED_PIN, LOW);
        WiFi.reconnect();
        break;
      case SYSTEM_EVENT_STA_GOT_IP:
        Log.noticeln("WiFi reconnected with IP %s", WiFi.localIP().toString().c_str());
        digitalWrite(LED_PIN, HIGH);
        break;
      default:
        break;
    }
  });

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait for connection with 10 second timeout and LED blinking
  unsigned long startTime = millis();
  bool ledState = false;
  unsigned long lastBlink = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    // Blink LED every 200ms during connection attempt
    if (millis() - lastBlink > 200) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastBlink = millis();
    }
    delay(50);  // Small delay to prevent busy-waiting
  }
  status = WiFi.status();

  if (status != WL_CONNECTED)
  {
    Log.errorln("Could not connect to %s, comms disabled", WIFI_SSID);
    digitalWrite(LED_PIN, LOW);  // Turn off LED on failure
    return false;
  }

  // Connection successful - keep LED on
  digitalWrite(LED_PIN, HIGH);

  // Create web server task on Core 0
  xTaskCreatePinnedToCore(
    webServerTask,          // Function to implement the task
    "WebServer",            // Name of the task
    8192,                   // Stack size in bytes
    this,                   // Task input parameter
    1,                      // Priority (1 is the default, higher numbers indicate higher priority)
    &webServerTaskHandle,   // Task handle
    0                       // Core 0
  );

  printWifiStatus();
  return true;
}

void Comms::webServerTask(void* parameter)
{
  Comms* comms = static_cast<Comms*>(parameter);

  comms->setupRoutes();
  comms->server.begin();
  Log.noticeln("Web server started on Core %d", xPortGetCoreID());

  while (true) {
    // Keep the task alive
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void Comms::setupRoutes()
{
  // Command routes
  server.on("/N", HTTP_GET, [this](AsyncWebServerRequest *request) {
    mc.queueWebCommand(Command::NEXT);
    sendMainPage(request);
  });

  server.on("/H", HTTP_GET, [this](AsyncWebServerRequest *request) {
    mc.queueWebCommand(Command::HOLD);
    sendMainPage(request);
  });

  server.on("/D", HTTP_GET, [this](AsyncWebServerRequest *request) {
    mc.queueWebCommand(Command::POWER_OFF);
    sendMainPage(request);
  });

  server.on("/W", HTTP_GET, [this](AsyncWebServerRequest *request) {
    mc.queueWebCommand(Command::WHITE);
    sendMainPage(request);
  });

  // Default route (root)
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    sendMainPage(request);
  });
}

void Comms::sendMainPage(AsyncWebServerRequest *request)
{
  milliseconds_t start = millis();

  const char* html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Andromeda Control</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            background: linear-gradient(135deg, #0c0c0c 0%, #1a1a1a 50%, #0c0c0c 100%);
            font-family: 'Arial', sans-serif;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            color: #ffffff;
        }

        .container {
            background: rgba(255, 255, 255, 0.05);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
            padding: 2rem;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
            text-align: center;
            max-width: 400px;
            width: 90%;
        }

        h1 {
            font-size: 2.5rem;
            margin-bottom: 2rem;
            background: linear-gradient(45deg, #00d4ff, #ff00d4, #00ff88, #ffaa00, #ff0066);
            background-size: 500% 500%;
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
            animation: gradientShift 30s ease-in-out infinite;
        }

        @keyframes gradientShift {
            0% { background-position: 0% 50%; }
            50% { background-position: 100% 50%; }
            100% { background-position: 0% 50%; }
        }

        .button-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 1rem;
            margin-top: 1rem;
        }

        .btn {
            display: block;
            padding: 1.2rem 1rem;
            background: linear-gradient(135deg, #333 0%, #555 100%);
            color: white;
            text-decoration: none;
            border-radius: 12px;
            border: 1px solid rgba(255, 255, 255, 0.2);
            font-size: 1.1rem;
            font-weight: bold;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }

        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(0, 0, 0, 0.4);
            border-color: rgba(255, 255, 255, 0.4);
        }

        .btn:active {
            transform: translateY(0);
        }

        .btn.next { background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%); }
        .btn.hold { background: linear-gradient(135deg, #2196F3 0%, #1976D2 100%); }
        .btn.off { background: linear-gradient(135deg, #f44336 0%, #d32f2f 100%); }
        .btn.white { background: linear-gradient(135deg, #ffffff 0%, #f0f0f0 100%); color: #333; }

        .status {
            margin-top: 1.5rem;
            padding: 0.8rem;
            background: rgba(0, 0, 0, 0.3);
            border-radius: 8px;
            font-size: 0.9rem;
            color: #aaa;
        }

        @media (max-width: 480px) {
            .button-grid {
                grid-template-columns: 1fr;
            }
            h1 {
                font-size: 2rem;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Andromeda</h1>
        <div class="button-grid">
            <a href="/N" class="btn next">Next</a>
            <a href="/H" class="btn hold">Hold</a>
            <a href="/D" class="btn off">Off</a>
            <a href="/W" class="btn white">White</a>
        </div>
    </div>

    <script>
        // Add visual feedback for button presses
        document.querySelectorAll('.btn').forEach(btn => {
            btn.addEventListener('click', function(e) {
                // Create ripple effect
                const ripple = document.createElement('span');
                const rect = this.getBoundingClientRect();
                const size = Math.max(rect.width, rect.height);
                const x = e.clientX - rect.left - size / 2;
                const y = e.clientY - rect.top - size / 2;

                ripple.style.cssText = `
                    position: absolute;
                    width: ${size}px;
                    height: ${size}px;
                    left: ${x}px;
                    top: ${y}px;
                    background: rgba(255, 255, 255, 0.3);
                    border-radius: 50%;
                    transform: scale(0);
                    animation: ripple 0.6s linear;
                    pointer-events: none;
                `;

                this.appendChild(ripple);

                setTimeout(() => {
                    ripple.remove();
                }, 600);
            });
        });

        // Add CSS animation for ripple effect
        const style = document.createElement('style');
        style.textContent = `
            @keyframes ripple {
                to {
                    transform: scale(4);
                    opacity: 0;
                }
            }
        `;
        document.head.appendChild(style);
    </script>
</body>
</html>
  )";

  request->send(200, "text/html", html);
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %s (%d dBm)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void Comms::loop()
{
}

#endif // ESP32