#include <Arduino.h>
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <time.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

// ==================== CONFIG ====================
const char* WIFI_SSID     = "Pretty_fly_for_a_Wi_Fi";
const char* WIFI_PASSWORD = "slick1963";
const char* OTA_HOSTNAME  = "Keg-MATRIX_livingroom";
const char* OTA_PASSWORD  = "slick1963";

// Matrix Pins
#define R1_PIN  25
#define G1_PIN  26
#define B1_PIN  27
#define R2_PIN  14
#define G2_PIN  12
#define B2_PIN  13
#define A_PIN   23
#define B_PIN   19
#define C_PIN    5
#define D_PIN   17
#define E_PIN   18
#define LAT_PIN  4
#define OE_PIN  15
#define CLK_PIN 16

#define PANEL_WIDTH       128
#define PANEL_HEIGHT       64
#define DEFAULT_BRIGHTNESS  60
#define MIN_BRIGHTNESS      10
#define MAX_BRIGHTNESS     255

// MQTT
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "ESP32BEERKEGS_livingroom"
char mqttServer[40]  = "192.168.0.191";
char mqttUser[32]    = "lagerbeer";
char mqttPass[32]    = "Slick1963$";

// Stats topic — JSON with all tap volumes and pour data
#define BEERMONITOR_STATS_TOPIC "beermonitor/stats"

// Keg temp topics and display names — both editable via web
char kegTempTopics[6][128] = {
    "garage/beer/keg_1_temp", "garage/beer/keg_2_temp",
    "garage/beer/keg_3_temp", "garage/beer/keg_4_temp",
    "garage/beer/keg_5_temp", "garage/beer/keg_6_temp"
};
char kegTempNames[6][20] = {
    "Keg 1", "Keg 2", "Keg 3", "Keg 4", "Keg 5", "Keg 6"
};
float         kegTemps[6]        = {0};
bool          kegTempConnected[6]= {false};
unsigned long kegTempLastSeen[6] = {0};

// Custom sensors
#define MAX_CUSTOM_SENSORS 8
struct CustomSensor {
    char  topic[128];
    char  name[16];
    char  unit[8];
    float value;
    bool  connected;
    unsigned long lastSeen;
} customSensors[MAX_CUSTOM_SENSORS];
int numCustomSensors = 0;

// POSIX timezone string — handles DST automatically
#define TZ_STRING           "EST5EDT,M3.2.0,M11.1.0"

// Timing
#define DISPLAY_REFRESH_MS  30
#define MQTT_RECONNECT_MS   10000
#define SENSOR_TIMEOUT      300000   // 5 min
// Page duration is now a runtime variable (set via web config)
#define COLOR_BLACK         0

// ==================== GLOBALS ====================
MatrixPanel_I2S_DMA* display = nullptr;
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
WebServer    server(80);
Preferences  prefs;

uint8_t       brightness    = DEFAULT_BRIGHTNESS;
bool          showDate      = true;
bool          wifiConnected = false;
bool          mqttConnected = false;
bool          timeSynced    = false;
bool          otaInProgress = false;

unsigned long lastDisplayUpdate  = 0;
unsigned long lastMqttReconnect  = 0;
unsigned long lastSensorCheck    = 0;
unsigned long lastPageSwitch     = 0;
unsigned long lastAutoBright     = 0;
uint8_t       colonState         = 1;

// Display pages
enum DisplayPage { PAGE_VOLUMES = 0, PAGE_TEMPS, PAGE_CUSTOM,
                   PAGE_POUR,       // last pour — part of normal rotation
                   PAGE_COUNT };
DisplayPage   currentPage        = PAGE_VOLUMES;
DisplayPage   nextPage           = PAGE_VOLUMES;

// Page display duration — user configurable (seconds, default 8)
unsigned long pageDurationSec = 8;

// Scroll animation state
// scrolling: true while transition is active
// scrollOffset: 0..PANEL_WIDTH (pixels the new page has moved in from the right)
bool          scrolling          = false;
int           scrollOffset       = 0;
unsigned long scrollStart        = 0;
#define SCROLL_DURATION_MS  350   // total ms for one slide

// Keg display names (editable)
char kegNames[6][20] = { "Keg 1","Keg 2","Keg 3","Keg 4","Keg 5","Keg 6" };
float kegMaxVolume   = 5.0f;     // gallon capacity, used for bar graph scale

// Auto-brightness schedule
bool  autoBrightEnabled = false;
uint8_t nightBrightness = 15;    // dim level during night hours
uint8_t dayBrightness   = DEFAULT_BRIGHTNESS;
int   nightStartHour    = 23;    // 11 PM
int   nightEndHour      = 7;     // 7 AM
bool  inNightMode       = false;

// Low-keg alert flash
#define LOW_KEG_THRESHOLD  1.0f  // gallons
#define FLASH_PERIOD_MS    600   // ms per flash half-cycle

// Last pour interrupt
#define LAST_POUR_TOPIC       "garage/beer/last_pour"
#define POUR_DISPLAY_SEC      12      // seconds to show pour page (overridable)
char          pourKegName[32]    = "";
float         pourAmountGal      = 0.0f;
char          pourTime[16]       = "";
bool          pourActive         = false;   // true while pour page is showing
unsigned long pourStartMs        = 0;
unsigned long pourDisplaySec     = POUR_DISPLAY_SEC;
DisplayPage   prePourPage        = PAGE_VOLUMES;  // page to return to after

float         kegVolumes[6]  = {0};
bool          kegConnected[6]= {false};
unsigned long kegLastSeen[6] = {0};
int           prevActiveTap  = -1;
char          tapBeerNames[6][32] = {};

struct tm timeinfo;

// ==================== FORWARD DECLARATIONS ====================
void initDisplay();
void drawTopBar();
void drawPageVolumes();
void drawPageTemps();
void drawPageCustom();
void drawPageIndicator();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void connectMQTT();
void resubscribeAll();
void loadConfig();
void saveConfig();
void setupOTA();
void initWebServer();
void handleStatus();
void addCustomSensor(const char* topic, const char* name, const char* unit);
void removeCustomSensor(int index);
void checkSensorTimeouts();
bool isSensorLive(bool connected, unsigned long lastSeen);
void updateAutoBrightness();
void drawPage(DisplayPage page, int xOffset);
void drawScrollFrame();
void startScroll(DisplayPage to);
void drawPagePour();
void triggerPour(const char* keg, float amount, const char* time);

// ==================== HELPERS ====================
bool isSensorLive(bool connected, unsigned long lastSeen) {
    return connected && (millis() - lastSeen < SENSOR_TIMEOUT);
}

uint16_t volumeColor(float vol, bool live) {
    if (!live)        return display->color565(70, 70, 70);
    if (vol >= 4.0f)  return display->color565(0, 255, 0);
    if (vol >= 2.5f)  return display->color565(0, 220, 255);
    if (vol >= 1.0f)  return display->color565(255, 165, 0);
    return                   display->color565(255, 40, 40);
}

// ==================== DISPLAY ====================
void initDisplay() {
    HUB75_I2S_CFG::i2s_pins pins = {
        R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN
    };
    HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, 1, pins);
    mxconfig.driver      = HUB75_I2S_CFG::FM6124;
    mxconfig.double_buff = true;

    display = new MatrixPanel_I2S_DMA(mxconfig);
    display->begin();
    display->setBrightness8(brightness);
    display->setTextWrap(false);
    display->fillScreen(COLOR_BLACK);
    display->flipDMABuffer();
    delay(50);
    display->fillScreen(COLOR_BLACK);
    display->flipDMABuffer();
}

// Top bar: status left, time/date right — 10px tall
void drawTopBar() {
    // Divider line
    display->drawFastHLine(0, 10, PANEL_WIDTH, display->color565(40, 40, 40));

    // Status indicator dot
    uint16_t dotColor;
    if      (!wifiConnected) dotColor = display->color565(220, 0, 0);
    else if (!mqttConnected) dotColor = display->color565(220, 180, 0);
    else                     dotColor = display->color565(0, 220, 0);
    display->fillCircle(4, 4, 3, dotColor);

    // Status text
    const char* statusStr = !wifiConnected ? "No WiFi" :
                            !mqttConnected ? "MQ Off"  : "Online";
    uint16_t textColor = !wifiConnected ? display->color565(220, 80, 80) :
                         !mqttConnected ? display->color565(220, 180, 0) :
                                         display->color565(100, 200, 100);
    display->setCursor(10, 2);
    display->setTextColor(textColor);
    display->setTextSize(1);
    display->print(statusStr);

    // Time & Date
    if (timeSynced) {
        char timeStr[9];
        if (colonState) strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
        else            strftime(timeStr, sizeof(timeStr), "%H %M", &timeinfo);

        int timeWidth = strlen(timeStr) * 6;
        int cursorX   = PANEL_WIDTH - timeWidth - 1;

        if (showDate) {
            char dateStr[7];
            strftime(dateStr, sizeof(dateStr), "%m/%d", &timeinfo);
            int dateWidth = strlen(dateStr) * 6;
            // date to the left of time with small gap
            display->setCursor(cursorX - dateWidth - 4, 2);
            display->setTextColor(display->color565(80, 160, 60));
            display->print(dateStr);
        }

        display->setCursor(cursorX, 2);
        display->setTextColor(display->color565(0, 230, 110));
        display->print(timeStr);
    }
}

// Page 1: Keg Volumes — bar graph + name + low-keg flash
// Panel content area: y=11..63 = 53px for 2 rows = 26px each
// Per cell layout (26px tall):
//   y+0 : name label (truncated, dim grey)
//   y+9 : bar track (38px wide x 5px)
//   y+16: volume value text
void drawPageVolumes(int xOff) {
    bool flashOn = (millis() / FLASH_PERIOD_MS) % 2 == 0;

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        int x   = 2  + col * 42 + xOff;
        int y   = 12 + row * 26;           // start just below top bar divider

        bool  live    = isSensorLive(kegConnected[i], kegLastSeen[i]);
        float vol     = kegVolumes[i];
        bool  lowKeg  = live && (vol < LOW_KEG_THRESHOLD);
        bool  visible = !lowKeg || flashOn;

        uint16_t c = volumeColor(vol, live);

        // Name label — up to 6 chars, muted
        char label[7];
        strncpy(label, kegNames[i], 6);
        label[6] = '\0';
        display->setCursor(x, y);
        display->setTextColor(visible ? display->color565(150, 150, 150)
                                      : display->color565(60, 60, 60));
        display->print(label);

        // Bar track
        int barX = x;
        int barY = y + 9;
        int barW = 38;
        int barH = 4;
        display->fillRect(barX, barY, barW, barH, display->color565(35, 35, 35));

        // Filled portion
        if (live && visible) {
            float ratio  = constrain(vol / kegMaxVolume, 0.0f, 1.0f);
            int   filled = (int)(ratio * barW);
            if (filled > 0) display->fillRect(barX, barY, filled, barH, c);
        }

        // Volume value
        display->setCursor(x, barY + 6);
        display->setTextColor(visible ? c : display->color565(40, 40, 40));
        char buf[8];
        if (live) snprintf(buf, sizeof(buf), "%.1fg", vol);
        else      strcpy(buf, "---");
        display->print(buf);
    }
}

// Page 2: Keg Temperatures — same 2×3 layout
void drawPageTemps(int xOff) {
    display->setCursor(2 + xOff, 12);
    display->setTextColor(display->color565(120, 120, 120));
    display->setTextSize(1);
    display->print("KEG TEMPS (\xB0""C)");

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        int x   = 2  + col * 42 + xOff;
        int y   = 22 + row * 20;

        bool live = isSensorLive(kegTempConnected[i], kegTempLastSeen[i]);

        uint16_t labelColor = display->color565(140, 140, 140);
        uint16_t valColor   = live ? display->color565(100, 160, 255)
                                   : display->color565(70, 70, 70);

        display->setCursor(x, y);
        display->setTextColor(labelColor);
        // Show user-defined name, truncated to 5 chars to fit 42px column
        char label[6];
        strncpy(label, kegTempNames[i], 5);
        label[5] = '\0';
        display->print(label);

        display->setCursor(x, y + 9);
        display->setTextColor(valColor);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f", kegTemps[i]);
        display->print(buf);
    }
}

// Page 3: Custom Sensors — up to 8, 2 rows × 4 cols
void drawPageCustom(int xOff) {
    if (numCustomSensors == 0) {
        display->setCursor(10 + xOff, 30);
        display->setTextColor(display->color565(100, 100, 100));
        display->print("No sensors configured");
        return;
    }

    display->setCursor(2 + xOff, 12);
    display->setTextColor(display->color565(120, 120, 120));
    display->setTextSize(1);
    display->print("CUSTOM SENSORS");

    const int COLS      = 4;
    const int COL_WIDTH = PANEL_WIDTH / COLS;  // 32px each

    for (int i = 0; i < numCustomSensors && i < MAX_CUSTOM_SENSORS; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int x   = col * COL_WIDTH + 1 + xOff;
        int y   = 22 + row * 20;
        if (y > 54) break;

        bool live = isSensorLive(customSensors[i].connected, customSensors[i].lastSeen);

        uint16_t labelColor = display->color565(140, 140, 140);
        uint16_t valColor   = live ? display->color565(255, 170, 80)
                                   : display->color565(70, 70, 70);

        // Name label (truncate to 4 chars)
        char shortName[5];
        strncpy(shortName, customSensors[i].name, 4);
        shortName[4] = '\0';

        display->setCursor(x, y);
        display->setTextColor(labelColor);
        display->print(shortName);

        // Value + unit
        display->setCursor(x, y + 9);
        display->setTextColor(valColor);
        char buf[10];
        snprintf(buf, sizeof(buf), "%.0f%s", customSensors[i].value, customSensors[i].unit);
        display->print(buf);
    }
}

// Small page indicator dots at bottom right
void drawPageIndicator() {
    // Build same explicit list as rotation so dots match exactly
    DisplayPage pageList[4];
    int numPages = 0;
    pageList[numPages++] = PAGE_VOLUMES;
    pageList[numPages++] = PAGE_TEMPS;
    if (numCustomSensors > 0) pageList[numPages++] = PAGE_CUSTOM;
    pageList[numPages++] = PAGE_POUR;

    int dotSize = 2;
    int spacing = 6;
    int startX  = PANEL_WIDTH - (numPages * spacing) - 2;
    int y       = PANEL_HEIGHT - dotSize - 1;

    for (int i = 0; i < numPages; i++) {
        uint16_t c = (pageList[i] == currentPage)
                   ? display->color565(200, 200, 200)
                   : display->color565(50, 50, 50);
        display->fillRect(startX + i * spacing, y, dotSize, dotSize, c);
    }
}

// ─── Pour interrupt page ─────────────────────────────────────────────────────
// Full-screen splash shown for pourDisplaySec seconds after a pour event.
// Layout (content area y=11..63):
//   y=13  "LAST POUR" heading dim
//   y=23  Keg name, large-ish
//   y=36  Amount bar (full width) + "X.Xg" text
//   y=52  Time string, dim
void drawPagePour() {
    // Heading
    display->setCursor(2, 13);
    display->setTextColor(display->color565(80, 80, 80));
    display->setTextSize(1);
    display->print("LAST POUR");

    // Keg name — centred, bright amber
    display->setTextColor(display->color565(255, 180, 40));
    int nameLen = strlen(pourKegName);
    int nameX   = max(0, (PANEL_WIDTH - nameLen * 6) / 2);
    display->setCursor(nameX, 23);
    display->print(pourKegName);

    // Amount bar (full usable width = 124px, margin 2px each side)
    int barX = 2;
    int barY = 34;
    int barW = 124;
    int barH = 6;
    display->fillRect(barX, barY, barW, barH, display->color565(35, 35, 35));
    float ratio  = constrain(pourAmountGal / (kegMaxVolume * 128.0f), 0.0f, 1.0f);
    int   filled = (int)(ratio * barW);
    if (filled > 0) display->fillRect(barX, barY, filled, barH, display->color565(255, 140, 0));

    // Amount in ounces (1 gal = 128 fl oz)
    float pourOz = pourAmountGal * 1.0f;
    char amtBuf[12];
    snprintf(amtBuf, sizeof(amtBuf), "%.1f oz", pourOz);
    int amtW = strlen(amtBuf) * 6;
    display->setCursor((PANEL_WIDTH - amtW) / 2, barY + 9);
    display->setTextColor(display->color565(255, 200, 80));
    display->print(amtBuf);

    // Time
    int timeW = strlen(pourTime) * 6;
    display->setCursor((PANEL_WIDTH - timeW) / 2, 53);
    display->setTextColor(display->color565(100, 100, 100));
    display->print(pourTime);

    // Thin accent line at bottom when showing as interrupt
    if (pourActive) {
        unsigned long elapsed = millis() - pourStartMs;
        float remain = 1.0f - constrain((float)elapsed / (pourDisplaySec * 1000UL), 0.0f, 1.0f);
        display->fillRect(0, 62, PANEL_WIDTH, 1, display->color565(30, 30, 30));
        display->fillRect(0, 62, (int)(remain * PANEL_WIDTH), 1, display->color565(80, 80, 200));
    }
}

// Called when a pour MQTT message arrives — interrupts the normal rotation
void triggerPour(const char* keg, float amount, const char* t) {
    strncpy(pourKegName, keg, sizeof(pourKegName) - 1);
    pourAmountGal = amount;
    strncpy(pourTime, t, sizeof(pourTime) - 1);

    prePourPage  = scrolling ? nextPage : currentPage;   // remember where we were
    pourActive   = true;
    pourStartMs  = millis();
    scrolling    = false;    // cancel any in-progress scroll
    currentPage  = PAGE_POUR;
    lastPageSwitch = millis();  // reset page timer so normal rotation restarts cleanly
    Serial.printf("Pour: %s %.2f gal at %s\n", keg, amount, t);
}

// ─── Unified page dispatcher (used for scroll rendering) ────────────────────
void drawPage(DisplayPage page, int xOff) {
    switch (page) {
        case PAGE_VOLUMES: drawPageVolumes(xOff); break;
        case PAGE_TEMPS:   drawPageTemps(xOff);   break;
        case PAGE_CUSTOM:  drawPageCustom(xOff);  break;
        case PAGE_POUR:    drawPagePour();         break;  // no xOff — not scrollable
        default: break;
    }
}

// ─── Scroll transition ───────────────────────────────────────────────────────
// Call once to kick off a slide-left transition to a new page
void startScroll(DisplayPage to) {
    if (scrolling) return;
    nextPage    = to;
    scrolling   = true;
    scrollStart = millis();
    scrollOffset= 0;
}

// Called every display refresh while scrolling is active.
// Draws outgoing page sliding left and incoming page entering from right.
void drawScrollFrame() {
    unsigned long elapsed = millis() - scrollStart;
    // Ease-out: fast start, slow finish
    float t   = (float)elapsed / SCROLL_DURATION_MS;
    if (t > 1.0f) t = 1.0f;
    float ease = 1.0f - (1.0f - t) * (1.0f - t);   // quadratic ease-out
    int off    = (int)(ease * PANEL_WIDTH);

    // Outgoing page slides left (negative offset)
    drawPage(currentPage, -off);
    // Incoming page enters from right
    drawPage(nextPage, PANEL_WIDTH - off);

    if (t >= 1.0f) {
        scrolling   = false;
        currentPage = nextPage;
    }
}

// ─── Auto-brightness ─────────────────────────────────────────────────────────
void updateAutoBrightness() {
    if (!autoBrightEnabled || !timeSynced) return;
    unsigned long now = millis();
    if (now - lastAutoBright < 10000) return;  // check every 10s
    lastAutoBright = now;

    int h = timeinfo.tm_hour;
    bool night;
    if (nightStartHour > nightEndHour) {
        // e.g. 23..7 wraps midnight
        night = (h >= nightStartHour || h < nightEndHour);
    } else {
        night = (h >= nightStartHour && h < nightEndHour);
    }

    if (night && !inNightMode) {
        inNightMode = true;
        display->setBrightness8(nightBrightness);
        Serial.printf("Auto-dim: night mode ON (hour=%d)\n", h);
    } else if (!night && inNightMode) {
        inNightMode = false;
        display->setBrightness8(dayBrightness);
        Serial.printf("Auto-dim: day mode ON (hour=%d)\n", h);
    }
}

// ==================== MQTT ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char val[24];
    int  len = min(length, (unsigned int)(sizeof(val) - 1));
    memcpy(val, payload, len);
    val[len] = '\0';
    float v  = atof(val);

    Serial.printf("MQTT rx: %s = %.2f\n", topic, v);

    // Stats topic — JSON with tap volumes and pour state
    if (strcmp(topic, BEERMONITOR_STATS_TOPIC) == 0) {
        char jsonBuf[2048];
        int jlen = min(length, (unsigned int)(sizeof(jsonBuf) - 1));
        memcpy(jsonBuf, payload, jlen);
        jsonBuf[jlen] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonBuf);
        if (err) {
            Serial.printf("Stats JSON parse error: %s\n", err.c_str());
            return;
        }

        unsigned long now = millis();
        int activeTap = doc["activeTap"] | -1;
        JsonArray taps = doc["taps"];

        int idx = 0;
        for (JsonObject tap : taps) {
            if (idx >= 6) break;
            kegVolumes[idx]  = tap["levelGal"] | 0.0f;
            kegConnected[idx]= true;
            kegLastSeen[idx] = now;
            const char* beerName = tap["beer"] | "";
            strncpy(tapBeerNames[idx], beerName, sizeof(tapBeerNames[idx]) - 1);
            tapBeerNames[idx][sizeof(tapBeerNames[idx]) - 1] = '\0';
            idx++;
        }

        // Pour detection — activeTap >=0 means someone is pouring
        if (activeTap >= 0 && activeTap < 6) {
            float currentPour = taps[activeTap]["currentPour"] | 0.0f;  // oz
            char tStr[16] = "";
            if (timeSynced) strftime(tStr, sizeof(tStr), "%H:%M", &timeinfo);

            if (prevActiveTap != activeTap) {
                // New tap started pouring
                triggerPour(tapBeerNames[activeTap], currentPour, tStr);
            } else if (pourActive) {
                // Update live pour amount
                pourAmountGal = currentPour;
            }
        }
        prevActiveTap = activeTap;
        return;
    }

    // Keg temp topics
    for (int i = 0; i < 6; i++) {
        if (strlen(kegTempTopics[i]) > 0 && strcmp(topic, kegTempTopics[i]) == 0) {
            kegTemps[i]         = v;
            kegTempConnected[i] = true;
            kegTempLastSeen[i]  = millis();
            return;
        }
    }

    // Last pour — JSON payload, handled separately before float parse
    if (strcmp(topic, LAST_POUR_TOPIC) == 0) {
        // Payload is the raw bytes; re-read as a proper string
        char jsonBuf[128];
        int jlen = min(length, (unsigned int)(sizeof(jsonBuf) - 1));
        memcpy(jsonBuf, payload, jlen);
        jsonBuf[jlen] = '\0';

        // Parse with ArduinoJson (StaticJsonDocument on stack — small payload)
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonBuf);
        if (!err) {
            const char* keg    = doc["keg"]           | "";
            float       amt    = doc["amount_gallons"] | doc["ounces"] | 0.0f;
            const char* tStr   = doc["time"]           | "";
            triggerPour(keg, amt, tStr);
        } else {
            Serial.printf("Pour JSON parse error: %s\n", err.c_str());
        }
        return;
    }

    // Custom sensors
    for (int i = 0; i < numCustomSensors; i++) {
        if (strlen(customSensors[i].topic) > 0 && strcmp(topic, customSensors[i].topic) == 0) {
            customSensors[i].value     = v;
            customSensors[i].connected = true;
            customSensors[i].lastSeen  = millis();
            return;
        }
    }
}

void resubscribeAll() {
    mqttClient.subscribe(BEERMONITOR_STATS_TOPIC);
    Serial.printf("  Sub: %s\n", BEERMONITOR_STATS_TOPIC);
    for (int i = 0; i < 6; i++) {
        if (strlen(kegTempTopics[i]) > 0) {
            mqttClient.subscribe(kegTempTopics[i]);
            Serial.printf("  Sub: %s\n", kegTempTopics[i]);
        }
    }
    for (int i = 0; i < numCustomSensors; i++) {
        if (strlen(customSensors[i].topic) > 0) {
            mqttClient.subscribe(customSensors[i].topic);
            Serial.printf("  Sub custom: %s\n", customSensors[i].topic);
        }
    }
    mqttClient.subscribe(LAST_POUR_TOPIC);
    Serial.printf("  Sub: %s\n", LAST_POUR_TOPIC);
}

void connectMQTT() {
    if (!wifiConnected) return;

    mqttClient.setServer(mqttServer, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(2048);

    Serial.print("Connecting MQTT... ");
    if (mqttClient.connect(MQTT_CLIENT_ID, mqttUser, mqttPass)) {
        mqttConnected = true;
        Serial.println("OK");
        resubscribeAll();
    } else {
        Serial.printf("Failed rc=%d\n", mqttClient.state());
    }
}

void checkSensorTimeouts() {
    unsigned long now = millis();

    for (int i = 0; i < 6; i++) {
        if (kegConnected[i] && (now - kegLastSeen[i] > SENSOR_TIMEOUT)) {
            kegConnected[i] = false;
            Serial.printf("Timeout: keg %d volume\n", i + 1);
        }
    }
    for (int i = 0; i < 6; i++) {
        if (kegTempConnected[i] && (now - kegTempLastSeen[i] > SENSOR_TIMEOUT)) {
            kegTempConnected[i] = false;
            Serial.printf("Timeout: keg %d temp\n", i + 1);
        }
    }
    for (int i = 0; i < numCustomSensors; i++) {
        if (customSensors[i].connected && (now - customSensors[i].lastSeen > SENSOR_TIMEOUT)) {
            customSensors[i].connected = false;
            Serial.printf("Timeout: sensor %s\n", customSensors[i].name);
        }
    }
}

// ==================== PREFERENCES ====================
void loadConfig() {
    prefs.begin("kegcfg", false);

    prefs.getString("mqttServer", mqttServer, sizeof(mqttServer));
    prefs.getString("mqttUser",   mqttUser,   sizeof(mqttUser));
    prefs.getString("mqttPass",   mqttPass,   sizeof(mqttPass));

    for (int i = 0; i < 6; i++) {
        char key[20];
        snprintf(key, sizeof(key), "kegTemp%d", i);
        String v = prefs.getString(key, kegTempTopics[i]);
        strncpy(kegTempTopics[i], v.c_str(), sizeof(kegTempTopics[i]) - 1);
        snprintf(key, sizeof(key), "kegTName%d", i);
        String n = prefs.getString(key, kegTempNames[i]);
        strncpy(kegTempNames[i], n.c_str(), sizeof(kegTempNames[i]) - 1);
    }

    // Keg volume display names
    for (int i = 0; i < 6; i++) {
        char key[16];
        snprintf(key, sizeof(key), "kegName%d", i);
        String n = prefs.getString(key, kegNames[i]);
        strncpy(kegNames[i], n.c_str(), sizeof(kegNames[i]) - 1);
    }
    kegMaxVolume = prefs.getFloat("kegMaxVol", kegMaxVolume);

    // Auto-brightness
    pageDurationSec   = prefs.getULong("pageDurSec", 8);
    pourDisplaySec    = prefs.getULong("pourDispSec", POUR_DISPLAY_SEC);
    autoBrightEnabled = prefs.getBool("autoBright", false);
    nightBrightness   = prefs.getUChar("nightBright", 15);
    dayBrightness     = prefs.getUChar("dayBright",   DEFAULT_BRIGHTNESS);
    nightStartHour    = prefs.getInt("nightStart", 23);
    nightEndHour      = prefs.getInt("nightEnd",    7);

    numCustomSensors = prefs.getInt("numSensors", 0);
    if (numCustomSensors > MAX_CUSTOM_SENSORS) numCustomSensors = MAX_CUSTOM_SENSORS;

    for (int i = 0; i < numCustomSensors; i++) {
        char key[20];

        snprintf(key, sizeof(key), "sTopic%d", i);
        String topic = prefs.getString(key, "");
        strncpy(customSensors[i].topic, topic.c_str(), sizeof(customSensors[i].topic) - 1);

        snprintf(key, sizeof(key), "sName%d", i);
        String name = prefs.getString(key, "S" + String(i + 1));
        strncpy(customSensors[i].name, name.c_str(), sizeof(customSensors[i].name) - 1);

        snprintf(key, sizeof(key), "sUnit%d", i);
        String unit = prefs.getString(key, "C");
        strncpy(customSensors[i].unit, unit.c_str(), sizeof(customSensors[i].unit) - 1);

        customSensors[i].value     = 0;
        customSensors[i].connected = false;
        customSensors[i].lastSeen  = 0;
    }
}

void saveConfig() {
    prefs.putString("mqttServer", mqttServer);
    prefs.putString("mqttUser",   mqttUser);
    prefs.putString("mqttPass",   mqttPass);

    for (int i = 0; i < 6; i++) {
        char key[20];
        snprintf(key, sizeof(key), "kegTemp%d", i);
        prefs.putString(key, kegTempTopics[i]);
        snprintf(key, sizeof(key), "kegTName%d", i);
        prefs.putString(key, kegTempNames[i]);
    }

    // Keg volume names
    for (int i = 0; i < 6; i++) {
        char key[16];
        snprintf(key, sizeof(key), "kegName%d", i);
        prefs.putString(key, kegNames[i]);
    }
    prefs.putFloat("kegMaxVol",   kegMaxVolume);

    // Auto-brightness
    prefs.putULong("pageDurSec",  pageDurationSec);
    prefs.putULong("pourDispSec", pourDisplaySec);
    prefs.putBool("autoBright",   autoBrightEnabled);
    prefs.putUChar("nightBright", nightBrightness);
    prefs.putUChar("dayBright",   dayBrightness);
    prefs.putInt("nightStart",    nightStartHour);
    prefs.putInt("nightEnd",      nightEndHour);

    prefs.putInt("numSensors", numCustomSensors);

    for (int i = 0; i < numCustomSensors; i++) {
        char key[20];
        snprintf(key, sizeof(key), "sTopic%d", i);
        prefs.putString(key, customSensors[i].topic);
        snprintf(key, sizeof(key), "sName%d", i);
        prefs.putString(key, customSensors[i].name);
        snprintf(key, sizeof(key), "sUnit%d", i);
        prefs.putString(key, customSensors[i].unit);
    }
}

void addCustomSensor(const char* topic, const char* name, const char* unit) {
    if (numCustomSensors >= MAX_CUSTOM_SENSORS) return;
    int i = numCustomSensors;
    strncpy(customSensors[i].topic, topic, sizeof(customSensors[i].topic) - 1);
    strncpy(customSensors[i].name,  name,  sizeof(customSensors[i].name)  - 1);
    strncpy(customSensors[i].unit,  unit,  sizeof(customSensors[i].unit)  - 1);
    customSensors[i].value     = 0;
    customSensors[i].connected = false;
    customSensors[i].lastSeen  = 0;
    numCustomSensors++;
    saveConfig();
    if (mqttConnected && strlen(topic) > 0) {
        mqttClient.subscribe(topic);
        Serial.printf("Subscribed new: %s\n", topic);
    }
}

void removeCustomSensor(int index) {
    if (index < 0 || index >= numCustomSensors) return;
    for (int i = index; i < numCustomSensors - 1; i++) {
        customSensors[i] = customSensors[i + 1];
    }
    numCustomSensors--;
    saveConfig();
}

// ==================== WEB SERVER ====================
// Shared CSS
static const char PAGE_CSS[] PROGMEM = R"rawliteral(
<style>
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.3}}
*{box-sizing:border-box}
body{background:#111827;color:#e5e7eb;font-family:'Segoe UI',Arial,sans-serif;margin:0;padding:0}
.navbar{background:#1f2937;padding:14px 20px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #374151}
.navbar h1{margin:0;font-size:1.2rem;color:#f9fafb}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}
.dot-green{background:#22c55e}.dot-yellow{background:#eab308}.dot-red{background:#ef4444}.dot-gray{background:#4b5563}
.container{max-width:900px;margin:20px auto;padding:0 16px}
.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:20px;margin-bottom:16px}
.card h2{margin:0 0 14px;font-size:1rem;color:#9ca3af;text-transform:uppercase;letter-spacing:0.05em}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.keg-cell{background:#111827;border-radius:8px;padding:10px;text-align:center;border:1px solid transparent;transition:border-color 0.5s}
.keg-cell.live{border-color:#374151}
.keg-label{font-size:0.7rem;color:#6b7280;margin-bottom:6px;display:flex;align-items:center;justify-content:center;gap:5px}
.live-dot{width:6px;height:6px;border-radius:50%;background:#22c55e;animation:pulse 2s infinite;flex-shrink:0}
.dead-dot{width:6px;height:6px;border-radius:50%;background:#374151;flex-shrink:0}
.keg-val{font-size:1.5rem;font-weight:700;transition:color 0.5s}
.c-green{color:#22c55e}.c-cyan{color:#06b6d4}.c-orange{color:#f97316}.c-red{color:#ef4444}.c-gray{color:#4b5563}
button,input[type=submit]{background:#2563eb;color:#fff;border:none;padding:9px 18px;border-radius:7px;cursor:pointer;font-size:0.9rem;transition:background 0.2s}
button:hover,input[type=submit]:hover{background:#1d4ed8}
.btn-sm{padding:5px 12px;font-size:0.8rem}
.btn-danger{background:#dc2626}.btn-danger:hover{background:#b91c1c}
.btn-sec{background:#374151}.btn-sec:hover{background:#4b5563}
input[type=text],input[type=password]{width:100%;padding:9px 12px;background:#111827;color:#e5e7eb;border:1px solid #374151;border-radius:7px;font-size:0.9rem;margin-top:4px}
input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#3b82f6}
label{font-size:0.85rem;color:#9ca3af}
.form-group{margin-bottom:12px}
.sensor-block{background:#111827;border-radius:8px;padding:12px;margin-bottom:10px;border:1px solid #374151}
.controls{display:flex;flex-wrap:wrap;gap:8px}
@keyframes lowFlash{0%,100%{opacity:1}50%{opacity:0.15}}
.low-flash{animation:lowFlash 0.6s infinite}
.keg-cell.alert{border-color:#7f1d1d !important}
.bar-track{background:#1f2937;border-radius:3px;height:6px;margin:6px 0 2px;overflow:hidden}
.bar-fill{height:100%;border-radius:3px;transition:width 0.8s ease,background 0.8s ease;width:0%}
.night-badge{display:none;background:#1e3a5f;color:#60a5fa;font-size:0.7rem;padding:2px 8px;border-radius:20px;margin-left:8px;vertical-align:middle}
.range-row{display:flex;gap:8px;align-items:center}
.range-row input[type=number]{width:70px}
</style>
)rawliteral";

// JSON API — polled by JS to update dashboard without page reload
void handleStatus() {
    String j = "{";
    j += "\"wifi\":"  + String(wifiConnected ? "true" : "false") + ",";
    j += "\"mqtt\":"  + String(mqttConnected ? "true" : "false") + ",";
    if (timeSynced) {
        char t[9]; strftime(t, sizeof(t), "%H:%M:%S", &timeinfo);
        j += "\"time\":\"" + String(t) + "\",";
    } else {
        j += "\"time\":\"--:--:--\",";
    }
    j += "\"nightMode\":" + String(inNightMode ? "true" : "false") + ",";
    j += "\"vols\":[";
    for (int i = 0; i < 6; i++) {
        bool live = isSensorLive(kegConnected[i], kegLastSeen[i]);
        float vol = kegVolumes[i];
        bool  low = live && (vol < LOW_KEG_THRESHOLD);
        j += "{\"v\":" + String(vol, 1);
        j += ",\"live\":" + String(live ? "true" : "false");
        j += ",\"low\":" + String(low  ? "true" : "false");
        j += ",\"n\":\"" + String(kegNames[i]) + "\"}";
        if (i < 5) j += ",";
    }
    j += "],\"temps\":[";
    for (int i = 0; i < 6; i++) {
        bool live = isSensorLive(kegTempConnected[i], kegTempLastSeen[i]);
        j += "{\"v\":" + String(kegTemps[i], 1) + ",\"live\":" + (live ? "true" : "false");
        j += ",\"n\":\"" + String(kegTempNames[i]) + "\"}";
        if (i < 5) j += ",";
    }
    j += "],\"custom\":[";
    for (int i = 0; i < numCustomSensors; i++) {
        bool live = isSensorLive(customSensors[i].connected, customSensors[i].lastSeen);
        j += "{\"n\":\"" + String(customSensors[i].name) + "\",";
        j += "\"v\":" + String(customSensors[i].value, 1) + ",";
        j += "\"u\":\"" + String(customSensors[i].unit) + "\",";
        j += "\"live\":" + String(live ? "true" : "false") + "}";
        if (i < numCustomSensors - 1) j += ",";
    }
    j += "],\"pour\":{\"active\":" + String(pourActive ? "true" : "false");
    j += ",\"keg\":\"" + String(pourKegName) + "\"";
    j += ",\"amount\":" + String(pourAmountGal, 2);
    j += ",\"time\":\"" + String(pourTime) + "\"}";
    j += "}";
    server.send(200, "application/json", j);
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><title>Keg Monitor</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += FPSTR(PAGE_CSS);

    // JS: polls /status every 5s, updates DOM without flicker
    html += "<script>\n"
            "function vc(v,live){if(!live)return'c-gray';if(v>=4)return'c-green';if(v>=2.5)return'c-cyan';if(v>=1)return'c-orange';return'c-red';}\n"
            "function dot(live){return \"<span class='\"+(live?'live':'dead')+\"-dot'></span>\";}\n"
            "function upd(){\n"
            "  fetch('/status').then(r=>r.json()).then(d=>{\n"
            "    var sc=d.wifi?(d.mqtt?'dot dot-green':'dot dot-yellow'):'dot dot-red';\n"
            "    var st=d.wifi?(d.mqtt?'Online':'MQTT Offline'):'No WiFi';\n"
            "    document.getElementById('sdot').className=sc;\n"
            "    document.getElementById('stxt').textContent=st;\n"
            "    document.getElementById('clk').textContent=d.time;\n"
            "    var nm=document.getElementById('nightBadge');\n"
            "    if(nm){nm.style.display=d.nightMode?'inline-block':'none';}\n"
            "    d.vols.forEach(function(s,i){\n"
            "      var cl=vc(parseFloat(s.v),s.live);\n"
            "      var pct=Math.round(Math.min(parseFloat(s.v)/5,1)*100);\n"
            "      document.getElementById('vv'+i).className='keg-val '+cl+(s.low?' low-flash':'');\n"
            "      document.getElementById('vv'+i).innerHTML=s.v+\"<small style='font-size:0.7rem;color:#6b7280'> gal</small>\";\n"
            "      document.getElementById('vb'+i).style.width=pct+'%';\n"
            "      document.getElementById('vb'+i).style.background=s.low?'#ef4444':(pct>50?'#22c55e':pct>20?'#f97316':'#ef4444');\n"
            "      document.getElementById('vl'+i).innerHTML=dot(s.live)+s.n;\n"
            "      document.getElementById('vc'+i).className='keg-cell '+(s.live?'live':'')+(s.low?' alert':'');\n"
            "    });\n"
            "    d.temps.forEach(function(s,i){\n"
            "      document.getElementById('tv'+i).className='keg-val '+(s.live?'c-cyan':'c-gray');\n"
            "      document.getElementById('tv'+i).innerHTML=s.v+\"<small style='font-size:0.7rem;color:#6b7280'>&deg;C</small>\";\n"
            "      document.getElementById('tl'+i).innerHTML=dot(s.live)+s.n;\n"
            "      document.getElementById('tc'+i).className='keg-cell '+(s.live?'live':'');\n"
            "    });\n"
            "    var cb=document.getElementById('cbody');\n"
            "    if(cb&&d.custom.length>0){\n"
            "      var h='';\n"
            "      d.custom.forEach(function(s,i){\n"
            "        var cl=s.live?'c-orange':'c-gray';\n"
            "        h+=\"<div class='keg-cell \"+(s.live?'live':'')+\"'>\";\n"
            "        h+=\"<div class='keg-label'>\"+dot(s.live)+s.n+\"</div>\";\n"
            "        h+=\"<div class='keg-val \"+cl+\"'>\"+s.v+\"<small style='font-size:0.7rem;color:#6b7280'> \"+s.u+\"</small></div></div>\";\n"
            "      });\n"
            "      cb.innerHTML=h;\n"
            "    }\n"
            "    // Last pour card\n"
            "    var pb=document.getElementById('pourBody');\n"
            "    var pc=document.getElementById('pourCard');\n"
            "    if(pb&&d.pour){\n"
            "      var p=d.pour;\n"
            "      if(p.keg&&p.keg.length>0){\n"
            "        var ac=p.active;\n"
            "        pc.style.borderColor=ac?'#f97316':'#374151';\n"
            "        var kg=ac?'<span style=\"color:#f97316\">POURING NOW</span>':'Keg';\n"
            "        pb.innerHTML='<div style=\"display:flex;gap:20px;align-items:center;flex-wrap:wrap\">'\n"
            "          +'<div><div style=\"font-size:0.7rem;color:#6b7280\">'+kg+'</div>'\n"
            "          +'<div style=\"font-size:1.3rem;font-weight:700;color:#f97316\">'+p.keg+'</div></div>'\n"
            "          +'<div><div style=\"font-size:0.7rem;color:#6b7280\">Amount</div>'\n"
            "          +'<div style=\"font-size:1.3rem;font-weight:700;color:#fbbf24\">'+(Math.round(p.amount*10)/10)+' oz</div></div>'\n"
            "          +'<div><div style=\"font-size:0.7rem;color:#6b7280\">Time</div>'\n"
            "          +'<div style=\"font-size:1.1rem;color:#9ca3af\">'+p.time+'</div></div></div>';\n"
            "      }\n"
            "    }\n"
            "  }).catch(function(){});\n"
            "}\n"
            "setInterval(upd,5000);\n"
            "window.addEventListener('load',upd);\n"
            "</script>";

    html += "</head><body>";

    // Navbar
    html += "<div class='navbar'>";
    html += "<span id='sdot' class='dot dot-gray'></span>";
    html += "<h1>Keg Monitor</h1>";
    html += "<span style='margin-left:auto;font-size:0.85rem;color:#9ca3af'>";
    html += "<span id='stxt'>...</span>";
    html += " &nbsp;|&nbsp; " + WiFi.localIP().toString();
    html += " &nbsp;|&nbsp; <span id='clk' style='font-variant-numeric:tabular-nums'>--:--:--</span>";
    html += "</span></div>";

    html += "<div class='container'>";

    // Keg Volumes — skeleton filled by JS (bar + name + value)
    html += "<div class='card'><h2>Keg Volumes <span class='night-badge' id='nightBadge'>Night Mode</span></h2><div class='grid3'>";
    for (int i = 0; i < 6; i++) {
        html += "<div class='keg-cell' id='vc" + String(i) + "'>";
        html += "<div class='keg-label' id='vl" + String(i) + "'>" + String(kegNames[i]) + "</div>";
        html += "<div class='bar-track'><div class='bar-fill' id='vb" + String(i) + "'></div></div>";
        html += "<div class='keg-val c-gray' id='vv" + String(i) + "'>--</div></div>";
    }
    html += "</div></div>";

    // Keg Temps
    html += "<div class='card'><h2>Keg Temperatures</h2><div class='grid3'>";
    for (int i = 0; i < 6; i++) {
        html += "<div class='keg-cell' id='tc" + String(i) + "'>";
        html += "<div class='keg-label' id='tl" + String(i) + "'>KEG " + String(i+1) + "</div>";
        html += "<div class='keg-val c-gray' id='tv" + String(i) + "'>--</div></div>";
    }
    html += "</div></div>";

    // Custom sensors — JS rebuilds this div
    if (numCustomSensors > 0) {
        html += "<div class='card'><h2>Custom Sensors</h2><div class='grid4' id='cbody'></div></div>";
    }

    // Last Pour card — always visible, JS updates it
    html += "<div class='card' id='pourCard'><h2>Last Pour</h2>";
    html += "<div id='pourBody' style='color:#6b7280;font-size:0.9rem'>No pour recorded yet</div></div>";

    // Controls — plain text, no emojis
    html += "<div class='card'><h2>Controls</h2><div class='controls'>";
    html += "<button onclick=\"location.href='/bup'\">Brighter</button>";
    html += "<button onclick=\"location.href='/bdown'\">Dimmer</button>";
    html += "<button onclick=\"location.href='/toggle/date'\">Toggle Date</button>";
    html += "<button onclick=\"location.href='/config'\" class='btn-sec'>Config</button>";
    html += "</div></div>";

    html += "</div></body></html>";
    server.send(200, "text/html", html);
}

void handleConfig() {
    String html = "<!DOCTYPE html><html><head><title>Configuration</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += FPSTR(PAGE_CSS);
    html += "</head><body>";

    html += "<div class='navbar'><h1>Configuration</h1></div>";
    html += "<div class='container'>";
    html += "<form method='POST' action='/saveconfig'>";

    // MQTT
    html += "<div class='card'><h2>MQTT Broker</h2>";
    html += "<div class='form-group'><label>Server</label><input type='text' name='mqttServer' value='" + String(mqttServer) + "'></div>";
    html += "<div class='form-group'><label>Username</label><input type='text' name='mqttUser' value='" + String(mqttUser) + "'></div>";
    html += "<div class='form-group'><label>Password</label><input type='password' name='mqttPass' value='" + String(mqttPass) + "'></div>";
    html += "</div>";

    // Keg volume names + capacity
    html += "<div class='card'><h2>Keg Names &amp; Capacity</h2>";
    for (int i = 0; i < 6; i++) {
        html += "<div class='form-group'><label>Keg " + String(i+1) + " Name</label>";
        html += "<input type='text' name='kegName" + String(i) + "' value='" + String(kegNames[i]) + "' placeholder='e.g. Lager'></div>";
    }
    html += "<div class='form-group'><label>Keg Capacity (gal) — used for bar graph</label>";
    html += "<input type='text' name='kegMaxVol' value='" + String(kegMaxVolume, 1) + "'></div>";
    html += "</div>";

    // Display timing + auto-brightness
    html += "<div class='card'><h2>Display Settings</h2>";
    html += "<div class='form-group'><label>Page display time (seconds, 5-60)</label>";
    html += "<input type='number' name='pageDurSec' min='5' max='60' value='" + String(pageDurationSec) + "'></div>";
    html += "<div class='form-group'><label>Pour alert display time (seconds, 5-30)</label>";
    html += "<input type='number' name='pourDispSec' min='5' max='30' value='" + String(pourDisplaySec) + "'></div>";
    html += "</div>";

    // Auto-brightness
    html += "<div class='card'><h2>Auto-Brightness Schedule</h2>";
    html += "<div class='form-group'><label style='display:flex;align-items:center;gap:8px;cursor:pointer'>";
    html += "<input type='checkbox' name='autoBright' value='1'" + String(autoBrightEnabled ? " checked" : "") + "> Enable auto-brightness</label></div>";
    html += "<div class='range-row'>";
    html += "<div class='form-group'><label>Night start (hour 0-23)</label><input type='number' name='nightStart' min='0' max='23' value='" + String(nightStartHour) + "'></div>";
    html += "<div class='form-group'><label>Night end (hour 0-23)</label><input type='number' name='nightEnd' min='0' max='23' value='" + String(nightEndHour) + "'></div>";
    html += "</div>";
    html += "<div class='range-row'>";
    html += "<div class='form-group'><label>Day brightness (10-255)</label><input type='number' name='dayBright' min='10' max='255' value='" + String(dayBrightness) + "'></div>";
    html += "<div class='form-group'><label>Night brightness (10-255)</label><input type='number' name='nightBright' min='10' max='255' value='" + String(nightBrightness) + "'></div>";
    html += "</div></div>";

    // Keg temp topics + names
    html += "<div class='card'><h2>Keg Temperature Sensors</h2>";
    for (int i = 0; i < 6; i++) {
        html += "<div class='sensor-block'>";
        html += "<div class='form-group'><label>Sensor " + String(i+1) + " Display Name</label>";
        html += "<input type='text' name='kegTName" + String(i) + "' value='" + String(kegTempNames[i]) + "' placeholder='e.g. Lager Fridge'></div>";
        html += "<div class='form-group'><label>MQTT Topic</label>";
        html += "<input type='text' name='kegTemp" + String(i) + "' value='" + String(kegTempTopics[i]) + "'></div>";
        html += "</div>";
    }
    html += "</div>";

    // Custom sensors
    html += "<div class='card'><h2>Custom Sensors</h2>";
    for (int i = 0; i < numCustomSensors; i++) {
        html += "<div class='sensor-block'>";
        html += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:8px'>";
        html += "<strong>Sensor " + String(i+1) + "</strong>";
        html += "<button type='button' class='btn-sm btn-danger' onclick=\"location.href='/removesensor?idx=" + String(i) + "'\">Remove</button>";
        html += "</div>";
        html += "<div class='form-group'><label>Display Name (max 4 chars on matrix)</label><input type='text' name='name" + String(i) + "' value='" + String(customSensors[i].name) + "'></div>";
        html += "<div class='form-group'><label>Unit</label><input type='text' name='unit" + String(i) + "' value='" + String(customSensors[i].unit) + "'></div>";
        html += "<div class='form-group'><label>MQTT Topic</label><input type='text' name='topic" + String(i) + "' value='" + String(customSensors[i].topic) + "'></div>";
        html += "</div>";
    }
    if (numCustomSensors < MAX_CUSTOM_SENSORS) {
        html += "<button type='button' onclick=\"location.href='/addsensor'\">Add Sensor</button>";
    } else {
        html += "<p style='color:#6b7280'>Maximum sensors reached (" + String(MAX_CUSTOM_SENSORS) + ")</p>";
    }
    html += "</div>";

    html += "<div style='display:flex;gap:10px;margin-bottom:30px'>";
    html += "<input type='submit' value='Save All'>";
    html += "<button type='button' class='btn-sec' onclick=\"location.href='/'\">Back</button>";
    html += "</div>";
    html += "</form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleSaveConfig() {
    if (server.hasArg("mqttServer")) strncpy(mqttServer, server.arg("mqttServer").c_str(), sizeof(mqttServer)-1);
    if (server.hasArg("mqttUser"))   strncpy(mqttUser,   server.arg("mqttUser").c_str(),   sizeof(mqttUser)-1);
    if (server.hasArg("mqttPass"))   strncpy(mqttPass,   server.arg("mqttPass").c_str(),   sizeof(mqttPass)-1);

    for (int i = 0; i < 6; i++) {
        String tArg = "kegTemp" + String(i);
        String nArg = "kegTName" + String(i);
        if (server.hasArg(tArg)) strncpy(kegTempTopics[i], server.arg(tArg).c_str(), sizeof(kegTempTopics[i])-1);
        if (server.hasArg(nArg)) strncpy(kegTempNames[i],  server.arg(nArg).c_str(), sizeof(kegTempNames[i])-1);
    }

    for (int i = 0; i < numCustomSensors; i++) {
        if (server.hasArg("name"  + String(i))) strncpy(customSensors[i].name,  server.arg("name"  + String(i)).c_str(), sizeof(customSensors[i].name) -1);
        if (server.hasArg("unit"  + String(i))) strncpy(customSensors[i].unit,  server.arg("unit"  + String(i)).c_str(), sizeof(customSensors[i].unit) -1);
        if (server.hasArg("topic" + String(i))) strncpy(customSensors[i].topic, server.arg("topic" + String(i)).c_str(), sizeof(customSensors[i].topic)-1);
    }

    // Keg volume names + capacity
    for (int i = 0; i < 6; i++) {
        String nArg = "kegName" + String(i);
        if (server.hasArg(nArg)) strncpy(kegNames[i], server.arg(nArg).c_str(), sizeof(kegNames[i])-1);
    }
    if (server.hasArg("kegMaxVol")) kegMaxVolume = server.arg("kegMaxVol").toFloat();

    // Auto-brightness
    if (server.hasArg("pageDurSec"))  pageDurationSec = (unsigned long)server.arg("pageDurSec").toInt();
    if (server.hasArg("pourDispSec")) pourDisplaySec  = (unsigned long)server.arg("pourDispSec").toInt();
    autoBrightEnabled = server.hasArg("autoBright") && server.arg("autoBright") == "1";
    if (server.hasArg("nightBright")) nightBrightness = (uint8_t)server.arg("nightBright").toInt();
    if (server.hasArg("dayBright"))   dayBrightness   = (uint8_t)server.arg("dayBright").toInt();
    if (server.hasArg("nightStart"))  nightStartHour  = server.arg("nightStart").toInt();
    if (server.hasArg("nightEnd"))    nightEndHour    = server.arg("nightEnd").toInt();

    saveConfig();
    mqttConnected = false;  // Force MQTT reconnect to re-subscribe with new topics
    // Apply brightness immediately
    if (!inNightMode) display->setBrightness8(dayBrightness);

    server.sendHeader("Location", "/");
    server.send(302, "", "");
}

void handleAddSensor() {
    char name[16];
    snprintf(name, sizeof(name), "S%d", numCustomSensors + 1);
    addCustomSensor("", name, "C");
    server.sendHeader("Location", "/config");
    server.send(302, "", "");
}

// Fixed: use query param ?idx= instead of path segment to avoid routing issues
void handleRemoveSensor() {
    if (server.hasArg("idx")) {
        int idx = server.arg("idx").toInt();
        removeCustomSensor(idx);
    }
    server.sendHeader("Location", "/config");
    server.send(302, "", "");
}

void handleBrightnessUp() {
    brightness = (uint8_t)min((int)brightness + 15, (int)MAX_BRIGHTNESS);
    display->setBrightness8(brightness);
    server.sendHeader("Location", "/");
    server.send(302, "", "");
}

void handleBrightnessDown() {
    brightness = (uint8_t)max((int)brightness - 15, (int)MIN_BRIGHTNESS);
    display->setBrightness8(brightness);
    server.sendHeader("Location", "/");
    server.send(302, "", "");
}

void handleToggleDate() {
    showDate = !showDate;
    server.sendHeader("Location", "/");
    server.send(302, "", "");
}

// ==================== OTA ====================
void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        display->fillScreen(COLOR_BLACK);
        display->setCursor(10, 28);
        display->setTextColor(display->color565(0, 200, 255));
        display->print("OTA Update...");
        display->flipDMABuffer();
    });
    ArduinoOTA.onEnd([]() { otaInProgress = false; });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress / (total / 100);
        // Progress bar
        display->fillRect(10, 40, 108, 6, display->color565(30, 30, 30));
        display->fillRect(10, 40, (108 * pct) / 100, 6, display->color565(0, 200, 255));
        display->fillScreen(COLOR_BLACK);
        display->setCursor(10, 28);
        display->setTextColor(display->color565(0, 200, 255));
        display->print("OTA Update...");
        display->fillRect(10, 40, 108, 6, display->color565(30, 30, 30));
        display->fillRect(10, 40, (108 * pct) / 100, 6, display->color565(0, 200, 255));
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        display->setCursor(55, 50);
        display->setTextColor(display->color565(200, 200, 200));
        display->print(buf);
        display->flipDMABuffer();
    });
    ArduinoOTA.begin();
}

void initWebServer() {
    server.on("/",             handleRoot);
    server.on("/status",       handleStatus);
    server.on("/config",       handleConfig);
    server.on("/saveconfig",   HTTP_POST, handleSaveConfig);
    server.on("/addsensor",    handleAddSensor);
    server.on("/removesensor", handleRemoveSensor);   // now uses ?idx=N
    server.on("/bup",          handleBrightnessUp);
    server.on("/bdown",        handleBrightnessDown);
    server.on("/toggle/date",  handleToggleDate);
    server.onNotFound([]() {
        server.sendHeader("Location", "/");
        server.send(302, "", "");
    });
    server.begin();
    Serial.println("Web server started");
}

// ==================== WIFI ====================
void WiFiEvent(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        wifiConnected = true;
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        setenv("TZ", TZ_STRING, 1);
        tzset();
        setupOTA();
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        wifiConnected = false;
        mqttConnected = false;
        Serial.println("WiFi disconnected");
    }
}

// ==================== SETUP & LOOP ====================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Keg Monitor Starting ===");

    loadConfig();
    initDisplay();

    // Boot animation — brief RGB flash
    const uint16_t flashColors[] = {
        display->color565(180,0,0),
        display->color565(0,180,0),
        display->color565(0,0,180)
    };
    for (int c = 0; c < 3; c++) {
        display->fillScreen(flashColors[c]);
        display->flipDMABuffer();
        delay(80);
    }
    display->fillScreen(COLOR_BLACK);
    display->flipDMABuffer();
    delay(50);
    display->fillScreen(COLOR_BLACK);
    display->flipDMABuffer();

    // Boot message
    display->setCursor(20, 26);
    display->setTextColor(display->color565(0,180,255));
    display->print("Connecting...");
    display->flipDMABuffer();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(WiFiEvent);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    initWebServer();
}

void loop() {
    unsigned long now = millis();

    if (!otaInProgress) {
        server.handleClient();
        ArduinoOTA.handle();
    }

    // MQTT connect / keep-alive
    if (wifiConnected) {
        if (!mqttConnected && (now - lastMqttReconnect > MQTT_RECONNECT_MS)) {
            lastMqttReconnect = now;
            connectMQTT();
        } else if (mqttConnected) {
            if (!mqttClient.loop()) {
                // loop() returns false if connection dropped
                mqttConnected = false;
                Serial.println("MQTT connection lost");
            }
        }

        // NTP sync
        if (!timeSynced) {
            timeSynced = getLocalTime(&timeinfo);
        } else {
            getLocalTime(&timeinfo);
        }
    }

    // Sensor timeout checks
    if (now - lastSensorCheck > 5000) {
        lastSensorCheck = now;
        checkSensorTimeouts();
    }

    // Auto-brightness check
    updateAutoBrightness();

    // Pour page timeout — return to normal rotation after pourDisplaySec
    if (pourActive && (now - pourStartMs > pourDisplaySec * 1000UL)) {
        pourActive  = false;
        currentPage = prePourPage;
        lastPageSwitch = now;   // restart dwell timer from here
        Serial.println("Pour display ended, resuming rotation");
    }

    // Page rotation — only when not showing pour interrupt
    if (!pourActive) {
        // Build explicit rotation list so PAGE_POUR always appears
        // and PAGE_CUSTOM only appears when sensors are configured
        DisplayPage pageList[4];
        int numPages = 0;
        pageList[numPages++] = PAGE_VOLUMES;
        pageList[numPages++] = PAGE_TEMPS;
        if (numCustomSensors > 0) pageList[numPages++] = PAGE_CUSTOM;
        pageList[numPages++] = PAGE_POUR;

        if (!scrolling && (now - lastPageSwitch > pageDurationSec * 1000UL)) {
            lastPageSwitch = now;
            // Find current page in list, advance to next
            int cur = 0;
            for (int pi = 0; pi < numPages; pi++) {
                if (pageList[pi] == currentPage) { cur = pi; break; }
            }
            DisplayPage np = pageList[(cur + 1) % numPages];
            startScroll(np);
        }
    }

    // Display update
    if (now - lastDisplayUpdate > DISPLAY_REFRESH_MS) {
        lastDisplayUpdate = now;
        colonState = (now / 500) % 2;

        display->fillScreen(COLOR_BLACK);
        drawTopBar();

        if (scrolling) {
            drawScrollFrame();
        } else {
            drawPage(currentPage, 0);
        }

        drawPageIndicator();
        display->flipDMABuffer();
    }

    delay(5);
}
