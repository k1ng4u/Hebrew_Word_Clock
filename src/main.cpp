#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <LittleFS.h>
#include <GxEPD2_BW.h>
#include <esp_sleep.h>

// https://wiki.seeedstudio.com/xiao_075inch_epaper_panel/

// WiFi Credentials
const char* ssid = "XXXX";
const char* password = "YYYY";

// NTP Server settings
const char* ntpServer = "pool.ntp.org";
const char* tzInfo = "IST-2IDT,M3.4.4/26,M10.5.0";
const long  gmtOffset_sec = 7200;      // Israel Time (UTC+2)
const int   daylightOffset_sec = 3600; // DST

// RTC memory to remember sync status across deep sleep
RTC_DATA_ATTR bool timeSynchronized = false; 

// E-Ink display initialization for Seeed Studio 7.5"
GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(/*CS=*/ D1, /*DC=*/ D3, /*RST=*/ D0, /*BUSY=*/ D2));

void syncTimeFromNTP() {
    Serial.println("[WIFI] Attempting to connect to WiFi...");
    WiFi.begin(ssid, password);
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected successfully!");
        
        configTzTime(tzInfo, ntpServer); // סנכרון זמן מול השרת עם אזור הזמן שלנו
        
        struct tm timeinfo;
        int retry = 0;
        // ממתינים עד 10 שניות כדי לוודא שהזמן ירד בהצלחה
        while (!getLocalTime(&timeinfo, 1000) && retry < 10) {
            Serial.print(".");
            retry++;
        }
        
        if (retry < 10) {
            timeSynchronized = true;
            Serial.println("\n[NTP] Time synchronized successfully!");
        }
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// Helpers for BMP reading
uint16_t read16(File& f) {
    uint16_t result;
    ((uint8_t *)&result)[0] = f.read();
    ((uint8_t *)&result)[1] = f.read();
    return result;
}

uint32_t read32(File& f) {
    uint32_t result;
    ((uint8_t *)&result)[0] = f.read();
    ((uint8_t *)&result)[1] = f.read();
    ((uint8_t *)&result)[2] = f.read();
    ((uint8_t *)&result)[3] = f.read();
    return result;
}

// קורא את מידות התמונה כדי שנוכל לחשב היכן לשבור שורות
void getBmpDim(const char *filename, int32_t &w, int32_t &h) {
    w = 0; h = 0;
    File bmpFS = LittleFS.open(filename, "r");
    if (!bmpFS) return;
    if (read16(bmpFS) == 0x4D42) {
        read32(bmpFS);
        read32(bmpFS);
        read32(bmpFS);
        read32(bmpFS);
        w = read32(bmpFS);
        h = abs((int32_t)read32(bmpFS)); 
    }
    bmpFS.close();
}

void drawBmp(const char *filename, int16_t x, int16_t y) {
    File bmpFS = LittleFS.open(filename, "r");
    if (!bmpFS) return;

    if (read16(bmpFS) == 0x4D42) {
        read32(bmpFS);
        read32(bmpFS);
        uint32_t seekOffset = read32(bmpFS);
        read32(bmpFS);
        int32_t w = read32(bmpFS);
        int32_t h = abs((int32_t)read32(bmpFS));
        if (read16(bmpFS) == 1 && read16(bmpFS) == 1) { 
            bmpFS.seek(seekOffset);
            uint16_t padding = (4 - ((w + 7) / 8) % 4) % 4;
            uint8_t lineBuffer[(w + 7) / 8 + padding];
            for (int32_t row = 0; row < h; row++) {
                bmpFS.read(lineBuffer, sizeof(lineBuffer));
                uint8_t *bptr = lineBuffer;
                uint8_t mask = 0x80;
                for (int32_t col = 0; col < w; col++) {
                    if (!(*bptr & mask)) { 
                        display.drawPixel(x + col, y + (h - 1 - row), GxEPD_BLACK);
                    }
                    mask >>= 1;
                    if (mask == 0) {
                        mask = 0x80;
                        bptr++;
                    }
                }
            }
        }
    }
    bmpFS.close();
}

// מבנה נתונים שישמור איך לסדר את השורות על המסך
struct LineInfo {
    int startIndex;
    int wordCount;
    int totalWidth;
    int maxHeight;
};

void setup() {
    Serial.begin(115200);

    // טעינת אזור הזמן לזיכרון מיד בהתעוררות כדי שיחול על השעון הפנימי
    setenv("TZ", tzInfo, 1);
    tzset();

    // ==============================================================
    // 1. מנגנון חלון זמן לעדכונים (OTA / USB Flash Grace Period)
    // ==============================================================
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
        // המכשיר התעורר בגלל כפתור RESET או חיבור לחשמל
        Serial.println("\n[SYSTEM] Cold boot or Reset detected!");
        Serial.println("[SYSTEM] Waiting 20 seconds to allow code updates...");
        delay(20000); 
        Serial.println("[SYSTEM] Grace period over. Resuming boot.");
    } else {
        // המכשיר התעורר משינה כי עברה דקה (שעון מעורר)
        delay(1500); // השהייה קלה רק כדי שהמחשב יספיק לזהות את ה-USB אם נרצה לוגים
        Serial.println("\n[SYSTEM] Woke up from deep sleep (Minute update). Fast execution.");
    }

    // ==============================================================
    // 2. אתחול והכנת נתונים
    // ==============================================================
    if (!LittleFS.begin(true)) {
        Serial.println("[ERROR] LittleFS Mount Failed!");
        return;
    }

    if (!timeSynchronized) syncTimeFromNTP();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;

    int displayHour = h % 12;
    if (displayHour == 0) displayHour = 12;

    String hourKey = String(displayHour);
    String min1Key = "";
    String min2Key = "";
    String min3Key = "";
    
    // ==============================================================
    // לוגיקת בחירת המילים - תקינות תחבירית מלאה בעברית
    // ==============================================================
    if (m == 0) {
        min1Key = "exactly";        // "בדיוק"
    } else if (m == 15) {
        min1Key = "quarter";        // "ורבע"
    } else if (m == 30) {
        min1Key = "half";           // "וחצי"
    } else if (m == 1) {
        min1Key = "and_1_minute";   // "ודקה"
    } else if (m == 2) {
        min1Key = "and_2_minutes";  // "ושתי דקות"
    } else {
        int tens = (m / 10) * 10; // עשרות: 10, 20, 30, 40, 50
        int units = m % 10;       // יחידות: 1 עד 9
        
        if (m >= 11 && m <= 19) {
            // 11 עד 19: "ואחת עשרה", "ושתים עשרה" וכו'
            min1Key = "and_" + String(m); 
        } else {
            // טיפול בעשרות (למשל 20, 40) - חובה להוסיף ו' החיבור!
            if (tens > 0) {
                min1Key = "and_" + String(tens); // מחפש קובץ "and_20" ("ועשרים")
            }
            
            // טיפול ביחידות (1-9)
            if (units > 0 && tens == 0) {
                // דקות 3-9: "ושלוש", "ותשע"
                min1Key = "and_" + String(units); 
            } else if (units > 0 && tens > 0) {
                // דקות מעורבות כמו 21, 45: "ואחת", "וחמש"
                min2Key = "and_" + String(units); 
            }
        }
        // הוספת המילה "דקות" בסוף המשפט
        min3Key = "minutes"; 
    }

    String podKey = "night";
    if (h >= 0 && h < 5) podKey = "early_morning";
    else if (h >= 5 && h < 12) podKey = "morning";
    else if (h >= 12 && h < 16) podKey = "noon";
    else if (h >= 16 && h < 20) podKey = "afternoon";
    else if (h >= 20 && h <= 23) podKey = "evening";

    // איסוף המילים שצריך להדפיס למערך
    String activeWords[5];
    int totalWords = 0;
    
    activeWords[totalWords++] = "/" + hourKey + ".bmp";
    if (min1Key != "") activeWords[totalWords++] = "/" + min1Key + ".bmp";
    if (min2Key != "") activeWords[totalWords++] = "/" + min2Key + ".bmp";
    if (min3Key != "") activeWords[totalWords++] = "/" + min3Key + ".bmp";
    activeWords[totalWords++] = "/" + podKey + ".bmp";

    // הוצאת מידות (רוחב וגובה) מכל תמונה
    int32_t wordW[5], wordH[5];
    for (int i = 0; i < totalWords; i++) {
        getBmpDim(activeWords[i].c_str(), wordW[i], wordH[i]);
    }

    // ==============================================================
    // 3. אלגוריתם Word Wrap חכם וכיוונון מרווחים
    // ==============================================================
    const int MAX_SCREEN_WIDTH = 760; // 40 פיקסלים שוליים בסך הכל למראה מאוורר
    const int WORD_SPACING = 30;      // הרווח האופקי בין מילה למילה באותה שורה
    const int LINE_SPACING = 20;      // הרווח האנכי בין השורות
    
    LineInfo lines[5];
    int lineCount = 0;
    
    int currentLineWidth = 0;
    int currentLineMaxH = 0;
    int currentStartIndex = 0;
    int currentWordCount = 0;

    for (int i = 0; i < totalWords; i++) {
        // חישוב רוחב המילה הנוכחית + רווח (אם זו לא המילה הראשונה בשורה)
        int addedWidth = wordW[i] + (currentWordCount > 0 ? WORD_SPACING : 0);
        
        // חריגה מהמסך? נשבור שורה (אלא אם זו המילה הראשונה בה)
        if (currentLineWidth + addedWidth > MAX_SCREEN_WIDTH && currentWordCount > 0) {
            lines[lineCount] = {currentStartIndex, currentWordCount, currentLineWidth, currentLineMaxH};
            lineCount++;
            
            // פתיחת שורה חדשה
            currentStartIndex = i;
            currentWordCount = 1;
            currentLineWidth = wordW[i];
            currentLineMaxH = wordH[i];
        } else {
            // צירוף המילה לשורה הנוכחית
            currentLineWidth += addedWidth;
            if (wordH[i] > currentLineMaxH) currentLineMaxH = wordH[i];
            currentWordCount++;
        }
    }
    if (currentWordCount > 0) {
        lines[lineCount] = {currentStartIndex, currentWordCount, currentLineWidth, currentLineMaxH};
        lineCount++;
    }

    // חישוב הגובה הכולל של הבלוק כדי למרכז אותו באמצע המסך (ציר Y)
    int totalBlockHeight = 0;
    for (int i = 0; i < lineCount; i++) {
        totalBlockHeight += lines[i].maxHeight;
    }
    totalBlockHeight += (lineCount - 1) * LINE_SPACING;
    
    // נקודת ההתחלה האנכית המושלמת
    int currentY = (480 - totalBlockHeight) / 2; 

    // ==============================================================
    // 4. הדפסה למסך (מימין לשמאל - RTL)
    // ==============================================================
    display.init(115200);
    display.fillScreen(GxEPD_WHITE);
    display.firstPage();

    do {
        int tempY = currentY; 
        
        for (int i = 0; i < lineCount; i++) {
            // מירכוז אופקי של השורה הספציפית הזו
            int lineStartX = (800 - lines[i].totalWidth) / 2;
            
            // מתחילים מהקצה הימני של הבלוק וזזים שמאלה
            int currentX = lineStartX + lines[i].totalWidth;
            
            for (int w = 0; w < lines[i].wordCount; w++) {
                int wordIdx = lines[i].startIndex + w;
                
                // תזוזה שמאלה כדי לפנות מקום למילה
                currentX -= wordW[wordIdx]; 
                
                drawBmp(activeWords[wordIdx].c_str(), currentX, tempY);
                
                // תזוזה נוספת שמאלה עבור הרווח למילה הבאה
                currentX -= WORD_SPACING; 
            }
            // ירידה לשורה הבאה
            tempY += lines[i].maxHeight + LINE_SPACING;
        }
    } while (display.nextPage());
    
    display.powerOff();
    
    // ==============================================================
    // 5. חזרה לשינה עמוקה עד לתחילת הדקה הבאה בדיוק
    // ==============================================================
    int secondsToNextMinute = 60 - timeinfo.tm_sec; 
    
    // הגנה מבאגים: אם החישוב חריג, נישן 60 שניות (עדכון רגיל)
    if (secondsToNextMinute <= 0 || secondsToNextMinute > 60) {
        secondsToNextMinute = 60;
    }
    
    Serial.printf("[SYSTEM] Routine complete. Deep sleeping for %d seconds...\n", secondsToNextMinute);
    esp_sleep_enable_timer_wakeup(secondsToNextMinute * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}