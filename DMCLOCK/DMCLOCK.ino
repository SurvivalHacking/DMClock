// DM-CLOCK V1.0 16/11/25 by davide gatti - Survival hacking - www.survivalhacking.it
//
// Un orologio ispirato alle copertine dei Singles dei Depeche Mode. / A clock inspired by the covers of Depeche Mode's singles.
// 
// Per funzionare si deve collegare alla rete WIFI: / To operate, it must be connected to a Wi-Fi network:
// All'avvio si configura in access point, cercare nella rete WIFI il dispositivo DM-CLOCK e collegarsi. Se non appare automaticamente la pagina di configurazione andare con il browser all'indirizzo 192.168.1.4 e da li configurare la rete WIFI e il vosto fuso orario.
// When starting up, configure it as an access point, search for the DM-CLOCK device on the Wi-Fi network, and connect. If the configuration page does not appear automatically, go to 192.168.1.4 in your browser and configure the Wi-Fi network and your time zone from there.

// Funzioni: / Functions:
// 
// Tasto MODE: premuto una volta mostra la data e premuto ulteriori volte mostra il messaggio iconico dei titoli degli album tipo DM 81 89
// Tasto LUMA: consente di recolare la luminosità.
// 
// Tenendo premuto il tasto MODE durante l'accensione verranno cancellate le impostazioni WIFI
//
//
// MODE button: press once to display the date and press again to display the iconic message of album titles such as DM 81 89.
// LUMA button: allows you to adjust the brightness.
//
// Holding down the MODE button while turning on the device will clear the Wi-Fi settings.
//
//


#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>

// ========================================
// CONFIGURAZIONE HARDWARE / HARDWARE CONFIGURATION
// ========================================

// Pin I2C / I2C pins
const int SDA_PIN = 9;
const int SCL_PIN = 10;

// Display a 6 cifre / 6-digit display
Adafruit_LEDBackpack matrix = Adafruit_LEDBackpack();

// ========================================
// CONFIGURAZIONE NTP / NTP CONFIGURATION
// ========================================

// Server NTP / NTP server
const char* ntpServer = "pool.ntp.org";

// Timezone con ora legale automatica (formato POSIX) / Timezone with automatic DST (POSIX format)
String timezoneStr = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // Roma/Italia / Rome/Italy

// ========================================
// PIN PULSANTI / BUTTON PINS
// ========================================

#define PIN_MODE 3      // Pulsante MODE / MODE button
#define PIN_FUNCTION 4  // Pulsante FUNCTION / FUNCTION button

// ========================================
// VARIABILI GLOBALI / GLOBAL VARIABLES
// ========================================

Preferences preferences;  // Memoria persistente / Persistent storage
WebServer server(80);     // Server web / Web server
int brightness = 7;       // Luminosità display (0-15) / Display brightness (0-15)
int displayMode = 0;      // Modalità display: 0=ora, 1=data, 2-4=messaggi / Display mode: 0=time, 1=date, 2-4=messages
unsigned long modeChangeTime = 0;  // Timestamp cambio modalità / Mode change timestamp
int animationStep = 0;    // Step animazione connessione / Connection animation step
unsigned long lastAnimationUpdate = 0;  // Ultimo aggiornamento animazione / Last animation update

// Variabili debouncing pulsanti / Button debouncing variables
unsigned long lastModePress = 0;
unsigned long lastFunctionPress = 0;
const unsigned long debounceDelay = 200;

// ========================================
// TABELLA CARATTERI 7 SEGMENTI / 7-SEGMENT CHARACTER TABLE
// ========================================

// Tabella conversione numeri 0-9 in formato 7 segmenti (PGFEDCBA)
// Conversion table for digits 0-9 to 7-segment format (PGFEDCBA)
static const uint8_t numbertable[] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// ========================================
// SETUP
// ========================================

void setup() {
  Serial.begin(115200);
  
  // Inizializza I2C / Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Inizializza display / Initialize display
  matrix.begin(0x70);
  
  // Inizializza memoria persistente / Initialize persistent storage
  preferences.begin("clock", false);
  brightness = preferences.getInt("brightness", 7);
  matrix.setBrightness(brightness);
  
  // Configura pulsanti con pull-up / Configure buttons with pull-up
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_FUNCTION, INPUT_PULLUP);
  
  // Controlla se MODE è premuto all'avvio per reset WiFi / Check if MODE is pressed at startup for WiFi reset
  if (digitalRead(PIN_MODE) == LOW) {
    Serial.println("Reset WiFi richiesto! / WiFi reset requested!");
    showResetMessage();
    delay(2000);
    
    WiFiManager wm;
    wm.resetSettings();
    preferences.clear();
    
    // Mostra "OK" sul display / Show "OK" on display
    matrix.clear();
    matrix.displaybuffer[0] = 0b00111111; // O
    matrix.displaybuffer[1] = 0b01010111; // K
    matrix.writeDisplay();
    delay(2000);
    
    ESP.restart();
  }
  
  // Mostra "CONN" durante connessione / Show "CONN" during connection
  showConnMessage();  
  
  // Configura WiFiManager / Configure WiFiManager
  WiFiManager wm;
  
  // Callback per salvataggio configurazione / Callback for configuration save
  wm.setSaveConfigCallback([]() {
    Serial.println("Configurazione salvata, riavvio... / Configuration saved, restarting...");
  });
  
  // Callback per modalità AP / Callback for AP mode
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("Modalità AP attiva / AP mode active");
    showWiFiMessage();
  });
  
  // Timeout portale configurazione / Configuration portal timeout
  wm.setConfigPortalTimeout(180); // 3 minuti / 3 minutes
  
  // Connessione WiFi con portale captive (senza password) / WiFi connection with captive portal (no password)
  bool res = wm.autoConnect("DM-CLOCK");
  
  if (!res) {
    Serial.println("Connessione fallita! / Connection failed!");
    showWiFiMessage();
    delay(3000);
    ESP.restart();
  }
  
  // Verifica connessione effettiva / Verify actual connection
  Serial.println("WiFi configurato, controllo connessione... / WiFi configured, checking connection...");
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    animateConnecting();
    delay(500);
    retries++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connessione WiFi fallita dopo configurazione, riavvio... / WiFi connection failed after configuration, restarting...");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("\nWiFi connesso! / WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Mostra indirizzo IP sul display / Show IP address on display
  showIPAddress();
  
  // Carica timezone dalle preferenze / Load timezone from preferences
  timezoneStr = preferences.getString("timezone", "CET-1CEST,M3.5.0/2,M10.5.0/3");
  
  // Configura NTP con timezone (ora legale automatica) / Configure NTP with timezone (automatic DST)
  configTzTime(timezoneStr.c_str(), ntpServer);
  
  // Attendi sincronizzazione NTP / Wait for NTP synchronization
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 30) {
    Serial.println("Attendo sincronizzazione NTP... / Waiting for NTP sync...");
    animateConnecting();
    delay(1000);
    attempts++;
  }
  
  // Avvia server web / Start web server
  setupWebServer();
  server.begin();
  
  Serial.println("Orologio avviato! / Clock started!");
}

// ========================================
// LOOP PRINCIPALE / MAIN LOOP
// ========================================

void loop() {
  // Gestione richieste web / Handle web requests
  server.handleClient();
  
  // Gestione pulsante MODE / Handle MODE button
  if (digitalRead(PIN_MODE) == LOW && (millis() - lastModePress > debounceDelay)) {
    lastModePress = millis();
    cycleDisplayMode();
  }
  
  // Gestione pulsante FUNCTION (luminosità) / Handle FUNCTION button (brightness)
  if (digitalRead(PIN_FUNCTION) == LOW && (millis() - lastFunctionPress > debounceDelay)) {
    lastFunctionPress = millis();
    adjustBrightness();
  }
  
  // Timeout automatico per modalità diverse dall'ora / Automatic timeout for non-time modes
  unsigned long timeout = (displayMode == 1) ? 5000 : 10000; // 5 sec per data, 10 sec per messaggi / 5 sec for date, 10 sec for messages
  if (displayMode != 0 && (millis() - modeChangeTime > timeout)) {
    displayMode = 0; // Torna all'ora / Return to time
  }
  
  // Visualizzazione in base alla modalità / Display based on mode
  switch (displayMode) {
    case 0:
      displayTime();      // Ora / Time
      break;
    case 1:
      displayDate();      // Data / Date
      break;
    case 2:
      displayMessage1();  // DM8185
      break;
    case 3:
      displayMessage2();  // DM8698
      break;
    case 4:
      displayMessage3();  // DM0117
      break;
    case 5:
      displayMessage4();  // DM0124
      break;
  }
  
  delay(100);
}

// ========================================
// FUNZIONI MESSAGGI DISPLAY / DISPLAY MESSAGE FUNCTIONS
// ========================================

//   A
// F   B
//   G
// E   C
//   D   P
// Mappa segmenti display 7-seg / 7-segment display map

// Mostra "WIFI" sul display / Show "WIFI" on display
void showWiFiMessage() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01111110; // W
  matrix.displaybuffer[1] = 0b00000110; // I
  matrix.displaybuffer[2] = 0b01110001; // F
  matrix.displaybuffer[3] = 0b00000110; // I
  matrix.writeDisplay();
}

// Mostra "CONN" (connessione) sul display / Show "CONN" (connecting) on display
void showConnMessage() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b00111001; // C
  matrix.displaybuffer[1] = 0b00111111; // O
  matrix.displaybuffer[2] = 0b01010100; // N
  matrix.displaybuffer[3] = 0b01010100; // N
  matrix.writeDisplay();
}

// Mostra "RESET" (reset) sul display / Show "RESET" (reset) on display
void showResetMessage() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01010000; // R
  matrix.displaybuffer[1] = 0b01111001; // E
  matrix.displaybuffer[2] = 0b01101101; // S
  matrix.displaybuffer[3] = 0b01111001; // E
  matrix.displaybuffer[4] = 0b01111000; // T
  matrix.writeDisplay();
}

// Mostra indirizzo IP completo segmentato con punti / Show complete IP address segmented with dots
void showIPAddress() {
  IPAddress ip = WiFi.localIP();
  
  // Mostra "IP=" / Show "IP="
  matrix.clear();
  matrix.displaybuffer[0] = 0b00000110; // I
  matrix.displaybuffer[1] = 0b01110011; // P
  matrix.displaybuffer[2] = 0b00001001; // =
  matrix.writeDisplay();
  delay(1000);
  
  // Mostra primo ottetto con punto (es. 192.) / Show first octet with dot (e.g. 192.)
  displayNumberWithDot(ip[0], true);
  delay(1000);
  
  // Mostra secondo ottetto con punto (es. 168.) / Show second octet with dot (e.g. 168.)
  displayNumberWithDot(ip[1], true);
  delay(1000);
  
  // Mostra terzo ottetto con punto (es. 1.) / Show third octet with dot (e.g. 1.)
  displayNumberWithDot(ip[2], true);
  delay(1000);
  
  // Mostra quarto ottetto senza punto (es. 98) / Show fourth octet without dot (e.g. 98)
  displayNumberWithDot(ip[3], false);
  delay(1000);
  matrix.clear();
  delay(500);
}

// Funzione helper per visualizzare un numero allineato a destra con punto opzionale / Helper function to display right-aligned number with optional dot
void displayNumberWithDot(int num, bool showDot) {
  matrix.clear();
  
  if (num >= 100) {
    // 3 cifre / 3 digits
    matrix.displaybuffer[3] = numbertable[num / 100];
    matrix.displaybuffer[4] = numbertable[(num / 10) % 10];
    matrix.displaybuffer[5] = numbertable[num % 10];
    if (showDot) {
      matrix.displaybuffer[5] |= 0b10000000; // Aggiungi punto decimale / Add decimal point
    }
  } else if (num >= 10) {
    // 2 cifre / 2 digits
    matrix.displaybuffer[4] = numbertable[num / 10];
    matrix.displaybuffer[5] = numbertable[num % 10];
    if (showDot) {
      matrix.displaybuffer[5] |= 0b10000000; // Aggiungi punto decimale / Add decimal point
    }
  } else {
    // 1 cifra / 1 digit
    matrix.displaybuffer[5] = numbertable[num];
    if (showDot) {
      matrix.displaybuffer[5] |= 0b10000000; // Aggiungi punto decimale / Add decimal point
    }
  }
  
  matrix.writeDisplay();
}



// Animazione durante connessione WiFi / Animation during WiFi connection
void animateConnecting() {
  matrix.clear();
  
  // Anima punti centrali / Animate center dots
  if (animationStep == 0) {
    matrix.displaybuffer[2] = 0b10000000;
    matrix.displaybuffer[3] = 0b10000000;
  } else {
    matrix.displaybuffer[2] = 0b00001001;
    matrix.displaybuffer[3] = 0b00001001;
  }
  
  matrix.writeDisplay();
  animationStep = (animationStep + 1) % 2;
}

// ========================================
// FUNZIONI VISUALIZZAZIONE / DISPLAY FUNCTIONS
// ========================================

// Visualizza ora in formato HH:MM:SS / Display time in HH:MM:SS format
void displayTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }
  
  int hours = timeinfo.tm_hour;
  int minutes = timeinfo.tm_min;
  int seconds = timeinfo.tm_sec;
  
  matrix.clear();
  
  // Visualizza HH:MM:SS sui 6 digit / Display HH:MM:SS on 6 digits
  matrix.displaybuffer[0] = numbertable[hours / 10];
  matrix.displaybuffer[1] = numbertable[hours % 10];
  matrix.displaybuffer[2] = numbertable[minutes / 10];
  matrix.displaybuffer[3] = numbertable[minutes % 10];
  matrix.displaybuffer[4] = numbertable[seconds / 10];
  matrix.displaybuffer[5] = numbertable[seconds % 10];
  
  matrix.writeDisplay();
}

// Visualizza data in formato GG:MM:AA / Display date in DD:MM:YY format
void displayDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }
  
  int day = timeinfo.tm_mday;
  int month = timeinfo.tm_mon + 1;
  int year = (timeinfo.tm_year + 1900) % 100;
  
  matrix.clear();
  
  // Visualizza GG:MM:AA / Display DD:MM:YY
  matrix.displaybuffer[0] = numbertable[day / 10];
  matrix.displaybuffer[1] = numbertable[day % 10];
  matrix.displaybuffer[2] = numbertable[month / 10];
  matrix.displaybuffer[3] = numbertable[month % 10];
  matrix.displaybuffer[4] = numbertable[year / 10];
  matrix.displaybuffer[5] = numbertable[year % 10];
  
  matrix.writeDisplay();
  delay(100);
}

// Cicla tra le modalità di visualizzazione / Cycle through display modes
void cycleDisplayMode() {
  displayMode = (displayMode + 1) % 6; // Cicla da 0 a 5 / Cycle from 0 to 5
  modeChangeTime = millis();
}

// Visualizza messaggio "DM8185" / Display message "DM8185"
void displayMessage1() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01011110; // D
  matrix.displaybuffer[1] = 0b11010100; // M
  matrix.displaybuffer[2] = numbertable[8];
  matrix.displaybuffer[3] = numbertable[1];
  matrix.displaybuffer[4] = numbertable[8];
  matrix.displaybuffer[5] = numbertable[5];
  matrix.writeDisplay();
  delay(100);
}

// Visualizza messaggio "DM8698" / Display message "DM8698"
void displayMessage2() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01011110; // D
  matrix.displaybuffer[1] = 0b11010100; // M
  matrix.displaybuffer[2] = numbertable[8];
  matrix.displaybuffer[3] = numbertable[6];
  matrix.displaybuffer[4] = numbertable[9];
  matrix.displaybuffer[5] = numbertable[8];
  matrix.writeDisplay();
  delay(100);
}

// Visualizza messaggio "DM0117" / Display message "DM0117"
void displayMessage3() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01011110; // D
  matrix.displaybuffer[1] = 0b11010100; // M
  matrix.displaybuffer[2] = numbertable[0];
  matrix.displaybuffer[3] = numbertable[1];
  matrix.displaybuffer[4] = numbertable[1];
  matrix.displaybuffer[5] = numbertable[7];
  matrix.writeDisplay();
  delay(100);
}

// Visualizza messaggio "DM0124" / Display message "DM0124"
void displayMessage4() {
  matrix.clear();
  matrix.displaybuffer[0] = 0b01011110; // D
  matrix.displaybuffer[1] = 0b11010100; // M
  matrix.displaybuffer[2] = numbertable[0];
  matrix.displaybuffer[3] = numbertable[1];
  matrix.displaybuffer[4] = numbertable[2];
  matrix.displaybuffer[5] = numbertable[4];
  matrix.writeDisplay();
  delay(100);
}


// ========================================
// FUNZIONE REGOLAZIONE LUMINOSITÀ / BRIGHTNESS ADJUSTMENT FUNCTION
// ========================================

// Regola luminosità display (0-15) / Adjust display brightness (0-15)
void adjustBrightness() {
  brightness = (brightness + 1) % 16;
  matrix.setBrightness(brightness);
  preferences.putInt("brightness", brightness);
  
  // Mostra "LUM" + valore luminosità / Show "LUM" + brightness value
  matrix.clear();
  matrix.displaybuffer[0] = 0b00111000; // L
  matrix.displaybuffer[1] = 0b00111110; // U
  matrix.displaybuffer[2] = 0b11010100; // M
  
  // Visualizza valore allineato a destra / Display value right-aligned
  if (brightness >= 10) {
    matrix.displaybuffer[4] = numbertable[1]; // 1
    matrix.displaybuffer[5] = numbertable[brightness - 10];
  } else {
    matrix.displaybuffer[5] = numbertable[brightness];
  }
  
  matrix.writeDisplay();
  delay(500);
}

// ========================================
// SERVER WEB / WEB SERVER
// ========================================

void setupWebServer() {
  // Pagina principale configurazione / Main configuration page
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Configurazione DM-CLOCK / DM-CLOCK Configuration</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
    html += ".container{max-width:500px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;text-align:center;}";
    html += ".info{background:#e3f2fd;padding:15px;border-radius:5px;margin-bottom:20px;}";
    html += ".info p{margin:5px 0;color:#1976d2;}";
    html += "label{display:block;margin:10px 0 5px;font-weight:bold;}";
    html += "input,select{width:100%;padding:8px;margin-bottom:15px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;}";
    html += "button{width:100%;padding:10px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin-top:10px;}";
    html += "button:hover{background:#45a049;}";
    html += ".reset-btn{background:#f44336;}";
    html += ".reset-btn:hover{background:#da190b;}";
    html += ".brightness-slider{width:100%;}</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>⏰ Configurazione Orologio / Clock Configuration</h1>";
    html += "<div class='info'>";
    html += "<p><strong>SSID:</strong> " + WiFi.SSID() + "</p>";
    html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>Luminosità / Brightness:</strong> " + String(brightness) + "/15</p>";
    html += "<p><strong>Timezone:</strong> " + timezoneStr + "</p>";
    html += "</div>";
    html += "<form action='/save' method='POST'>";
    html += "<label>Timezone:</label>";
    html += "<select name='timezone'>";
    html += "<option value='CET-1CEST,M3.5.0/2,M10.5.0/3'" + String(timezoneStr == "CET-1CEST,M3.5.0/2,M10.5.0/3" ? " selected" : "") + ">Europa/Roma (CET)</option>";
    html += "<option value='GMT0BST,M3.5.0/1,M10.5.0'" + String(timezoneStr == "GMT0BST,M3.5.0/1,M10.5.0" ? " selected" : "") + ">Europa/Londra (GMT)</option>";
    html += "<option value='EET-2EEST,M3.5.0/3,M10.5.0/4'" + String(timezoneStr == "EET-2EEST,M3.5.0/3,M10.5.0/4" ? " selected" : "") + ">Europa/Atene (EET)</option>";
    html += "<option value='EST5EDT,M3.2.0,M11.1.0'" + String(timezoneStr == "EST5EDT,M3.2.0,M11.1.0" ? " selected" : "") + ">America/New York (EST)</option>";
    html += "<option value='PST8PDT,M3.2.0,M11.1.0'" + String(timezoneStr == "PST8PDT,M3.2.0,M11.1.0" ? " selected" : "") + ">America/Los Angeles (PST)</option>";
    html += "<option value='AEST-10AEDT,M10.1.0,M4.1.0/3'" + String(timezoneStr == "AEST-10AEDT,M10.1.0,M4.1.0/3" ? " selected" : "") + ">Australia/Sydney (AEST)</option>";
    html += "</select>";
    html += "<label>Server NTP:</label>";
    html += "<input type='text' name='ntpServer' value='" + String(ntpServer) + "'>";
    html += "<label>Luminosità Display / Display Brightness (0-15): <span id='brightVal'>" + String(brightness) + "</span></label>";
    html += "<input type='range' name='brightness' min='0' max='15' value='" + String(brightness) + "' class='brightness-slider' ";
    html += "oninput=\"document.getElementById('brightVal').textContent=this.value\">";
    html += "<button type='submit'>Salva Configurazione / Save Configuration</button>";
    html += "</form>";
    html += "<form action='/resetwifi' method='POST' style='margin-top:20px;'>";
    html += "<button type='submit' class='reset-btn'>Reset WiFi</button>";
    html += "</form>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });
  
  // Endpoint salvataggio configurazione / Configuration save endpoint
  server.on("/save", HTTP_POST, []() {
    bool needRestart = false;
    
    // Salva timezone / Save timezone
    if (server.hasArg("timezone")) {
      String newTimezone = server.arg("timezone");
      if (newTimezone != timezoneStr) {
        preferences.putString("timezone", newTimezone);
        timezoneStr = newTimezone;
        needRestart = true;
      }
    }
    
    // Salva server NTP / Save NTP server
    if (server.hasArg("ntpServer")) {
      preferences.putString("ntpServer", server.arg("ntpServer"));
      needRestart = true;
    }
    
    // Salva luminosità / Save brightness
    if (server.hasArg("brightness")) {
      brightness = server.arg("brightness").toInt();
      preferences.putInt("brightness", brightness);
      matrix.setBrightness(brightness);
    }
    
    // Pagina conferma / Confirmation page
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    if (needRestart) {
      html += "<meta http-equiv='refresh' content='3;url=/'>";
    } else {
      html += "<meta http-equiv='refresh' content='2;url=/'>";
    }
    html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0;}";
    html += ".message{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);display:inline-block;}";
    html += "h2{color:#4CAF50;}";
    html += "p{color:#666;}</style></head><body>";
    html += "<div class='message'><h2>✓ Configurazione Salvata / Configuration Saved</h2>";
    if (needRestart) {
      html += "<p>Riavvio in corso... / Restarting...</p></div></body></html>";
    } else {
      html += "<p>Aggiornamento completato / Update completed</p></div></body></html>";
    }
    server.send(200, "text/html", html);
    
    // Riavvia se necessario / Restart if needed
    if (needRestart) {
      delay(2000);
      ESP.restart();
    }
  });
  
  // Endpoint reset WiFi / WiFi reset endpoint
  server.on("/resetwifi", HTTP_POST, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0;}";
    html += ".message{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);display:inline-block;}";
    html += "h2{color:#f44336;}";
    html += "p{color:#666;}</style></head><body>";
    html += "<div class='message'><h2>⚠ Reset WiFi</h2>";
    html += "<p>Impostazioni WiFi cancellate. Riavvio... / WiFi settings cleared. Restarting...</p></div></body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
  });
}