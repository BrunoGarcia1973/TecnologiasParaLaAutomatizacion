/*
  Invernadero ESP32 - versión mejorada
  - Histeresis en ventilación
  - Actualiza OLED sólo cuando hace falta
  - Manejo correcto de eventos de riego (prevWatering)
  - Configura atenuación ADC para el potenciómetro
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "esp_system.h" // para esp_random()

// Pines (ajustables)
#define DHTPIN         4
#define DHTTYPE        DHT22
#define LED_VENT_PIN   2
#define LED_RIEGO_PIN  5
#define POT_PIN        32
#define BUTTON_PIN     33
#define SDA_PIN        21
#define SCL_PIN        22

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// DHT
DHT dht(DHTPIN, DHTTYPE);

// Tiempos
const unsigned long DHT_INTERVAL = 2000; // ms
const unsigned long BLINK_INTERVAL = 500; // ms para riego
const unsigned long DISPLAY_INTERVAL = 700; // ms mínimo entre updates de pantalla

unsigned long lastDHTRead = 0;
unsigned long lastBlink = 0;
unsigned long lastDisplayUpdate = 0;

// Estados y lecturas
float currentTemp = NAN;
float currentHum = NAN;
float tempReference = 25.0;
int humThreshold = 50; // generado al inicio
bool ventState = false;
bool prevVentState = false;
bool watering = false;
bool prevWatering = false;
bool blinkLedState = false;

// Histeresis (para evitar oscilaciones)
const float VENT_HYST = 0.5f; // grados C

// Pantalla
int screen = 1; // 1 = temp, 2 = hum
bool screenChanged = true;
bool sensorsUpdated = false;

// Boton debounce
int lastButtonReading = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
unsigned long ignoreButtonUntil = 0;

void logEvent(const char* msg) {
  Serial.println(msg);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERROR: No se encontro OLED");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // DHT
  dht.begin();

  // Pines salidas
  pinMode(LED_VENT_PIN, OUTPUT);
  digitalWrite(LED_VENT_PIN, LOW);
  pinMode(LED_RIEGO_PIN, OUTPUT);
  digitalWrite(LED_RIEGO_PIN, LOW);

  // --- IMPORTANTE: configurar el botón ANTES de leer su estado
  pinMode(BUTTON_PIN, INPUT_PULLUP);        // usamos pull-up interno
  lastButtonReading = digitalRead(BUTTON_PIN);
  buttonState = lastButtonReading;
  lastDebounceTime = millis();
  ignoreButtonUntil = millis() + 300;      // ignorar 300 ms tras arranque
  Serial.print("Button init reading: ");
  Serial.println(lastButtonReading);

  // Configurar ADC del potenciómetro (ESP32)
  #if defined(ARDUINO_ARCH_ESP32)
    analogSetPinAttenuation(POT_PIN, ADC_11db);
  #endif

  // Semilla aleatoria (ESP32)
  randomSeed((uint32_t)esp_random());

  // Umbral aleatorio [40..60]
  humThreshold = random(40, 61);
  Serial.println("=== Inicio del sistema ===");
  Serial.print("Umbral de humedad generado: ");
  Serial.print(humThreshold);
  Serial.println("%");

  // Mostrar umbral de inicio en OLED (3s)
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Invernadero - Iniciando");
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("Umbral: ");
  display.print(humThreshold);
  display.println("%");
  display.display();
  delay(3000);
  display.clearDisplay();
  display.display();

  // Primera lectura para inicializar variables
  lastDHTRead = millis() - DHT_INTERVAL;
  sensorsUpdated = true;
  screenChanged = true;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (screen == 1) {
    // Pantalla 1: Temperatura actual, referencia y estado ventilacion
    display.println("Pantalla 1 - Temperatura");
    display.setTextSize(2);
    display.setCursor(0, 18);
    if (!isnan(currentTemp)) {
      display.print("T: ");
      display.print(currentTemp, 1);
      display.print(" C");
    } else {
      display.print("T: --.- C");
    }
    display.setTextSize(1);
    display.setCursor(0, 42);
    display.print("Ref: ");
    display.print(tempReference, 1);
    display.print(" C");
    display.setCursor(0, 54);
    display.print("Ventilacion: ");
    display.print(ventState ? "ON" : "OFF");
  } else {
    // Pantalla 2: Humedad actual, umbral y estado riego
    display.println("Pantalla 2 - Humedad");
    display.setTextSize(2);
    display.setCursor(0, 18);
    if (!isnan(currentHum)) {
      display.print("H: ");
      display.print(currentHum, 1);
      display.print(" %");
    } else {
      display.print("H: --.- %");
    }
    display.setTextSize(1);
    display.setCursor(0, 42);
    display.print("Umbral: ");
    display.print(humThreshold);
    display.print("%");
    display.setCursor(0, 54);
    display.print("Riego: ");
    display.print(watering ? "ON" : "OFF");
  }

  display.display();
  lastDisplayUpdate = millis();
}

void readSensors() {
  unsigned long now = millis();
  if (now - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      Serial.println("Warning: lectura DHT fallida");
      // no sobreescribir si falló (mantenemos último valor válido)
    } else {
      currentHum = h;
      currentTemp = t;
      sensorsUpdated = true;
    }

    // Leer potenciómetro (ADC 12-bit: 0..4095)
    int potRaw = analogRead(POT_PIN);
    // Mapear a 10..50°C (ajustable)
    tempReference = (potRaw / 4095.0f) * 40.0f + 10.0f;
    sensorsUpdated = true;
  }
}

void handleVentilationAndIrrigation() {
  // Ventilación con histeresis
  bool newVentState = ventState;
  if (!isnan(currentTemp)) {
    if (currentTemp > tempReference + VENT_HYST) {
      newVentState = true;
    } else if (currentTemp < tempReference - VENT_HYST) {
      newVentState = false;
    } // si está dentro de la banda, no cambiar
  }
  if (newVentState != prevVentState) {
    if (newVentState) {
      Serial.println("Evento: Ventilacion ACTIVADA");
    } else {
      Serial.println("Evento: Ventilacion APAGADA");
    }
    prevVentState = newVentState;
  }
  ventState = newVentState;
  digitalWrite(LED_VENT_PIN, ventState ? HIGH : LOW);

  // Riego (humedad < umbral)
  bool shouldWater = false;
  if (!isnan(currentHum)) {
    shouldWater = (currentHum < (float)humThreshold);
  }
  // eventos riego (usar prevWatering)
  if (shouldWater && !prevWatering) {
    Serial.println("Evento: RIEGO ACTIVADO (humedad por debajo del umbral)");
  } else if (!shouldWater && prevWatering) {
    Serial.println("Evento: RIEGO DETENIDO (humedad OK)");
    // asegurar LED apagado
    digitalWrite(LED_RIEGO_PIN, LOW);
    blinkLedState = false;
  }
  watering = shouldWater;
  prevWatering = shouldWater;

  // Si riego -> parpadeo
  if (watering) {
    unsigned long now = millis();
    if (now - lastBlink >= BLINK_INTERVAL) {
      lastBlink = now;
      blinkLedState = !blinkLedState;
      digitalWrite(LED_RIEGO_PIN, blinkLedState ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_RIEGO_PIN, LOW);
  }
}

void handleButton() {
  // Ignorar botón durante los primeros ms
  if (millis() < ignoreButtonUntil) return;

  int reading = digitalRead(BUTTON_PIN);
  Serial.print("Button reading: ");
  Serial.println(reading);

  // Debounce simple
  if (reading != buttonState && (millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading == LOW) { // flanco de presion
      screen = (screen == 1) ? 2 : 1;
      screenChanged = true;
      Serial.print("Pantalla cambiada a: ");
      Serial.println(screen);
    }
    buttonState = reading;
    lastDebounceTime = millis();
  }
}



void loop() {
  readSensors();
  handleVentilationAndIrrigation();
  handleButton();

  // Actualizar display solo cuando hay nuevas lecturas, cambio de pantalla, o timeout
  if (sensorsUpdated || screenChanged || (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL)) {
    updateDisplay();
    sensorsUpdated = false;
    screenChanged = false;
  }

  // pequeño delay para no saturar loop
  delay(10);
}
