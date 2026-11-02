/*
 * ESP-01 Novuton Shield - Versao 5 (Relay via Serial)
 * Portal de configuracao proprio (sem WiFiManager)
 * MQTT + HA Discovery + Web Interface
 *
 * Hardware: ESP8266-01 (512KB ou 1MB) + Novuton Relay Shield
 *   - TX: GPIO1 (Serial para Nuvoton - 115200 baud)
 *   - RX: GPIO3 (liberado para outros usos)
 *   - Serial TX_ONLY para nao interferir com boot
 *
 * Novuton Shield:
 *   - 2 reles controlados via serial (Nuvoton MCU)
 *   - Comandos: A0 01 00 A1 (rele 1 OFF), A0 01 01 A2 (rele 1 ON)
 *               A0 02 00 A2 (rele 2 OFF), A0 02 01 A3 (rele 2 ON)
 *   - Baud rate: 115200
 *   - Precisa de inicializacao: "WIFI CONNECTED\nWIFI GOT IP\n..."
 *   - LED D6 pisca quando comandos funcionam
 *
 * Bibliotecas necessarias: apenas PubSubClient
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <EEPROM.h>

// ============================================================
// PINOS
// ============================================================
// Novuton Shield usa serial para controlar reles
// GPIO0 e GPIO2 livres (nao usados para reles)
// TX (GPIO1) -> Nuvoton RX
// RX (GPIO3) -> Nuvoton TX (nao usado, SERIAL_TX_ONLY)

// ============================================================
// EEPROM
// ============================================================
#define EEPROM_SIZE 512
#define CFG_MAGIC   0xA5

struct Config {
  uint8_t  magic;
  char     ssid[33];
  char     pass[65];
  char     mqttServer[41];
  uint16_t mqttPort;
  char     mqttUser[21];
  char     mqttPass[21];
};

Config config;

// ============================================================
// TOPICOS FIXOS
// ============================================================
const char TOPIC_R1_SET[]   = "esp01/novuton/relay1/set";
const char TOPIC_R1_STATE[] = "esp01/novuton/relay1/state";
const char TOPIC_R2_SET[]   = "esp01/novuton/relay2/set";
const char TOPIC_R2_STATE[] = "esp01/novuton/relay2/state";
const char TOPIC_AVAIL[]    = "esp01/novuton/availability";

// Discovery topics
const char DISC_R1[] = "homeassistant/switch/ESP01_Novuton_R1/config";
const char DISC_R2[] = "homeassistant/switch/ESP01_Novuton_R2/config";

// ============================================================
// INSTANCIAS
// ============================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer webServer(80);

bool relay1State = false;
bool relay2State = false;
bool discoveryPublished = false;
bool nuvotonInitialized = false;

// ============================================================
// HTML EM PROGMEM
// ============================================================
static const char HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP-01 Novuton</title><style>
body{font-family:Arial;max-width:400px;margin:20px auto;padding:10px;text-align:center;background:#f4f4f4}
h1,h2{color:#333}.box{border:1px solid #ccc;border-radius:8px;padding:15px;margin:15px 0;background:#fff}
.on{color:green;font-weight:bold}.off{color:red;font-weight:bold}
.btn{display:inline-block;padding:10px 20px;margin:5px;border-radius:5px;color:#fff;font-weight:bold;text-decoration:none}
.bon{background:#4CAF50}.boff{background:#f44336}
input{display:block;width:100%;padding:8px;margin:6px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px}
button{background:#2196F3;color:#fff;padding:10px 20px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;width:100%;margin-top:10px}
a{text-decoration:none}
.shield{background:#e8f5e9;border:1px solid #4CAF50;border-radius:8px;padding:10px;margin:10px 0;font-size:12px;color:#2e7d32}
</style></head><body>
)rawliteral";

static const char HTML_FOOT[] PROGMEM = "</body></html>";

// ============================================================
// PROTOTIPOS
// ============================================================
void loadConfig();
void saveConfig();
void startConfigPortal();
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishDiscovery();
void setupWebServer();
void handleRoot();
void handleR1On();
void handleR1Off();
void handleR2On();
void handleR2Off();
void handleReset();
void initNuvoton();
void relayCommand(byte relay, byte state);

// ============================================================
// SETUP
// ============================================================
void setup() {
  // Usar SERIAL_TX_ONLY para liberar RX (GPIO3) para outros usos
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(200);
  Serial.println(F("\n--- ESP-01 Boot (Novuton Shield) ---"));

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  Serial.print(F("Heap inicial: "));
  Serial.println(ESP.getFreeHeap());

  if (config.magic != CFG_MAGIC || strlen(config.ssid) == 0) {
    Serial.println(F("Sem config. Iniciando portal..."));
    startConfigPortal();
    return;
  }

  connectWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi falhou. Iniciando portal..."));
    startConfigPortal();
    return;
  }

  mqttClient.setServer(config.mqttServer, config.mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);
  connectMQTT();

  setupWebServer();

  // Inicializar Nuvoton (aguardar 5s + enviar string de init)
  initNuvoton();

  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("Heap final setup: "));
  Serial.println(ESP.getFreeHeap());
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  webServer.handleClient();

  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
    connectMQTT();
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  yield();
}

// ============================================================
// NOVUTON - INICIALIZACAO E COMANDOS
// ============================================================
void initNuvoton() {
  Serial.println(F("[NUVOTON] Aguardando 5s para inicializacao..."));
  delay(5000);

  // String de inicializacao para o Nuvoton
  char nuvotonInit[] = "WIFI CONNECTED\nWIFI GOT IP\nAT+CIPMUX=1\nAT+CIPSERVER=1,8080\nAT+CIPSTO=360\n";
  Serial.write(nuvotonInit, sizeof(nuvotonInit) - 1);

  Serial.println(F("[NUVOTON] String de init enviada. Aguardando ~10s..."));
  delay(10000);

  nuvotonInitialized = true;
  Serial.println(F("[NUVOTON] Inicializado. Comandos prontos."));
}

void relayCommand(byte relay, byte state) {
  // Comando: A0 [relay] [state] [checksum]
  // relay: 1 ou 2
  // state: 0 = OFF, 1 = ON
  // checksum: A0 + relay + state
  byte msg = 0xa0;
  Serial.write(msg);
  Serial.write(relay);
  Serial.write(state);
  Serial.write(msg + relay + state);
}

// ============================================================
// EEPROM
// ============================================================
void loadConfig() {
  EEPROM.get(0, config);
  if (config.magic != CFG_MAGIC) {
    memset(&config, 0, sizeof(config));
    config.magic = CFG_MAGIC;
    config.mqttPort = 1883;
    strncpy(config.mqttServer, "192.168.1.100", sizeof(config.mqttServer) - 1);
  }
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

// ============================================================
// PORTAL DE CONFIGURACAO
// ============================================================
void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP01_Config");

  Serial.print(F("AP: ESP01_Config  IP: "));
  Serial.println(WiFi.softAPIP());

  webServer.on("/", HTTP_GET, []() {
    String html;
    html.reserve(1800);
    html += FPSTR(HTML_HEAD);
    html += F("<h1>Configuracao ESP-01 (Novuton)</h1><div class='box'><form action='/save' method='POST'>");
    html += F("<label>WiFi SSID</label><input name='s' maxlength='32' value='");
    html += config.ssid;
    html += F("'>");
    html += F("<label>WiFi Senha</label><input name='p' type='password' maxlength='64'>");
    html += F("<label>MQTT Servidor</label><input name='ms' maxlength='40' value='");
    html += config.mqttServer;
    html += F("'>");
    html += F("<label>MQTT Porta</label><input name='mp' type='number' value='");
    html += String(config.mqttPort);
    html += F("'>");
    html += F("<label>MQTT Usuario (opcional)</label><input name='mu' maxlength='20' value='");
    html += config.mqttUser;
    html += F("'>");
    html += F("<label>MQTT Senha (opcional)</label><input name='mpw' type='password' maxlength='20'>");
    html += F("<button type='submit'>SALVAR E REINICIAR</button>");
    html += F("</form></div>");
    html += FPSTR(HTML_FOOT);
    webServer.send(200, "text/html", html);
  });

  webServer.on("/save", HTTP_POST, []() {
    if (webServer.hasArg("s")) {
      strncpy(config.ssid, webServer.arg("s").c_str(), sizeof(config.ssid) - 1);
      config.ssid[sizeof(config.ssid) - 1] = '\0';
    }
    if (webServer.hasArg("p") && webServer.arg("p").length() > 0) {
      strncpy(config.pass, webServer.arg("p").c_str(), sizeof(config.pass) - 1);
      config.pass[sizeof(config.pass) - 1] = '\0';
    }
    if (webServer.hasArg("ms")) {
      strncpy(config.mqttServer, webServer.arg("ms").c_str(), sizeof(config.mqttServer) - 1);
      config.mqttServer[sizeof(config.mqttServer) - 1] = '\0';
    }
    if (webServer.hasArg("mp")) {
      config.mqttPort = (uint16_t)webServer.arg("mp").toInt();
      if (config.mqttPort == 0) config.mqttPort = 1883;
    }
    if (webServer.hasArg("mu")) {
      strncpy(config.mqttUser, webServer.arg("mu").c_str(), sizeof(config.mqttUser) - 1);
      config.mqttUser[sizeof(config.mqttUser) - 1] = '\0';
    }
    if (webServer.hasArg("mpw") && webServer.arg("mpw").length() > 0) {
      strncpy(config.mqttPass, webServer.arg("mpw").c_str(), sizeof(config.mqttPass) - 1);
      config.mqttPass[sizeof(config.mqttPass) - 1] = '\0';
    }

    config.magic = CFG_MAGIC;
    saveConfig();

    webServer.send(200, "text/html",
      F("<!DOCTYPE html><html><head><meta charset='UTF-8'></head>"
        "<body style='font-family:Arial;text-align:center;padding:40px'>"
        "<h2>Salvo!</h2><p>Reiniciando...</p></body></html>"));
    delay(1500);
    ESP.restart();
  });

  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/plain", "");
  });

  webServer.begin();

  while (true) {
    webServer.handleClient();
    yield();
    delay(2);
  }
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {
  Serial.print(F("Conectando WiFi: "));
  Serial.println(config.ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi OK. IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi timeout"));
  }
}

// ============================================================
// MQTT
// ============================================================
void connectMQTT() {
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();

  Serial.print(F("Heap antes MQTT: "));
  Serial.println(ESP.getFreeHeap());

  String clientId = "ESP01-";
  clientId += String(ESP.getChipId(), HEX);

  bool ok;
  if (strlen(config.mqttUser) > 0) {
    ok = mqttClient.connect(clientId.c_str(),
                            config.mqttUser, config.mqttPass,
                            TOPIC_AVAIL, 0, true, "offline");
  } else {
    ok = mqttClient.connect(clientId.c_str(),
                            nullptr, nullptr,
                            TOPIC_AVAIL, 0, true, "offline");
  }

  if (ok) {
    Serial.println(F("MQTT OK"));
    mqttClient.publish(TOPIC_AVAIL, "online", true);

    if (!discoveryPublished) {
      publishDiscovery();
    }

    mqttClient.subscribe(TOPIC_R1_SET);
    mqttClient.subscribe(TOPIC_R2_SET);

    mqttClient.publish(TOPIC_R1_STATE, relay1State ? "ON" : "OFF", true);
    mqttClient.publish(TOPIC_R2_STATE, relay2State ? "ON" : "OFF", true);
  } else {
    Serial.print(F("MQTT falha rc="));
    Serial.println(mqttClient.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[8];
  uint8_t len = (length < 7) ? length : 7;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strcmp(topic, TOPIC_R1_SET) == 0) {
    if (strcmp(msg, "ON") == 0) {
      relayCommand(1, 1);
      relay1State = true;
      mqttClient.publish(TOPIC_R1_STATE, "ON", true);
    } else if (strcmp(msg, "OFF") == 0) {
      relayCommand(1, 0);
      relay1State = false;
      mqttClient.publish(TOPIC_R1_STATE, "OFF", true);
    }
  } else if (strcmp(topic, TOPIC_R2_SET) == 0) {
    if (strcmp(msg, "ON") == 0) {
      relayCommand(2, 1);
      relay2State = true;
      mqttClient.publish(TOPIC_R2_STATE, "ON", true);
    } else if (strcmp(msg, "OFF") == 0) {
      relayCommand(2, 0);
      relay2State = false;
      mqttClient.publish(TOPIC_R2_STATE, "OFF", true);
    }
  }
}

// ============================================================
// DISCOVERY - HA AUTO-DESCOBERTA
// ============================================================
void publishDiscovery() {
  char buf[512];

  // Rele 1
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Rele 1\",\"uniq_id\":\"ESP01_Novuton_R1\","
    "\"stat_t\":\"esp01/novuton/relay1/state\","
    "\"cmd_t\":\"esp01/novuton/relay1/set\","
    "\"avty_t\":\"esp01/novuton/availability\","
    "\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
    "\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"ret\":true,"
    "\"dev\":{\"ids\":[\"ESP01_Novuton\"],"
    "\"name\":\"ESP-01 Novuton Shield\","
    "\"mf\":\"Novuton\",\"mdl\":\"ESP-01 Relay Shield\",\"sw\":\"5.0\"}}");
  bool ok1 = mqttClient.publish(DISC_R1, buf, true);
  Serial.print(F("Discovery R1: "));
  Serial.println(ok1 ? F("OK") : F("FALHOU"));

  // Rele 2
  snprintf(buf, sizeof(buf),
    "{\"name\":\"Rele 2\",\"uniq_id\":\"ESP01_Novuton_R2\","
    "\"stat_t\":\"esp01/novuton/relay2/state\","
    "\"cmd_t\":\"esp01/novuton/relay2/set\","
    "\"avty_t\":\"esp01/novuton/availability\","
    "\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
    "\"stat_on\":\"ON\",\"stat_off\":\"OFF\",\"ret\":true,"
    "\"dev\":{\"ids\":[\"ESP01_Novuton\"],"
    "\"name\":\"ESP-01 Novuton Shield\","
    "\"mf\":\"Novuton\",\"mdl\":\"ESP-01 Relay Shield\",\"sw\":\"5.0\"}}");
  bool ok2 = mqttClient.publish(DISC_R2, buf, true);
  Serial.print(F("Discovery R2: "));
  Serial.println(ok2 ? F("OK") : F("FALHOU"));

  discoveryPublished = true;
}

// ============================================================
// SERVIDOR WEB
// ============================================================
void setupWebServer() {
  webServer.on("/",        handleRoot);
  webServer.on("/r1on",    handleR1On);
  webServer.on("/r1off",   handleR1Off);
  webServer.on("/r2on",    handleR2On);
  webServer.on("/r2off",   handleR2Off);
  webServer.on("/reset",   handleReset);
  webServer.begin();
  Serial.println(F("Web OK"));
}

void sendMainPage() {
  String html;
  html.reserve(1200);

  html += FPSTR(HTML_HEAD);
  html += F("<h1>ESP-01 Novuton Shield</h1>");
  html += F("<div class='shield'>Novuton Relay Shield - 2 Reles via Serial</div>");

  // Rele 1
  html += F("<div class='box'><h2>Rele 1</h2><p>Status: <span class='");
  html += relay1State ? F("on'>LIGADO") : F("off'>DESLIGADO");
  html += F("</span></p>");
  html += F("<a href='/r1on' class='btn bon'>LIGAR</a>");
  html += F("<a href='/r1off' class='btn boff'>DESLIGAR</a></div>");

  // Rele 2
  html += F("<div class='box'><h2>Rele 2</h2><p>Status: <span class='");
  html += relay2State ? F("on'>LIGADO") : F("off'>DESLIGADO");
  html += F("</span></p>");
  html += F("<a href='/r2on' class='btn bon'>LIGAR</a>");
  html += F("<a href='/r2off' class='btn boff'>DESLIGAR</a></div>");

  html += F("<div class='box'><a href='/reset' style='color:#c00'>Apagar config e reiniciar</a></div>");

  html += FPSTR(HTML_FOOT);

  webServer.send(200, "text/html", html);
}

void handleRoot()  { sendMainPage(); }

void handleR1On()  {
  relayCommand(1, 1);
  relay1State = true;
  if (mqttClient.connected()) mqttClient.publish(TOPIC_R1_STATE, "ON", true);
  sendMainPage();
}
void handleR1Off() {
  relayCommand(1, 0);
  relay1State = false;
  if (mqttClient.connected()) mqttClient.publish(TOPIC_R1_STATE, "OFF", true);
  sendMainPage();
}
void handleR2On()  {
  relayCommand(2, 1);
  relay2State = true;
  if (mqttClient.connected()) mqttClient.publish(TOPIC_R2_STATE, "ON", true);
  sendMainPage();
}
void handleR2Off() {
  relayCommand(2, 0);
  relay2State = false;
  if (mqttClient.connected()) mqttClient.publish(TOPIC_R2_STATE, "OFF", true);
  sendMainPage();
}

void handleReset() {
  config.magic = 0;
  saveConfig();
  webServer.send(200, "text/html",
    F("<!DOCTYPE html><html><body style='font-family:Arial;text-align:center;padding:40px'>"
      "<h2>Config apagada</h2><p>Reiniciando...</p></body></html>"));
  delay(1500);
  ESP.restart();
}