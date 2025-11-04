/* Invernadero + Telegram control
   - LEDs: GPIO23 (verde), GPIO2 (azul)
   - DHT22 -> GPIO4
   - OLED (SSD1306) -> SDA=21, SCL=22
   - Pot -> GPIO32
   - Telegram commands: /start, /led<gpio><on/off>, /dht22, /pote, /platiot, /display<cmd>
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ThingSpeak.h>

// -CONFIG (rellenar) ACA PONER EL SSID DE SU CELULAR, CONTRASEÑA Y EL TOKEN DEL BOOT DE TELEGRAM---------------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

#define BOT_TOKEN ""
#define CHAT_ID "" // string or number

// ThingSpeak (opcional)
const char* THINGSPEAK_SERVER = "api.thingspeak.com";
const char* THINGSPEAK_API_KEY = "KBVBHYA1LJA4Z6Y1"; // Reemplazar con tu Write API Key de ThingSpeak (16 caracteres)
unsigned long THINGSPEAK_CHANNEL_ID = 3145865; // Reemplazar con tu Channel ID de ThingSpeak

// --------------------- PINES ---------------------
#define DHTPIN 4
#define DHTTYPE DHT22
#define LED_GREEN_PIN 23
#define LED_BLUE_PIN 2
#define POT_PIN 32
#define SDA_PIN 21
#define SCL_PIN 22

// --------------------- OLED ---------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --------------------- DHT ---------------------
DHT dht(DHTPIN, DHTTYPE);

// --------------------- Telegram ---------------------
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);
unsigned long lastTelegramCheck = 0;
const unsigned long TELEGRAM_CHECK_MS = 2000; // 2s

// --------------------- ThingSpeak client ---------------------
WiFiClient thingClient;

// --------------------- Timers y estados ---------------------
const unsigned long DHT_INTERVAL = 2000;
unsigned long lastDhtRead = 0;
float currentTemp = NAN;
float currentHum = NAN;

bool ledGreen = false;
bool ledBlue = false;

// Control de envío a ThingSpeak (mínimo 15 segundos entre escrituras)
unsigned long lastThingSpeakWrite = 0;
const unsigned long THINGSPEAK_INTERVAL = 15000; // 15 segundos

// --------------------- Helpers ---------------------
String formatFloat(float v, int decimals=1) {
  char buf[16];
  dtostrf(v, 0, decimals, buf);
  return String(buf);
}

void showOnOLED(const String &title, const String &line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println(title);
  display.setTextSize(2);
  display.setCursor(0,20);
  display.println(line2);
  display.display();
}

// --------------------- Setup ---------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed!");
  }

  // Secure client for Telegram (HTTPS)
  secureClient.setInsecure(); // <-- simplifica (no validar certificado)

  // OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("No OLED found");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // DHT
  dht.begin();

  // Pins
  pinMode(LED_GREEN_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, LOW);
  pinMode(LED_BLUE_PIN, OUTPUT);
  digitalWrite(LED_BLUE_PIN, LOW);

  // ADC atenuation
  analogSetPinAttenuation(POT_PIN, ADC_11db);

  // ThingSpeak init
  ThingSpeak.begin(thingClient);

  // Welcome
  showOnOLED("Invernadero", "Iniciando...");
  delay(1200);
  display.clearDisplay();
  display.display();

  // Send startup message to bot (opcional)
  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(CHAT_ID, "🤖 Invernadero: conectado y listo", "");
  }
}

// --------------------- Telegram message handling ---------------------
void handleTelegramMessage(int i) {
  String chat_id = String(bot.messages[i].chat_id);
  String text = bot.messages[i].text;
  Serial.print("Msg: ");
  Serial.println(text);

  // /start
  if (text == "/start") {
    String welcome = "Invernadero Bot\nComandos:\n";
    welcome += "/start\n";
    welcome += "/led23on /led23off /led2on /led2off\n";
    welcome += "/dht22\n";
    welcome += "/pote\n";
    welcome += "/platiot\n";
    welcome += "/displayled /displaypote /displaydht\n";
    bot.sendMessage(chat_id, welcome, "");
    return;
  }

  // /led<gpio><on/off> -> ej: /led23on
  if (text.startsWith("/led")) {
    // parse numeric gpio and action (very simple parse)
    String tail = text.substring(4); // e.g. "23on"
    int idxOn = tail.indexOf("on");
    int idxOff = tail.indexOf("off");
    bool turnOn = false;
    if (idxOn >= 0) turnOn = true;
    if (idxOff >= 0) turnOn = false;
    // parse pin number from beginning
    String sPin = tail;
    if (idxOn >= 0) sPin = tail.substring(0, idxOn);
    if (idxOff >= 0) sPin = tail.substring(0, idxOff);
    int pin = sPin.toInt();
    if (pin == LED_GREEN_PIN || pin == LED_BLUE_PIN) {
      // valid pin
      if (turnOn) {
        digitalWrite(pin, HIGH);
        if (pin == LED_GREEN_PIN) ledGreen = true; else ledBlue = true;
        bot.sendMessage(chat_id, "LED encendido en pin " + String(pin), "");
      } else {
        digitalWrite(pin, LOW);
        if (pin == LED_GREEN_PIN) ledGreen = false; else ledBlue = false;
        bot.sendMessage(chat_id, "LED apagado en pin " + String(pin), "");
      }
      return;
    } else {
      bot.sendMessage(chat_id, "Error: solo pines 23 (verde) o 2 (azul) soportados", "");
      return;
    }
  } // end /led

  // /dht22
  if (text == "/dht22") {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      bot.sendMessage(chat_id, "Error lectura DHT22", "");
    } else {
      String msg = "Temp: " + formatFloat(t,1) + " C\nHum: " + formatFloat(h,1) + " %";
      bot.sendMessage(chat_id, msg, "");
    }
    return;
  }

  // /pote
  if (text == "/pote") {
    int potRaw = analogRead(POT_PIN);
    float volts = (potRaw / 4095.0f) * 3.3f;
    String msg = "Pot raw: " + String(potRaw) + "\nVolt: " + formatFloat(volts,2) + " V";
    bot.sendMessage(chat_id, msg, "");
    return;
  }

  // /platiot -> enviar a ThingSpeak (ejemplo)
  if (text == "/platiot") {
    // Verificar tiempo mínimo entre escrituras (15 segundos)
    unsigned long currentTime = millis();
    if (currentTime - lastThingSpeakWrite < THINGSPEAK_INTERVAL) {
      unsigned long waitTime = (THINGSPEAK_INTERVAL - (currentTime - lastThingSpeakWrite)) / 1000;
      bot.sendMessage(chat_id, "⏳ Espera " + String(waitTime) + " segundos antes de enviar datos nuevamente", "");
      return;
    }
    
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      bot.sendMessage(chat_id, "❌ Error lectura DHT22, no se envía a IoT", "");
      return;
    }
    
    // Mostrar datos que se van a enviar
    Serial.println("=== Enviando a ThingSpeak ===");
    Serial.printf("Temperatura: %.1f °C\n", t);
    Serial.printf("Humedad: %.1f %%\n", h);
    Serial.printf("Channel ID: %lu\n", THINGSPEAK_CHANNEL_ID);
    
    // Enviar a ThingSpeak (field1=temp, field2=hum)
    ThingSpeak.setField(1, t);
    ThingSpeak.setField(2, h);
    int response = ThingSpeak.writeFields(THINGSPEAK_CHANNEL_ID, THINGSPEAK_API_KEY);
    
    Serial.printf("Respuesta ThingSpeak: %d\n", response);
    
    if (response == 200) {
      lastThingSpeakWrite = currentTime; // Actualizar timestamp solo si fue exitoso
      String msg = "✅ Datos enviados a ThingSpeak OK\n";
      msg += "🌡️ Temp: " + formatFloat(t,1) + " °C\n";
      msg += "💧 Hum: " + formatFloat(h,1) + " %";
      bot.sendMessage(chat_id, msg, "");
    } else {
      String errorMsg = "❌ Error al enviar a ThingSpeak\nCódigo: " + String(response) + "\n";
      if (response == 0) {
        errorMsg += "Causa: Sin conexión a Internet";
      } else if (response == 400) {
        errorMsg += "Causa: API Key o Channel ID inválidos";
      } else if (response == 404) {
        errorMsg += "Causa: Canal no encontrado";
      } else if (response == -301) {
        errorMsg += "Causa: Tiempo de espera agotado";
      }
      bot.sendMessage(chat_id, errorMsg, "");
    }
    return;
  }

  // /display<cmd> -> mostrar estado en OLED
  if (text.startsWith("/display")) {
    String cmd = text.substring(8); // after "/display"
    if (cmd == "led") {
      String s = "LED23: " + String(ledGreen ? "ON" : "OFF") + "\nLED2: " + String(ledBlue ? "ON" : "OFF");
      showOnOLED("STATUS LEDs", s);
      bot.sendMessage(chat_id, "OLED: mostrado estado de LEDs", "");
    } else if (cmd == "pote") {
      int potRaw = analogRead(POT_PIN);
      float volts = (potRaw / 4095.0f) * 3.3f;
      showOnOLED("POT", String(formatFloat(volts,2)) + " V");
      bot.sendMessage(chat_id, "OLED: mostrado estado pot", "");
    } else if (cmd == "dht") {
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      if (isnan(h) || isnan(t)) {
        showOnOLED("DHT22", "Error lectura");
        bot.sendMessage(chat_id, "OLED: error lectura DHT", "");
      } else {
        showOnOLED("DHT22", "T:" + formatFloat(t,1) + "C H:" + formatFloat(h,1) + "%");
        bot.sendMessage(chat_id, "OLED: mostrado estado DHT", "");
      }
    } else {
      showOnOLED("DISPLAY", "Comando no reconocido");
      bot.sendMessage(chat_id, "OLED: comando display no reconocido", "");
    }
    return;
  }

  // default: unknown command
  bot.sendMessage(chat_id, "Comando no reconocido. /start para ayuda", "");
}

// --------------------- Main loop ---------------------
void loop() {
  // 1) DHT sampling periodic (non-blocking)
  if (millis() - lastDhtRead >= DHT_INTERVAL) {
    lastDhtRead = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      currentHum = h;
      currentTemp = t;
      Serial.printf("DHT: T=%.1f H=%.1f\n", t, h);
    } else {
      Serial.println("DHT error");
    }
  }

  // 2) Check Telegram updates (polling)
  if (millis() - lastTelegramCheck > TELEGRAM_CHECK_MS) {
    int numNew = bot.getUpdates(bot.last_message_received + 1);
    while (numNew) {
      for (int i = 0; i < numNew; i++) handleTelegramMessage(i);
      numNew = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTelegramCheck = millis();
  }

  // small idle
  delay(10);
}
