#include <Arduino.h>
#define BUTTON 5
#define LED1 12
#define LED2 14
// put function declarations here:
void esp32_io_setup(void);
void setup() {
 // put your setup code here, to run once:

 Serial.begin(115200);
 Serial.println("Hello, I'm in a terminal!");
 Serial.println();
 esp32_io_setup();
}
void loop() {
 // put your main code here, to run repeatedly:
  if (digitalRead(BUTTON)){
 digitalWrite(LED1, HIGH);
 digitalWrite(LED2, LOW);
 Serial.println("button OFF");
 }else{
 digitalWrite(LED1, LOW);
 digitalWrite(LED2, HIGH);
 Serial.println("button ON");
 }
}
// put function definitions here:
void esp32_io_setup(void) {
 pinMode(BUTTON, INPUT_PULLUP);
 pinMode(LED1, OUTPUT);
 pinMode(LED2, OUTPUT);
}
