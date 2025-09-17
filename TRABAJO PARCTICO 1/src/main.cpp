#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "esp_system.h" 

// Pines
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
const unsigned long DHT_INTERVAL = 2000;
const unsigned long BLINK_INTERVAL = 500; 
const unsigned long DISPLAY_INTERVAL = 700;

unsigned long lastDHTRead = 0;
unsigned long lastBlink = 0;
unsigned long lastDisplayUpdate = 0;

// Estados y lecturas
float currentTemp = NAN;
float currentHum = NAN;
float tempReference = 25.0;
int humThreshold = 50;
bool ventState = false;
bool prevVentState = false;
bool watering = false;
bool prevWatering = false;
bool blinkLedState = false;

// Histeresis
const float VENT_HYST = 0.5f;

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

  // configuracion del botón
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonReading = digitalRead(BUTTON_PIN);
  buttonState = lastButtonReading;
  lastDebounceTime = millis();
  ignoreButtonUntil = millis() + 300;
  Serial.print("Button init reading: ");
  Serial.println(lastButtonReading);

  // Configurar ADC del potenciómetro
  #if defined(ARDUINO_ARCH_ESP32)
    analogSetPinAttenuation(POT_PIN, ADC_11db);
  #endif

  // Semilla aleatoria
  randomSeed((uint32_t)esp_random());

  // Umbral aleatorio
  humThreshold = random(40, 61);
  Serial.println("=== Inicio del sistema ===");
  Serial.print("Umbral de humedad generado: ");
  Serial.print(humThreshold);
  Serial.println("%");
  

  // Mostrar umbral de inicio en OLED
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

  // Primera lectura de inicializacion de variables
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
      //display.println("*** MENU PRINCIPAL ***");
      display.println("1. Temp Actual");
      display.println("2. Humedad Actual");
      display.println("3. Estado Completo");
      display.println("4. Config Temp");
      display.println("5. Config Hum");
      display.println("6. Manual Vent");
      display.println("7. Manual Riego");
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
      break;

    case MENU_CONFIG_HUM:
      display.println("CONFIG HUMEDAD");
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
      display.print("Umbral Fijo: ");
      display.print(humThreshold);
      display.println("%");
      display.setCursor(0, 54);
      display.println("Usar potenciometro");
      break;

    case MENU_MANUAL_VENT:
      display.println("CONTROL VENT");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Estado:");
      display.print(ventState ? "ON" : "OFF");
      display.setTextSize(1);
      display.setCursor(0, 42);
      break;

    case MENU_MANUAL_RIEGO:
      display.println("CONTROL RIEGO");
      display.setTextSize(2);
      display.setCursor(0, 18);
      display.print("Riego: ");
      display.print(watering ? "ON" : "OFF");
      display.setTextSize(1);
      display.setCursor(0, 42);
      display.setCursor(0, 54);
      display.println("Usar potenciometro");
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
    } else {
      currentHum = h;
      currentTemp = t;
      sensorsUpdated = true;
    }

    // Lectura potenciómetro
    int potRaw = analogRead(POT_PIN);
    
    // Modificacion de variables segun opcion del menú
    if (currentMenu == MENU_CONFIG_HUM) {
      //Simular humedad modificada
      currentHum = (potRaw / 4095.0f) * 20.0f + 40.0f;
      manualRiegoOverride = false;
    } else if (currentMenu == MENU_MANUAL_RIEGO) {
      //control manual de riego
      if (potRaw > 2047) {
        manualRiegoOverride = true;
        watering = true;
      } else {
        manualRiegoOverride = true;
        watering = false;
      }
    } else if (currentMenu == MENU_CONFIG_TEMP) {
      tempReference = (potRaw / 4095.0f) * 40.0f + 10.0f;
    }
    sensorsUpdated = true;
  }
}

void handleVentilationAndIrrigation() {
  // Ventilación - automática o manual
  bool newVentState = ventState;
  if (manualVentOverride) {
    newVentState = ventState;
  } else {
    // Control automático con histeresis
    if (!isnan(currentTemp)) {
      if (currentTemp > tempReference + VENT_HYST) {
        newVentState = true;
      } else if (currentTemp < tempReference - VENT_HYST) {
        newVentState = false;
      }
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

  // Riego automático - manual
  bool shouldWater = false;
  if (manualRiegoOverride) {
    // Control manual
    shouldWater = watering;
  } else {
    // Control automático
    if (!isnan(currentHum)) {
      shouldWater = (currentHum < (float)humThreshold);
    }
  }
  
  // eventos riego
  if (shouldWater && !prevWatering) {
    Serial.println("Evento: RIEGO ACTIVADO (humedad por debajo del umbral)");
  } else if (!shouldWater && prevWatering) {
    Serial.println("Evento: RIEGO DETENIDO (humedad OK)");
    digitalWrite(LED_RIEGO_PIN, LOW);
    blinkLedState = false;
  }
  watering = shouldWater;
  prevWatering = shouldWater;

  // parpadeo
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

  // Detectar estados
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        // Navegación del menú
        if (currentMenu == MENU_MAIN) {
          currentMenu = MENU_TEMP_DISPLAY;
        } else {
          currentMenu = (currentMenu + 1) % 8;
          if (currentMenu == MENU_MAIN) {
            currentMenu = MENU_TEMP_DISPLAY;
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
      if (newHum >= 40 && newHum <= 60) {
        humThreshold = newHum;
        Serial.print("Umbral de humedad configurado a: ");
        Serial.print(newHum);
        Serial.println("%");
        menuChanged = true;
      } else {
        Serial.println("Error: Humedad debe estar entre 40-60%");
      }
    }
    else if (command == "VENT ON") {
      manualVentOverride = true;
      ventState = true;
      Serial.println("Ventilación activada manualmente");
      menuChanged = true;
    }
    else if (command == "VENT OFF") {
      manualVentOverride = true;
      ventState = false;
      Serial.println("Ventilación desactivada manualmente");
      menuChanged = true;
    }
    else if (command == "RIEGO ON") {
      manualRiegoOverride = true;
      watering = true;
      Serial.println("Riego activado manualmente");
      menuChanged = true;
    }
    else if (command == "RIEGO OFF") {
      manualRiegoOverride = true;
      watering = false;
      Serial.println("Riego desactivado manualmente");
      menuChanged = true;
    }
    else if (command == "AUTO") {
      manualVentOverride = false;
      manualRiegoOverride = false;
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
      Serial.println("=====================================\n");
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

  // Actualizar display
  if (sensorsUpdated || menuChanged || (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL)) {
    updateDisplay();
    sensorsUpdated = false;
    menuChanged = false;
  }

  delay(10);
}
