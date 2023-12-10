#include <Arduino.h>
// #include "SdFat.h"
#include "Adafruit_SPIFlash.h"
#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <OneButton.h>

#if defined(CUSTOM_CS) && defined(CUSTOM_SPI)
Adafruit_FlashTransport_SPI flashTransport(CUSTOM_CS, CUSTOM_SPI);

#elif defined(ARDUINO_ARCH_ESP32)
// ESP32 use same flash device that store code.
// Therefore there is no need to specify the SPI and SS
Adafruit_FlashTransport_ESP32 flashTransport;

#else
// On-board external flash (QSPI or SPI) macros should already
// defined in your board variant if supported
// - EXTERNAL_FLASH_USE_QSPI
// - EXTERNAL_FLASH_USE_CS/EXTERNAL_FLASH_USE_SPI
#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;

#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);

#else
#error No QSPI/SPI flash are defined on your board variant.h !
#endif
#endif

// Adafruit_SPIFlash flash(&flashTransport);

// typedef volatile uint32_t REG32;
// #define pREG32 (REG32 *)

// #define DEVICE_ID_HIGH (*(pREG32(0x10000060)))
// #define DEVICE_ID_LOW (*(pREG32(0x10000064)))

#define BAT_AVERAGE_COUNT 16
#define BAT_AVERAGE_MASK 0x000F
// #define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_MV_PER_LSB (0.439453126F) // 1.8V ADC range and 12-bit ADC resolution = 1800mV/4096

BLEDis bledis;
BLEHidAdafruit blehid;
BLEBas blebas;
bool hasKeyPressed = false;
uint32_t lastMeasure;
int8_t vindex;
int8_t count;
uint8_t lastnotify;
uint16_t rawvalues[BAT_AVERAGE_COUNT] = {0};
hid_keyboard_report_t keycombi_report[6];
int work_LED_status = HIGH;
enum
{
  MyKeyCombi_0,
  MyKeyCombi_1,
  MyKeyCombi_2,
  MyKeyCombi_3,
  MyKeyCombi_4,
  MyKeyCombi_5
};
// Setup buttons
OneButton button0(D1, true);
OneButton button1(D2, true);
OneButton button2(D3, true);
OneButton button3(D4, true);
OneButton button4(D5, true);
OneButton button5(D6, true);
OneButton button6(D7, true);

int myFunction(int, int);
void MyKeyCombi_init(void)
{
  varclr(&keycombi_report);
  keycombi_report[MyKeyCombi_0].modifier = KEYBOARD_MODIFIER_LEFTGUI;
  keycombi_report[MyKeyCombi_0].keycode[0] = HID_KEY_E;
  // keycombi_report[MyKeyCombi_0].keycode[0] = HID_KEY_NONE;

  keycombi_report[MyKeyCombi_1].modifier = KEYBOARD_MODIFIER_LEFTCTRL;
  keycombi_report[MyKeyCombi_1].keycode[0] = HID_KEY_C;

  keycombi_report[MyKeyCombi_2].modifier = KEYBOARD_MODIFIER_LEFTCTRL;
  keycombi_report[MyKeyCombi_2].keycode[0] = HID_KEY_V;

  keycombi_report[MyKeyCombi_3].modifier = KEYBOARD_MODIFIER_LEFTCTRL;
  keycombi_report[MyKeyCombi_3].keycode[0] = HID_KEY_Z;

  keycombi_report[MyKeyCombi_4].modifier = KEYBOARD_MODIFIER_LEFTGUI;
  keycombi_report[MyKeyCombi_4].keycode[0] = HID_KEY_L;

  keycombi_report[MyKeyCombi_5].modifier = KEYBOARD_MODIFIER_LEFTGUI;
  // keycombi_report[MyKeyCombi_5].keycode[0] = HID_KEY_L;
  keycombi_report[MyKeyCombi_5].keycode[0] = HID_KEY_D;
}
void myKeyboardReport(hid_keyboard_report_t *report)
{
  BLEConnection *connection = Bluefruit.Connection(0);
  if (connection && connection->connected() && connection->secured())
  {
    blehid.keyboardReport(report);
  }
}
void myBasNotyfy(uint8_t value)
{
  BLEConnection *connection = Bluefruit.Connection(0);
  if (connection && connection->connected() && connection->secured())
  {
    blebas.notify(value);
  }
}
void click0(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_0]);
  hasKeyPressed = true;
  Serial.println("button0");
  delay(5);
}
void click1(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_1]);
  hasKeyPressed = true;
  Serial.println("button1");
  delay(5);
}
void click2(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_2]);
  hasKeyPressed = true;
  Serial.println("button2");
  delay(5);
}
void click3(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_3]);
  hasKeyPressed = true;
  Serial.println("button3");
  delay(5);
}
void click4(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_4]);
  hasKeyPressed = true;
  Serial.println("button4");
  delay(5);
}
void click5(void)
{
  myKeyboardReport(&keycombi_report[MyKeyCombi_5]);
  hasKeyPressed = true;
  Serial.println("button5");
  delay(5);
}
void longpress6(void)
{
  Bluefruit.Periph.clearBonds();
  Serial.println("clear bonding infos");
  delay(5);
  // digitalWrite(LED_RED, HIGH);
}
void QSPIF_sleep(void)
{
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  flashTransport.end();
}

void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
void setup()
{
  // Enable DC-DC converter
  NRF_POWER->DCDCEN = 1;
  MyKeyCombi_init();
  QSPIF_sleep();
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(D9, OUTPUT);
  digitalWrite(D9, LOW); // for button6
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, work_LED_status);
#if 0
  flash.begin();
  if (flash.deepPowerDown() == false)
  {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    while (1)
    {
      yield();
    }
  }
  flash.end();
#endif
  // put your setup code here, to run once:

  // int result = myFunction(2, 3);
  button0.attachClick(click0);
  button1.attachClick(click1);
  button2.attachClick(click2);
  button3.attachClick(click3);
  button4.attachClick(click4);
  button5.attachClick(click5);
  button6.attachLongPressStart(longpress6);
  Bluefruit.begin();
  Bluefruit.autoConnLed(0);
  Bluefruit.setTxPower(0); // Check bluefruit.h for supported values
                           // Configure and Start Device Information Service
  bledis.setManufacturer("j1okabe");
  bledis.setModel("xiao ble");
  Bluefruit.setName("nRF52Keyboard");
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  Bluefruit.Periph.setConnIntervalMS(50, 200);
  Bluefruit.Periph.setConnSlaveLatency(20);
  Bluefruit.Periph.setConnSupervisionTimeoutMS(4000);
  // Bluefruit.Periph.
  bledis.begin();
  blebas.begin();
  lastnotify = 100;
  blebas.write(lastnotify);
  /* Start BLE HID
   * Note: Apple requires BLE device must have min connection interval >= 20m
   * ( The smaller the connection interval the faster we could send data).
   * However for HID and MIDI device, Apple could accept min connection interval
   * up to 11.25 ms. Therefore BLEHidAdafruit::begin() will try to set the min and max
   * connection interval to 11.25  ms and 15 ms respectively for best performance.
   */
  blehid.begin();
  if (NRF_POWER->USBREGSTATUS & 0x0001)
  {
    Serial.begin(115200);
    delay(100);
    // VBUS present
    Serial.println("VBUS present");
  }
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  // Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);

  // Include BLE HID service
  Bluefruit.Advertising.addService(blehid);

  // Include BLE battery service
  Bluefruit.Advertising.addService(blebas);

  // There is enough room for the dev name in the advertising packet
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(20);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds
  lastMeasure = 0;
}

void loop()
{
  // int32_t rawtotal;
  uint32_t ms = millis();
  button0.tick();
  button1.tick();
  button2.tick();
  button3.tick();
  button4.tick();
  button5.tick();
  button6.tick();

  // Only send KeyRelease if previously pressed to avoid sending
  // multiple keyRelease reports (that consume memory and bandwidth)
  if (hasKeyPressed)
  {
    hasKeyPressed = false;
    blehid.keyRelease();

    // Delay a bit after a report
    delay(5);
  }
  if (ms - lastMeasure > 3000)
  {
    lastMeasure = ms;
    Bluefruit.autoConnLed(false);
    digitalWrite(LED_RED, HIGH);

    // pinMode(VBAT_ENABLE, OUTPUT);
    // digitalWrite(VBAT_ENABLE, LOW);
    analogReference(AR_INTERNAL_1_8);
    // Set the resolution to 12-bit (0..4095)
    analogReadResolution(12); // Can be 8, 10, 12 or 14
    // Let the ADC settle
    delay(1);

    rawvalues[vindex] = (uint16_t)analogRead(A0);
    // Set the ADC back to the default settings
    analogReference(AR_DEFAULT);
    analogReadResolution(10);
    // pinMode(VBAT_ENABLE, INPUT);
    vindex = (vindex + 1) & BAT_AVERAGE_MASK;
    count = min(count + 1, BAT_AVERAGE_COUNT);
    uint16_t rawtotal = 0;
    for (int i = 0; i < count; i++)
      rawtotal += rawvalues[i];
    // 10bit, Vref=3.6V, 分圧比1000:510
    // double volt = (double)rawtotal / count / 1024 * 3.6 / 510 * 1510;
    uint16_t mV = (double)rawtotal / count * VBAT_MV_PER_LSB;
    uint16_t volt1000 = mV;
    if (volt1000 > 1400)
    {
      volt1000 = 1400;
    }
    if (volt1000 < 900)
    {
      volt1000 = 900;
    }
    uint8_t value = (uint8_t)(map(volt1000, 900, 1400, 1, 100));
    if (lastnotify != value)
    {
      lastnotify = value;
#if 1
      if (mV <= 900)
      {
        myBasNotyfy(1);
      }
      else
      {
        myBasNotyfy(value);
      }
#endif
    }

    Serial.printf("battery mV %d notify %d", mV, value);
    Serial.println();
  }

  delay(50);
}

void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection *connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = {0};
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);
  // digitalWrite(LED_RED, HIGH);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void)conn_handle;
  (void)reason;

  Serial.println();
  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
}
// https://days-of-programming.blogspot.com/search/label/nRF52840

// XIAO BLEをArduino開発するときの2種のボードライブラリの違い
// https://zenn.dev/ukkz/articles/a9ec6fc37b68b7