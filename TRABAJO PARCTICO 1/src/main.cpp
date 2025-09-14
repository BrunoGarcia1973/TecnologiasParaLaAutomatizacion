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

// Sistema de menú
enum MenuState {
  MENU_MAIN = 0,
  MENU_TEMP_DISPLAY = 1,
  MENU_HUM_DISPLAY = 2,
  MENU_FULL_STATUS = 3,
  MENU_CONFIG_TEMP = 4,
  MENU_CONFIG_HUM = 5,
  MENU_MANUAL_VENT = 6,
  MENU_MANUAL_RIEGO = 7
};

int currentMenu = MENU_MAIN;
bool menuChanged = true;
bool sensorsUpdated = false;

// Control manual
bool manualMode = false;
bool manualVentOverride = false;
bool manualRiegoOverride = false;

// Boton debounce
int lastButtonReading = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
unsigned long ignoreButtonUntil = 0;

void logEvent(const char* msg) {
  Serial.println(msg);
}

void printMenuHelp() {
  Serial.println("\n=== COMANDOS DISPONIBLES ===");
  Serial.println("TEMP <valor>     - Configurar temperatura de referencia (10-50°C)");
  Serial.println("HUM <valor>      - Configurar umbral de humedad (20-80%)");
  Serial.println("VENT ON/OFF      - Control manual de ventilación");
  Serial.println("RIEGO ON/OFF     - Control manual de riego");
  Serial.println("AUTO             - Volver al modo automático");
  Serial.println("STATUS           - Mostrar estado completo");
  Serial.println("HELP             - Mostrar esta ayuda");
  Serial.println("=============================\n");
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
  
  // Mostrar ayuda de comandos
  printMenuHelp();

  // Mostrar umbral de inicio en OLED (3s)
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Invernadero - Iniciando");
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("Umbral:");
  display.print(humThreshold);
  display.println("%");
  display.display();
  delay(3000);
  display.clearDisplay();
  display.display();

  // Primera lectura para inicializar variables
  lastDHTRead = millis() - DHT_INTERVAL;
  sensorsUpdated = true;
  menuChanged = true;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  switch (currentMenu) {
    case MENU_MAIN:
      //display.println("=== MENU PRINCIPAL ===");
      display.println("1. Temp Actual");
      display.println("2. Humedad Actual");
      display.println("3. Estado Completo");
      display.println("4. Config Temp");
      display.println("5. Config Hum");
      display.println("6. Manual Vent");
      display.println("7. Manual Riego");
      display.setTextSize(1);
      display.setCursor(0, 56);
      display.print("Modo: ");
      display.print(manualMode ? "MANUAL" : "AUTO");
      break;

    case MENU_TEMP_DISPLAY:
      display.println("TEMPERATURA");
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
      display.print("Ref:");
      display.print(tempReference, 1);
      display.print(" C");
      display.setCursor(0, 54);
      display.print("Vent: ");
      display.print(ventState ? "ON" : "OFF");
      break;

    case MENU_HUM_DISPLAY:
      display.println("HUMEDAD");
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
      display.print("Umbral:");
      display.print(humThreshold);
      display.print("%");
      display.setCursor(0, 54);
      display.print("Riego: ");
      display.print(watering ? "ON" : "OFF");
      break;

    case MENU_FULL_STATUS:
      display.println("ESTADO COMPLETO");
      display.setTextSize(1);
      display.setCursor(0, 12);
      if (!isnan(currentTemp)) {
        display.print("Temp: ");
        display.print(currentTemp, 1);
        display.println(" C");
      } else {
        display.println("Temp: --.- C");
      }
      display.setCursor(0, 24);
      if (!isnan(currentHum)) {
        display.print("Hum:  ");
        display.print(currentHum, 1);
        display.println(" %");
      } else {
        display.println("Hum:  --.- %");
      }
      display.setCursor(0, 36);
      display.print("Ref Temp:");
      display.print(tempReference, 1);
      display.println(" C");
      display.setCursor(0, 48);
      display.print("Umbral:");
      display.print(humThreshold);
      display.println("%");
      display.setCursor(0, 56);
      display.print("Vent:");
      display.print(ventState ? "ON " : "OFF");
      display.print(" Riego:");
      display.print(watering ? "ON" : "OFF");
      break;

    case MENU_CONFIG_TEMP:
      display.println("CONFIG TEMP");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Ref:");
      display.print(tempReference, 1);
      display.println(" C");
      display.setTextSize(1);
      display.setCursor(0, 42);
      display.println("Usar potenciometro");
      display.setCursor(0, 54);
      display.println("o comando TEMP");
      break;

    case MENU_CONFIG_HUM:
      display.println("CONFIG HUMEDAD");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Umbral:");
      display.print(humThreshold);
      display.println("%");
      display.setTextSize(1);
      display.setCursor(0, 42);
      display.println("Comando: HUM <valor>");
      display.setCursor(0, 54);
      display.println("Rango: 20-80%");
      break;

    case MENU_MANUAL_VENT:
      display.println("CONTROL VENT");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Estado: ");
      display.print(ventState ? "ON" : "OFF");
      display.setTextSize(1);
      display.setCursor(0, 42);
      display.println("Comando: VENT ON/OFF");
      display.setCursor(0, 54);
      display.print("Manual: ");
      display.print(manualVentOverride ? "SI" : "NO");
      break;

    case MENU_MANUAL_RIEGO:
      display.println("CONTROL RIEGO");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Estado: ");
      display.print(watering ? "ON" : "OFF");
      display.setTextSize(1);
      display.setCursor(0, 42);
      display.println("Comando: RIEGO ON/OFF");
      display.setCursor(0, 54);
      display.print("Manual: ");
      display.print(manualRiegoOverride ? "SI" : "NO");
      break;
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
  // Ventilación - automática o manual
  bool newVentState = ventState;
  if (manualVentOverride) {
    // Control manual - mantener estado actual
    newVentState = ventState;
  } else {
    // Control automático con histeresis
    if (!isnan(currentTemp)) {
      if (currentTemp > tempReference + VENT_HYST) {
        newVentState = true;
      } else if (currentTemp < tempReference - VENT_HYST) {
        newVentState = false;
      } // si está dentro de la banda, no cambiar
    }
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

  // Riego - automático o manual
  bool shouldWater = false;
  if (manualRiegoOverride) {
    // Control manual - mantener estado actual
    shouldWater = watering;
  } else {
    // Control automático (humedad < umbral)
    if (!isnan(currentHum)) {
      shouldWater = (currentHum < (float)humThreshold);
    }
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
  if (millis() < ignoreButtonUntil) return;

  int reading = digitalRead(BUTTON_PIN);

  // Detectar cambios de estado con debounce
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Botón presionado
        // Navegación del menú
        if (currentMenu == MENU_MAIN) {
          // Desde menú principal, ir a la primera opción
          currentMenu = MENU_TEMP_DISPLAY;
        } else {
          // Ciclar entre opciones
          currentMenu = (currentMenu + 1) % 8;
          if (currentMenu == MENU_MAIN) {
            currentMenu = MENU_TEMP_DISPLAY; // Saltar el menú principal en el ciclo
          }
        }
        menuChanged = true;
        Serial.print("Menu cambiado a: ");
        Serial.println(currentMenu);
      }
    }
  }
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toUpperCase();
    
    if (command.startsWith("TEMP ")) {
      float newTemp = command.substring(5).toFloat();
      if (newTemp >= 10.0 && newTemp <= 50.0) {
        tempReference = newTemp;
        Serial.print("Temperatura de referencia configurada a: ");
        Serial.print(newTemp, 1);
        Serial.println(" °C");
        menuChanged = true;
      } else {
        Serial.println("Error: Temperatura debe estar entre 10-50°C");
      }
    }
    else if (command.startsWith("HUM ")) {
      int newHum = command.substring(4).toInt();
      if (newHum >= 20 && newHum <= 80) {
        humThreshold = newHum;
        Serial.print("Umbral de humedad configurado a: ");
        Serial.print(newHum);
        Serial.println("%");
        menuChanged = true;
      } else {
        Serial.println("Error: Humedad debe estar entre 20-80%");
      }
    }
    else if (command == "VENT ON") {
      manualVentOverride = true;
      ventState = true;
      manualMode = true;
      Serial.println("Ventilación activada manualmente");
      menuChanged = true;
    }
    else if (command == "VENT OFF") {
      manualVentOverride = true;
      ventState = false;
      manualMode = true;
      Serial.println("Ventilación desactivada manualmente");
      menuChanged = true;
    }
    else if (command == "RIEGO ON") {
      manualRiegoOverride = true;
      watering = true;
      manualMode = true;
      Serial.println("Riego activado manualmente");
      menuChanged = true;
    }
    else if (command == "RIEGO OFF") {
      manualRiegoOverride = true;
      watering = false;
      manualMode = true;
      Serial.println("Riego desactivado manualmente");
      menuChanged = true;
    }
    else if (command == "AUTO") {
      manualVentOverride = false;
      manualRiegoOverride = false;
      manualMode = false;
      Serial.println("Modo automático activado");
      menuChanged = true;
    }
    else if (command == "STATUS") {
      Serial.println("\n=== ESTADO COMPLETO DEL INVERNADERO ===");
      if (!isnan(currentTemp)) {
        Serial.print("Temperatura actual: ");
        Serial.print(currentTemp, 1);
        Serial.println(" °C");
      } else {
        Serial.println("Temperatura actual: --.- °C");
      }
      if (!isnan(currentHum)) {
        Serial.print("Humedad actual: ");
        Serial.print(currentHum, 1);
        Serial.println(" %");
      } else {
        Serial.println("Humedad actual: --.- %");
      }
      Serial.print("Temperatura de referencia: ");
      Serial.print(tempReference, 1);
      Serial.println(" °C");
      Serial.print("Umbral de humedad: ");
      Serial.print(humThreshold);
      Serial.println(" %");
      Serial.print("Ventilación: ");
      Serial.println(ventState ? "ACTIVA" : "INACTIVA");
      Serial.print("Riego: ");
      Serial.println(watering ? "ACTIVO" : "INACTIVO");
      Serial.print("Modo: ");
      Serial.println(manualMode ? "MANUAL" : "AUTOMATICO");
      Serial.println("=====================================\n");
    }
    else if (command == "HELP") {
      printMenuHelp();
    }
    else if (command.length() > 0) {
      Serial.println("Comando no reconocido. Escriba HELP para ver comandos disponibles.");
    }
  }
}



void loop() {
  readSensors();
  handleVentilationAndIrrigation();
  handleButton();
  handleSerialCommands();

  // Actualizar display solo cuando hay nuevas lecturas, cambio de menú, o timeout
  if (sensorsUpdated || menuChanged || (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL)) {
    updateDisplay();
    sensorsUpdated = false;
    menuChanged = false;
  }

  // pequeño delay para no saturar loop
  delay(10);
}
