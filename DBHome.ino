#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>

// Define this to enable button
#define BUTTON_ENABLE

// Set your ssid and password
const char ssid[] = "YOUR_SSID";
const char password[] = "YOUR_PASSWORD";

// Set the site id and direction code. More info: https://www.trafiklab.se/api/trafiklab-apis/sl/transport
const int SiteId = 1183;
const int direction_code = 2;

// Set the refresh interval (ms)
const int refresh_interval = 10000;

// Set the pins
#ifdef BUTTON_ENABLE
const int BUTTON_K1 = D4;
const int BUTTON_K2 = D5;
#endif
const int DISPLAY_DC = D2;
const int DISPLAY_CS = D3;
const int DISPLAY_SCK = D8;
const int DISPLAY_MOSI = D10;
const int DISPLAY_RST = D1;
const int DISPLAY_BL = D0;

// Set the bus, the screen, and the canvas. More info: https://github.com/moononournation/Arduino_GFX
Arduino_DataBus *bus = new Arduino_ESP32SPI(DISPLAY_DC /* DC */, DISPLAY_CS /* CS */,
                                            DISPLAY_SCK /* SCK */, DISPLAY_MOSI /* MOSI */, GFX_NOT_DEFINED /* MISO */,
                                            FSPI /* spi_num */);
Arduino_GFX *gfx = new Arduino_ST7735(
    bus, DISPLAY_RST /* RST */, 3 /* rotation */, false /* IPS */,
    128 /* width */, 160 /* height */,
    0 /* col offset 1 */, 0 /* row offset 1 */,
    0 /* col offset 2 */, 0 /* row offset 2 */,
    false /* BGR */);
Arduino_GFX *canvas = new Arduino_Canvas_Indexed(160 /* width */, 128 /* height */, gfx,
                                                 0 /* output_x */, 0 /* output_y */, 0 /* rotation */,
                                                 3 /* mask_level */);

// Declare the HTTPClient
HTTPClient https;

void setup(void)
{
    Serial.begin(115200);

#ifdef BUTTON_ENABLE
    // Init Button
    pinMode(BUTTON_K1, INPUT_PULLUP);
    pinMode(BUTTON_K2, INPUT_PULLUP);
#endif

    // Init Display
    pinMode(DISPLAY_BL, OUTPUT);
    digitalWrite(DISPLAY_BL, HIGH);
    Serial.println("Init gfx...");
    if (!gfx->begin())
    {
        Serial.println("gfx->begin() failed!");
    }
    gfx->setUTF8Print(true);
    gfx->setFont(u8g2_font_7x14_tf);
    gfx->setTextColor(ORANGE);
    gfx->setTextSize(1 /* x scale */, 1 /* y scale */, 1 /* pixel_margin */);
    gfx->fillScreen(BLACK);

    canvas->begin();
    canvas->setUTF8Print(true);
    canvas->setFont(u8g2_font_7x14_tf);
    canvas->setTextColor(ORANGE);
    canvas->setTextSize(1 /* x scale */, 1 /* y scale */, 1 /* pixel_margin */);

    // Init WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    gfx->setCursor(0, 10);
    gfx->print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        gfx->print(".");
    }
    gfx->println("");
    gfx->println("WiFi connected!");
    gfx->println("IP address: ");
    gfx->println(WiFi.localIP());
}

JsonDocument doc;
long lastTime = -5000;
bool data_valid = false;
int data_num = 0;
int display_start = 0;

void loop()
{
    bool data_new = false;
    int button_pressed = 0;

    // GET new data
    if (millis() - lastTime > refresh_interval)
    {
        lastTime = millis();
        display_start = 0;

        char url[64];
        sprintf(url, "https://transport.integration.sl.se/v1/sites/%d/departures", SiteId);
        while (!https.begin(url) && (WiFi.status() == WL_CONNECTED))
        {
            data_num = 0;
            data_valid = false;
            canvas->fillScreen(BLACK);
            canvas->setCursor(0, 10);
            canvas->println("Connection failed.");
            canvas->println("Retrying...");
            canvas->flush();
            delay(1000);
        }

        int httpResponseCode = https.GET();

        if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            String payload = https.getString();
            DeserializationError error = deserializeJson(doc, payload.c_str());
            if (error) // JSON parse failed
            {
                data_num = 0;
                data_valid = false;
                canvas->fillScreen(BLACK);
                canvas->setCursor(0, 10);
                canvas->println("JSON parse failed: ");
                canvas->println(error.f_str());
                canvas->flush();
                delay(1000);
            }
            else // Got valid data
            {
                data_num = 0;
                for (int i = 0; i < doc["departures"].size(); i++)
                {
                    int dir = doc["departures"][i]["direction_code"];
                    if (dir == direction_code)
                    {
                        data_num++;
                    }
                }
                data_valid = true;
                data_new = true;
            }
        }
        else // GET failed
        {
            data_num = 0;
            data_valid = false;
            canvas->fillScreen(BLACK);
            canvas->setCursor(0, 10);
            canvas->println("GET failed, code: ");
            canvas->println(httpResponseCode);
            canvas->flush();
        }
        https.end();
    }

#ifdef BUTTON_ENABLE
    // Detect button press
    if (digitalRead(BUTTON_K1) == LOW)
    {
        delay(50);
        while (digitalRead(BUTTON_K1) == LOW)
            ;
        button_pressed = 1;
    }
    if (digitalRead(BUTTON_K2) == LOW)
    {
        delay(50);
        while (digitalRead(BUTTON_K2) == LOW)
            ;
        button_pressed = -1;
    }
#endif

    // Flush display
    if (data_valid && (data_new || button_pressed != 0))
    {
#ifdef BUTTON_ENABLE
        if (button_pressed != 0)
        {
            display_start += button_pressed;
            if (display_start < 0)
            {
                display_start = 0;
            }
            if (display_start > data_num - 3)
            {
                display_start = data_num - 3;
            }
            lastTime = millis();
        }
#endif

        canvas->fillScreen(BLACK);
        if (data_num == 0)
        {
            canvas->setCursor(0, 10);
            canvas->println(" -No departures now-");
        }
        else
        {

            int printed_num = 0;
            int to_be_ignored = display_start;
            for (int i = 0; i < doc["departures"].size(); i++)
            {
                int dir = doc["departures"][i]["direction_code"];
                if (dir == direction_code)
                {
                    if (to_be_ignored > 0)
                    {
                        to_be_ignored--;
                        continue;
                    }

                    const char *line_mode = doc["departures"][i]["line"]["transport_mode"];
                    const char *line_id = doc["departures"][i]["line"]["designation"];
                    const char *display = doc["departures"][i]["display"];
                    const char *destination = doc["departures"][i]["destination"];
                    const char *state = doc["departures"][i]["state"];
                    const char *expected = doc["departures"][i]["expected"];

                    canvas->setCursor(0, 10 + 43 * printed_num);
                    canvas->print(line_mode);
                    canvas->print(" ");
                    canvas->print(line_id);
                    canvas->setCursor(118, 10 + 43 * printed_num);
                    canvas->print(display);

                    canvas->setCursor(0, 24 + 43 * printed_num);
                    canvas->print(destination);

                    canvas->setCursor(0, 38 + 43 * printed_num);
                    canvas->print(state);
                    canvas->setCursor(104, 38 + 43 * printed_num);
                    canvas->print(expected + 11);

                    printed_num++;
                    if (printed_num >= 3)
                    {
                        break;
                    }
                }
            }
            for (int i = 0; i < printed_num - 1; i++)
            {
                canvas->drawFastHLine(0, 40 + 43 * i, 160, ORANGE);
            }
        }
        canvas->flush();
    }
}