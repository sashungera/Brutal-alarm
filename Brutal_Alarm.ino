/**
 * Brutal Alarm System
 * ============================================================
 * Hardware (Lolin32 Lite):
 *   I2S MAX98357A : BCLK=26, LRC=27, DIN=25
 *   OLED SSD1306  : SDA=16, SCL=4,  addr=0x3C  (Wire0)
 *   RTC DS3231    : SDA=33, SCL=32             (Wire1)
 *   Button MODE   : GPIO 22 (INPUT_PULLUP)
 *   Button HOURS  : GPIO 23 (INPUT_PULLUP)
 *   Button MINS   : GPIO 18 (INPUT_PULLUP)
 *   LED           : GPIO 2  (PWM)
 *
 * Alarm behaviour:
 *   - Cannot be stopped for first 60 seconds (LOCK period)
 *   - Volume ramps from quiet to max over 60 seconds
 *   - LED fades from off to full brightness over 60 seconds
 *   - After 60s MODE button stops the alarm
 *   - alarmFired flag prevents re-triggering within same minute
 *
 * Dependencies:
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - RTClib (Adafruit)
 *   - ESP32 Arduino core (built-in I2S driver)
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <driver/i2s.h>

// --- Pin Definitions ----------------------------------------
#define PIN_I2S_BCLK    26
#define PIN_I2S_LRC     27
#define PIN_I2S_DIN     25

#define PIN_OLED_SDA    16
#define PIN_OLED_SCL     4

#define PIN_RTC_SDA     33
#define PIN_RTC_SCL     32

#define PIN_BTN_MODE    22
#define PIN_BTN_HOURS   23
#define PIN_BTN_MINS    18

#define PIN_LED          2

// --- OLED ---------------------------------------------------
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_ADDR   0x3C
#define OLED_RESET    -1

// --- I2S ----------------------------------------------------
#define I2S_PORT            I2S_NUM_0
#define I2S_SAMPLE_RATE     16000
#define I2S_DMA_BUF_COUNT   4
#define I2S_DMA_BUF_LEN     256

// --- Timing -------------------------------------------------
#define DEBOUNCE_MS          50
#define DISPLAY_REFRESH_MS  400
#define TONE_CHUNK_MS        20

// --- Alarm behaviour ----------------------------------------
#define ALARM_LOCK_MS      60000
#define ALARM_RAMP_MS      60000
#define TONE_FREQ_HZ         880
#define TONE_AMP_MIN         200
#define TONE_AMP_MAX       16000

// --- LED PWM ------------------------------------------------
#define LED_PWM_FREQ      5000
#define LED_PWM_RES       8

// --- State Machine ------------------------------------------
enum SystemState {
  STATE_NORMAL,
  STATE_SETTING,
  STATE_ALARM
};

// --- Globals ------------------------------------------------
TwoWire wireOLED = TwoWire(0);
TwoWire wireRTC  = TwoWire(1);

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &wireOLED, OLED_RESET);
RTC_DS3231 rtc;

SystemState state = STATE_NORMAL;

uint8_t  alarmHour   = 6;
uint8_t  alarmMinute = 0;

char     displayBuf[32];
uint32_t lastDisplayRefresh = 0;

uint32_t alarmStartMs     = 0;
uint32_t lastToneChunk    = 0;
uint32_t tonePhaseCounter = 0;

bool alarmFired = false;

struct Button {
  uint8_t  pin;
  bool     lastReading;
  bool     stableState;
  uint32_t lastChangeTime;
};

Button btnMode  = { PIN_BTN_MODE,  HIGH, HIGH, 0 };
Button btnHours = { PIN_BTN_HOURS, HIGH, HIGH, 0 };
Button btnMins  = { PIN_BTN_MINS,  HIGH, HIGH, 0 };

bool pressedMode  = false;
bool pressedHours = false;
bool pressedMins  = false;

// --- Forward Declarations -----------------------------------
void initI2S();
void stopI2S();
void pushToneChunk(int16_t amplitude);
void updateButton(Button &btn, bool &flag);
void handleButtons();
void updateDisplay(const DateTime &now);
void enterAlarm();
void exitAlarm();
void enterSetting();
void exitSetting();
int16_t currentAmplitude();
uint8_t currentLedBrightness();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_MODE,  INPUT_PULLUP);
  pinMode(PIN_BTN_HOURS, INPUT_PULLUP);
  pinMode(PIN_BTN_MINS,  INPUT_PULLUP);

  ledcAttach(PIN_LED, LED_PWM_FREQ, LED_PWM_RES);
  ledcWrite(PIN_LED, 0);

  wireOLED.begin(PIN_OLED_SDA, PIN_OLED_SCL, 400000UL);
  wireRTC.begin(PIN_RTC_SDA,   PIN_RTC_SCL,  400000UL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  if (!rtc.begin(&wireRTC)) {
    Serial.println("RTC not found - check GPIO 32/33");
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC reset to compile time");
  }

  Serial.println("Brutal Alarm ready.");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  uint32_t now_ms = millis();
  handleButtons();
  DateTime now = rtc.now();

  switch (state) {

    case STATE_NORMAL:
      if (pressedMode) {
        pressedMode = false;
        enterSetting();
        break;
      }
      pressedHours = pressedMins = false;

      if (now.hour() == alarmHour && now.minute() == alarmMinute && !alarmFired) {
        enterAlarm();
        break;
      }
      if (now.minute() != alarmMinute) {
        alarmFired = false;
      }

      if (now_ms - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
        lastDisplayRefresh = now_ms;
        updateDisplay(now);
      }
      break;

    case STATE_SETTING:
      if (pressedMode) {
        pressedMode = false;
        exitSetting();
        break;
      }
      if (pressedHours) {
        pressedHours = false;
        alarmHour = (alarmHour + 1) % 24;
      }
      if (pressedMins) {
        pressedMins = false;
        alarmMinute = (alarmMinute + 1) % 60;
      }

      if (now_ms - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
        lastDisplayRefresh = now_ms;
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("  SET ALARM");
        display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
        display.setTextSize(3);
        display.setCursor(10, 20);
        snprintf(displayBuf, sizeof(displayBuf), "%02d:%02d", alarmHour, alarmMinute);
        display.print(displayBuf);
        display.setTextSize(1);
        display.setCursor(0, 54);
        display.print("H=+hr  M=+min  MODE=save");
        display.display();
      }
      break;

    case STATE_ALARM: {
      uint32_t elapsed = now_ms - alarmStartMs;
      bool locked = (elapsed < ALARM_LOCK_MS);

      if (pressedMode && !locked) {
        pressedMode = false;
        exitAlarm();
        break;
      }
      pressedMode = pressedHours = pressedMins = false;

      if (now_ms - lastToneChunk >= TONE_CHUNK_MS) {
        lastToneChunk = now_ms;
        pushToneChunk(currentAmplitude());
      }

      ledcWrite(PIN_LED, currentLedBrightness());

      if (now_ms - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
        lastDisplayRefresh = now_ms;
        display.clearDisplay();
        display.setTextSize(3);
        display.setCursor(4, 4);
        display.println("ALARM!");

        display.setTextSize(1);
        if (locked) {
          uint32_t remaining = (ALARM_LOCK_MS - elapsed) / 1000 + 1;
          snprintf(displayBuf, sizeof(displayBuf), "Wait: %lu sec", remaining);
          display.setCursor(20, 50);
          display.print(displayBuf);
        } else {
          display.setCursor(15, 50);
          display.print("MODE = stop");
        }
        display.display();
      }
      break;
    }
  }
}

// ============================================================
//  AMPLITUDE & LED RAMP
// ============================================================
int16_t currentAmplitude() {
  uint32_t elapsed = millis() - alarmStartMs;
  if (elapsed >= ALARM_RAMP_MS) return TONE_AMP_MAX;
  int32_t amp = TONE_AMP_MIN + (int32_t)(TONE_AMP_MAX - TONE_AMP_MIN) * elapsed / ALARM_RAMP_MS;
  return (int16_t)amp;
}

uint8_t currentLedBrightness() {
  uint32_t elapsed = millis() - alarmStartMs;
  if (elapsed >= ALARM_RAMP_MS) return 255;
  return (uint8_t)(255UL * elapsed / ALARM_RAMP_MS);
}

// ============================================================
//  STATE TRANSITIONS
// ============================================================
void enterAlarm() {
  alarmFired = true;
  state = STATE_ALARM;
  alarmStartMs      = millis();
  tonePhaseCounter  = 0;
  lastToneChunk     = millis();
  lastDisplayRefresh = 0;
  initI2S();
  Serial.println("-> ALARM");
  pressedMode = false;
}

void exitAlarm() {
  state = STATE_NORMAL;
  stopI2S();
  ledcWrite(PIN_LED, 0);
  lastDisplayRefresh = 0;
  Serial.println("-> NORMAL (alarm stopped)");
  pressedMode = false;
}

void enterSetting() {
  state = STATE_SETTING;
  lastDisplayRefresh = 0;
  Serial.printf("-> SETTING (alarm=%02d:%02d)\n", alarmHour, alarmMinute);
  delay(200);
}

void exitSetting() {
  state = STATE_NORMAL;
  lastDisplayRefresh = 0;
  alarmFired = false;
  pressedMode = false;
  Serial.printf("-> NORMAL (saved=%02d:%02d)\n", alarmHour, alarmMinute);
  delay(200);
}

// ============================================================
//  DISPLAY - NORMAL mode
// ============================================================
void updateDisplay(const DateTime &now) {
  display.clearDisplay();

  display.setTextSize(3);
  display.setCursor(4, 8);
  snprintf(displayBuf, sizeof(displayBuf), "%02d:%02d", now.hour(), now.minute());
  display.print(displayBuf);

  display.setTextSize(2);
  display.setCursor(92, 16);
  snprintf(displayBuf, sizeof(displayBuf), "%02d", now.second());
  display.print(displayBuf);

  display.drawLine(0, 46, 127, 46, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 52);
  snprintf(displayBuf, sizeof(displayBuf), "ALM: %02d:%02d", alarmHour, alarmMinute);
  display.print(displayBuf);

  display.display();
}

// ============================================================
//  BUTTON DEBOUNCE
// ============================================================
void updateButton(Button &btn, bool &flag) {
  bool     reading = digitalRead(btn.pin);
  uint32_t now_ms  = millis();

  if (reading != btn.lastReading) {
    btn.lastChangeTime = now_ms;
    btn.lastReading    = reading;
  }

  if ((now_ms - btn.lastChangeTime) >= DEBOUNCE_MS) {
    if (btn.stableState == HIGH && reading == LOW) {
      flag = true;
    }
    btn.stableState = reading;
  }
}

void handleButtons() {
  updateButton(btnMode,  pressedMode);
  updateButton(btnHours, pressedHours);
  updateButton(btnMins,  pressedMins);
}

// ============================================================
//  I2S
// ============================================================
void initI2S() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = I2S_SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = I2S_DMA_BUF_COUNT,
    .dma_buf_len          = I2S_DMA_BUF_LEN,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num   = PIN_I2S_BCLK,
    .ws_io_num    = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DIN,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

void stopI2S() {
  i2s_zero_dma_buffer(I2S_PORT);
  i2s_driver_uninstall(I2S_PORT);
}

// ============================================================
//  TONE GENERATION - triangle wave with dynamic amplitude
// ============================================================
void pushToneChunk(int16_t amplitude) {
  const uint32_t samplesPerChunk = (I2S_SAMPLE_RATE * TONE_CHUNK_MS) / 1000;
  const uint32_t period          = I2S_SAMPLE_RATE / TONE_FREQ_HZ;

  static int16_t buf[512];
  uint32_t count = (samplesPerChunk > 512) ? 512 : samplesPerChunk;

  for (uint32_t i = 0; i < count; i++) {
    uint32_t phase = tonePhaseCounter % period;
    int32_t  half  = period / 2;
    int16_t  sample;
    if (phase < (uint32_t)half) {
      sample = (int16_t)(amplitude * (2 * (int32_t)phase - (int32_t)half) / half);
    } else {
      sample = (int16_t)(amplitude * ((int32_t)half - 2 * ((int32_t)phase - half)) / half);
    }
    buf[i] = sample;
    tonePhaseCounter++;
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, buf, count * sizeof(int16_t), &bytesWritten, 0);
}
