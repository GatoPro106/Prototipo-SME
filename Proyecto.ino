#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <SPI.h>
#include <SD.h>

//Configuracion de Red, Firebase y tiempo

#define WIFI_SSID "TU_SSID_DE_WIFI"
#define WIFI_PASSWORD "TU_PASSWORD"

#define FIREBASE_HOST "TU_PROYECTO_RTDB.firebaseio.com"
#define FIREBASE_AUTH "TU_TOKEN_SECRETO_O_SECRET_KEY"

const long gmtOffset_sec = -21600;
const int daylightOffset_sec = 0;

//Pines

#define PIN_GAS 0      
#define PIN_RUIDO 1    
#define PIN_BUZZER 2   
#define PIN_SD_CS 7    
#define I2C_SDA 8      
#define I2C_SCL 9      

//Alertas

#define TEMP_LIMITE   40.0
#define GAS_LIMITE    1800.0  // Umbral en PPM reales
#define RUIDO_LIMITE  85.0    // Umbral en dB

//Tamaño de la OLED

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;

// Variables de control 
bool bmeOnline = false;
bool oledOnline = false;
bool sdOnline = false;
bool pantallaPrendida = true;

//Alerta modo Local
bool alertaGeneral = false;
bool modoLocal = true;

// Control de variables
float temperatura, humidity, presion, gasPPM, ruidoDB;
float gasProm = 0, ruidoProm = 0, tempProm = 0;
int muestras = 0;

// Control de tiempo 
unsigned long tiempoLectura = 0;
unsigned long tiempoEnvio = 0;
const unsigned long intervaloLectura = 1000; // Medición cada segundo
const unsigned long intervaloEnvio = 60000;  // Envío/Guardado cada minuto

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=========================================");
  Serial.println("     SME - SISTEMA UNIFICADO DE MONITOREO  ");
  Serial.println("=========================================");

  pinMode(PIN_GAS, INPUT);
  pinMode(PIN_RUIDO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);

  if(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    oledOnline = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("=== DIAGNOSTICO ===");
    display.display();
  }
  delay(500);

  if (bme.begin(0x76, &Wire)) {
    bmeOnline = true;
    Serial.println("[+] OK: BME280 detectado.");
    if (oledOnline) { display.println("-> BME280:  [ OK ]"); display.display(); }
  } else {
    Serial.println("[-] Error: BME280 no responde.");
    if (oledOnline) { display.println("-> BME280:  [ ERROR ]"); display.display(); }
  }
  delay(500);

  Serial.println("[Hardware] Inicializando MicroSD...");
  if (SD.begin(PIN_SD_CS, SPI, 2000000)) { 
    sdOnline = true;
    Serial.println("[+] OK: Tarjeta SD lista.");
    if (oledOnline) { display.println("-> MicroSD: [ OK ]"); display.display(); }
  } else {
    Serial.println("[-] Error: MicroSD no encontrada.");
    if (oledOnline) { display.println("-> MicroSD: [ ERROR ]"); display.display(); }
  }
  delay(500);

  if (oledOnline) { display.print("-> Red: Conectando"); display.display(); }
  
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 12000) {
    delay(400);
    Serial.print(".");
    if (oledOnline) { display.print("."); display.display(); }
  }

  if(WiFi.status() == WL_CONNECTED) {
    modoLocal = false;
    Serial.println("\n[+] WiFi Conectado con éxito.");
    if (oledOnline) { display.println("\n-> WiFi:    [ CONECTADO ]"); display.display(); }
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  } else {
    modoLocal = true;
    Serial.println("\n[-] WiFi No disponible. Modo local activo.");
    if (oledOnline) { display.println("\n-> WiFi:    [ OFFLINE ]"); display.display(); }
  }

  delay(3000); 
}

void loop() {
  unsigned long tiempoActual = millis();

  //Conteo de Lecturas
  if (tiempoActual - tiempoLectura >= intervaloLectura) {
    tiempoLectura = tiempoActual;

    leerSensores();
    
    //Chequeo de variables
    bool alertaTemp = (temperatura > TEMP_LIMITE);
    bool alertaGas = (gasPPM > GAS_LIMITE);
    bool alertaRuido = (ruidoDB > RUIDO_LIMITE);
    alertaGeneral = alertaTemp || alertaGas || alertaRuido;

    if (oledOnline) {
      display.clearDisplay();  
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      
      if (alertaGeneral) {
        digitalWrite(PIN_BUZZER, HIGH);
        
        display.setCursor(0, 0);
        display.println("!! ALERTA DE MINA !!");
        display.println("--------------------");
        if(alertaTemp) { display.print("Temp Critica: "); display.print(temperatura, 1); display.println(" C"); }
        if(alertaGas)  { display.print("Gas Rango:    "); display.print(gasPPM, 0);       display.println(" PPM"); }
        if(alertaRuido){ display.print("Ruido Alto:   "); display.print(ruidoDB, 0);     display.println(" dB"); }
      } else {
        digitalWrite(PIN_BUZZER, LOW);
        
        display.setCursor(0, 0);
        display.println("=== MONITOREO SME ===");
        display.println("---------------------");
        display.print("Temp:     "); display.print(temperatura, 1); display.println(" C"); 
        display.print("Gas Rng:  "); display.print(gasPPM, 0);       display.println(" PPM");
        display.print("Ruido:    "); display.print(ruidoDB, 0);     display.println(" dB");
      }
      
      display.display(); 
    }

    gasProm += gasPPM;
    ruidoProm += ruidoDB;
    tempProm += temperatura;
    muestras++;
  }

  //Envio de promedios
  if (tiempoActual - tiempoEnvio >= intervaloEnvio) {
    tiempoEnvio = tiempoActual;

    if (muestras == 0) muestras = 1;

    float gasFinal = gasProm / muestras;
    float ruidoFinal = ruidoProm / muestras;
    float tempFinal = tempProm / muestras;

    String jsonPayload = crearJSON(tempFinal, humidity, presion, gasFinal, ruidoFinal);

    if (WiFi.status() != WL_CONNECTED) {
      modoLocal = true;
      guardarEnSD(jsonPayload);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
      if (modoLocal) {
        sincronizarSDconFirebase();
        modoLocal = false;
      }
      enviarFirebase(jsonPayload);
    }

    gasProm = 0; ruidoProm = 0; tempProm = 0; muestras = 0;
  }
}

void leerSensores() {
  temperatura = bmeOnline ? bme.readTemperature() : 0.0;
  humidity = bmeOnline ? bme.readHumidity() : 0.0;
  presion = bmeOnline ? (bme.readPressure() / 100.0F) : 0.0;

  int gasRaw = analogRead(PIN_GAS);
  float voltsGas = (gasRaw * 3.3) / 4095.0;
  if (voltsGas < 0.1) voltsGas = 0.1; 

  float Rs = RL_VALOR * ((3.3 - voltsGas) / voltsGas);
  gasPPM = CO_FACTOR_A * pow((Rs / R0), CO_PENDIENTE_B);

  if(gasPPM < 1.0) gasPPM = 1.0;
  if(gasPPM > 5000.0) gasPPM = 5000.0;

  int maximo = 0, minimo = 4095;
  for(int i = 0; i < 80; i++) { 
    int lectura = analogRead(PIN_RUIDO);
    if(lectura > maximo) maximo = lectura;
    if(lectura < minimo) minimo = lectura;
  }
  float voltaje = (maximo - minimo) * (3.3 / 4095.0);
  ruidoDB = 20 * log10(voltaje / 0.00631);
  if(ruidoDB < 0 || isnan(ruidoDB)) ruidoDB = 0;
}

String crearJSON(float t, float h, float p, float g, float r) {
  time_t ahora;
  time(&ahora);
  unsigned long long timestampMilis = (unsigned long long)ahora * 1000ULL;
  
  String json = "{";
  json += "\"temperatura\":" + String(t, 1) + ",";
  json += "\"humedad\":" + String(h, 1) + ",";
  json += "\"presion\":" + String(p, 1) + ",";
  json += "\"gas\":" + String(g, 0) + ",";
  json += "\"ruido\":" + String(r, 0) + ",";
  json += "\"alerta\":" + String(alertaGeneral ? "true" : "false") + ",";
  json += "\"timestamp\":" + String(timestampMilis);
  json += "}";
  return json;
}

void enviarFirebase(String json) {
  WiFiClientSecure client;
  client.setInsecure(); 
  
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/SME_Monitoreo.json?auth=" + String(FIREBASE_AUTH);
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(json); 
  Serial.printf("[Nube] Respuesta de Firebase: %d\n", httpCode);
  http.end();
}

void guardarEnSD(String data) {
  if (!sdOnline) return;
  
  File file = SD.open("/hist.txt", FILE_WRITE);
  if(!file) {
    Serial.println("[-] Error al abrir hist.txt en la SD.");
    return;
  }
  file.println(data);
  file.close();
  Serial.println("[SD] Registro guardado con éxito.");
}

void sincronizarSDconFirebase() {
  if (!sdOnline) return;

  File file = SD.open("/hist.txt");
  if(!file) return; 

  Serial.println("[Sincronización] Transfiriendo registros locales a Firebase...");
  while(file.available()) {
    String lineaJSON = file.readStringUntil('\n');
    lineaJSON.trim();
    if(lineaJSON.length() > 0) {
      enviarFirebase(lineaJSON);
      delay(200);
    }
  }
  file.close();
  
  SD.remove("/hist.txt");
  Serial.println("[Sincronización] Buffer de tarjeta limpio y sincronizado.");
}