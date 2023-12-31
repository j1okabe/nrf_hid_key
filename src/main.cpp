#include <Arduino.h>
#include <bluefruit.h>
// #include <Adafruit_LittleFS.h>
// #include <InternalFileSystem.h>
#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
#include "SPI.h"
#include "SdFat.h"
#include "simpleiniread.h"
#include <OneButton.h>
// #define DISK_BLOCK_NUM 16
// #define DISK_BLOCK_SIZE 512
// #define RAMDISK
// #include "ramdisk.h"
// #define DEBUG
/*
.pio\libdeps\xiaoble_adafruit_nrf52\Adafruit
SPIFlash\src\Adafruit_SPIFlashBase.cpp line 111 add  P25Q16H

.pio\libdeps\xiaoble_adafruit_nrf52\Adafruit SPIFlash\src\flash_devices.h line
537 .max_clock_speed_mhz = 104, .quad_enable_bit_mask = 0x02,


*/
const char *basedevicename = "BTCUSTKBD_";
const char *inifilename = "config.ini";
char mydevicename[14] = {0};
#define KEYSNUM 10
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
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS,
                                           EXTERNAL_FLASH_USE_SPI);

#else
#error No QSPI/SPI flash are defined on your board variant.h !
#endif
#endif

// Adafruit_SPIFlash flash(&flashTransport);
Adafruit_SPIFlash flash(&flashTransport);
// file system object from SdFat
FatFileSystem fatfs;

FatFile root;
FatFile file;

// USB Mass Storage object
Adafruit_USBD_MSC usb_msc;

// Set to true when PC write to flash
bool fs_changed;
uint32_t lastflashed;
#define BAT_AVERAGE_COUNT 16
#define BAT_AVERAGE_MASK 0x000F
// #define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC
// resolution = 3000mV/4096
#define VBAT_MV_PER_LSB \
    (0.439453126F) // 1.8V ADC range and 12-bit ADC resolution = 1800mV/4096

BLEDis bledis;
BLEHidAdafruit blehid;
BLEBas blebas;
bool hasKeyPressed = false;
uint32_t lastMeasure;
int8_t vindex;
int8_t count;
uint8_t lastnotify;
uint16_t rawvalues[BAT_AVERAGE_COUNT] = {0};
uint8_t const conv_table[128][2] = {HID_ASCII_TO_KEYCODE};
//  keycode[0] = conv_table[chr][1];

hid_keyboard_report_t keycombi_report[KEYSNUM];
int work_LED_status = HIGH;
enum Mymodifier
{
    myWIN,
    myCMD,
    myCTRL,
    myALT,
    myOPT
};

enum mycombi
{
    MyKeyCombi_0,
    MyKeyCombi_1,
    MyKeyCombi_2,
    MyKeyCombi_3,
    MyKeyCombi_4,
    MyKeyCombi_5,
    MyKeyCombi_6,
    MyKeyCombi_7,
    MyKeyCombi_8,
    MyKeyCombi_9,
    MyKeyCombi_10
};
// Setup buttons
OneButton button0(D1, true);
OneButton button1(D2, true);
OneButton button2(D3, true);
OneButton button3(D4, true);
OneButton button4(D5, true);
OneButton button5(D6, true);
OneButton button6(D7, true);
OneButton button7(D8, true);
OneButton button8(D9, true);
OneButton button9(D10, true);
OneButton button10(PIN_NFC1, true);
OneButton button11(PIN_NFC2, true);
void loadmapfile(void);
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
void myKeyboardReport(mycombi combi)
{
    BLEConnection *connection = Bluefruit.Connection(0);
    if (connection && connection->connected() && connection->secured())
    {
        blehid.keyboardReport(&keycombi_report[combi]);
    }
    hasKeyPressed = true;
    Serial.print("button");
    Serial.println((int)combi);
    delay(5);
}
void myBasNotyfy(uint8_t value)
{
    BLEConnection *connection = Bluefruit.Connection(0);
    if (connection && connection->connected() && connection->secured())
    {
        blebas.notify(value);
    }
}
void click0(void) { myKeyboardReport(MyKeyCombi_0); }
void click1(void) { myKeyboardReport(MyKeyCombi_1); }
void click2(void) { myKeyboardReport(MyKeyCombi_2); }
void click3(void) { myKeyboardReport(MyKeyCombi_3); }
void click4(void) { myKeyboardReport(MyKeyCombi_4); }
void click5(void) { myKeyboardReport(MyKeyCombi_5); }
void click6(void) { myKeyboardReport(MyKeyCombi_6); }
void click7(void) { myKeyboardReport(MyKeyCombi_7); }
void click8(void) { myKeyboardReport(MyKeyCombi_8); }
void click9(void) { myKeyboardReport(MyKeyCombi_9); }
void click10(void) { myKeyboardReport(MyKeyCombi_10); }

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
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize);
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize);
void msc_flush_cb(void);

void setup()
{
    char lastletter[3] = {0};
    // Enable DC-DC converter
    NRF_POWER->DCDCEN = 1;
    MyKeyCombi_init();

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(D9, OUTPUT);
    digitalWrite(D9, LOW); // for button6
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, work_LED_status);

    button0.attachClick(click0);
    button1.attachClick(click1);
    button2.attachClick(click2);
    button3.attachClick(click3);
    button4.attachClick(click4);
    button5.attachClick(click5);
    button6.attachLongPressStart(longpress6);
    // delay(100);
    Bluefruit.Periph.setConnIntervalMS(30, 120);
    Bluefruit.begin();
    Bluefruit.autoConnLed(0);
    Bluefruit.setTxPower(0); // Check bluefruit.h for supported values
                             // Configure and Start Device Information Service
    bledis.setManufacturer("j1okabe");
    bledis.setModel("xiao ble");
    ble_gap_addr_t myaddres = Bluefruit.getAddr();
    snprintf(lastletter, 3, "%2x", myaddres.addr[0]);
    memcpy(mydevicename, basedevicename, strlen(basedevicename));
    memcpy(&mydevicename[strlen(basedevicename)], lastletter, strlen(lastletter));
    Bluefruit.setName(mydevicename);
    // Bluefruit.setName("nRF52Keyboard");
    Bluefruit.Periph.setConnectCallback(connect_callback);
    Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
    Bluefruit.Periph.setConnSlaveLatency(20);
    Bluefruit.Periph.setConnSupervisionTimeoutMS(4000);

    bledis.begin();
    blebas.begin();
    lastnotify = 100;
    blebas.write(lastnotify);
    /* Start BLE HID
     * Note: Apple requires BLE device must have min connection interval >= 20m
     * ( The smaller the connection interval the faster we could send data).
     * However for HID and MIDI device, Apple could accept min connection
     * interval up to 11.25 ms. Therefore BLEHidAdafruit::begin() will try to
     * set the min and max connection interval to 11.25  ms and 15 ms
     * respectively for best performance.
     */
    blehid.begin();
    // Bluefruit.Periph.setConnIntervalMS(30, 120);
    Bluefruit.Periph.setConnInterval(18, 24);
    delay(100);
    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk)
    {
        Serial.begin(115200);
        delay(10);
#ifdef DEBUG
        while (!Serial)
            delay(10); // wait for native usb
#endif
        // VBUS present
        Serial.println("VBUS present");
        flashTransport.begin();
        flashTransport.runCommand(0xAB);

        if (flash.begin() == false)
        {
            Serial.println("flash.begin false");
        };
        // Set disk vendor id, product id and revision with string up to 8, 16,
        // 4 characters respectively usb_msc.setID("Adafruit", "External Flash",
        // "1.0");
        usb_msc.setID("Adafruit", "Mass Storage", "1.0");

        // Set callback
        usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);

        // Set disk size, block size should be 512 regardless of spi flash page
        // size

        usb_msc.setCapacity(flash.size() / 512, 512);
        // usb_msc.setCapacity(DISK_BLOCK_NUM, DISK_BLOCK_SIZE);

        // MSC is ready for read/write
        usb_msc.setUnitReady(true);

        usb_msc.begin();
        fatfs.begin(&flash);

        Serial.println("Adafruit TinyUSB Mass Storage External Flash example");
        Serial.print("JEDEC ID: 0x");
        Serial.println(flash.getJEDECID(), HEX);
        Serial.print("Flash size: ");
        Serial.print(flash.size() / 1024);
        Serial.println(" KB");
        int16_t pagesize = flash.pageSize();
        Serial.print(F("Page size: "));
        Serial.println(pagesize);
        int16_t numpages = flash.numPages();
        Serial.print(F("Page num: "));
        Serial.println(numpages);
#if 0
        while (!Serial)
            delay(10); // wait for native usb
#endif
        loadmapfile();
    }
    else
    {
#ifdef DEBUG
        while (!Serial)
            delay(10); // wait for native usb
#endif
        Serial.println("VBUS NOT present");
        if (flash.begin())
        {
            loadmapfile();
        };

        QSPIF_sleep();
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
    Bluefruit.Advertising.start(
        0); // 0 = Don't stop advertising after n seconds
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
    uint32_t ms2 = millis();
    if (fs_changed && ((ms2 - lastflashed) > 1000))
    {
#if 0
        if (!root.open("/"))
        {
            Serial.println("open root failed");
            return;
        }

        Serial.println("Flash contents:");

        // Open next file in root.
        // Warning, openNext starts at the current directory position
        // so a rewind of the directory may be required.
        while (file.openNext(&root, O_RDONLY))
        {
            file.printFileSize(&Serial);
            Serial.write(' ');
            file.printName(&Serial);
            if (file.isDir())
            {
                // Indicate a directory.
                Serial.write('/');
            }
            Serial.println();
            file.close();
        }

        root.close();
#else
        loadmapfile();
#endif
        Serial.println();
        fs_changed = false;
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

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize)
{
#ifdef RAMDISK
    uint8_t const *addr = msc_disk[lba];
    memcpy(buffer, addr, bufsize);

    return bufsize;
#else
    // Note: SPIFLash Bock API: readBlocks/writeBlocks/syncBlocks
    // already include 4K sector caching internally. We don't need to cache it,
    // yahhhh!!
    return flash.readBlocks(lba, (uint8_t *)buffer, bufsize / 512) ? bufsize
                                                                   : -1;
#endif
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize)
{
#ifdef RAMDISK
    uint8_t *addr = msc_disk[lba];
    memcpy(addr, buffer, bufsize);

    return bufsize;
#else
    digitalWrite(LED_GREEN, LOW);

    // Note: SPIFLash Bock API: readBlocks/writeBlocks/syncBlocks
    // already include 4K sector caching internally. We don't need to cache it,
    // yahhhh!!
    return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
#endif
}

// Callback invoked when WRITE10 command is completed (status received and
// accepted by host). used to flush any pending cache.
void msc_flush_cb(void)
{
#ifdef RAMDISK
#else
    // sync with flash
    flash.syncBlocks();

    // clear file system's cache to force refresh
    fatfs.cacheClear();

    fs_changed = true;
    lastflashed = millis();

    digitalWrite(LED_GREEN, HIGH);
#endif
}

void loadmapfile(void)
{
    char iniheader[8] = {0};
    const char modifier[] = "modifier";
    const char *modifierstr[] = {"WIN", "CMD", "CTRL", "ALT", "OPT"};
    const char key[] = "key";
    const char pad[] = "PAD";
    char positionstr[3] = {0};
    char *readstr;
    String loadsettingstr;
    int keystrlen;
    char *padpos;
    int padstrlen;

    if (file.open(inifilename, O_RDONLY))
    {

        // open succsess
        for (int i = 0; i < KEYSNUM; i++)
        {

            memclr(iniheader, sizeof(iniheader));
            memcpy(iniheader, "key", 3);
            sprintf(positionstr, "%d", i + 1);
            loadsettingstr = String(positionstr);
            memcpy(&iniheader[strlen(iniheader)], positionstr, strlen(positionstr));
            readstr = inifileString(file, (char *)iniheader, (char *)modifier);
            if (readstr != NULL)
            {

                // loadsettingstr.concat(i);
                loadsettingstr.concat(" : ");
                keycombi_report[i].modifier = 0;
                if (strstr(readstr, modifierstr[myWIN]) != NULL)
                {
                    keycombi_report[i].modifier = KEYBOARD_MODIFIER_LEFTGUI;
                    loadsettingstr.concat("GUI ");
                }
                if (strstr(readstr, modifierstr[myCMD]) != NULL)
                {
                    keycombi_report[i].modifier |= KEYBOARD_MODIFIER_LEFTGUI;
                    loadsettingstr.concat("GUI ");
                }
                if (strstr(readstr, modifierstr[myCTRL]) != NULL)
                {
                    keycombi_report[i].modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
                    loadsettingstr.concat("CTRL ");
                }
                if (strstr(readstr, modifierstr[myALT]) != NULL)
                {
                    keycombi_report[i].modifier |= KEYBOARD_MODIFIER_LEFTALT;
                    loadsettingstr.concat("ALT ");
                }
                if (strstr(readstr, modifierstr[myOPT]) != NULL)
                {
                    keycombi_report[i].modifier |= KEYBOARD_MODIFIER_LEFTALT;
                    loadsettingstr.concat("ALT ");
                }
            }
            else
            {
                keycombi_report[i].modifier = 0;
                loadsettingstr.concat("NO_MIDIFIER ");
            }
            keycombi_report[i].keycode[0] = 0;
            free(readstr);
            readstr = inifileString(file, (char *)iniheader, (char *)key);
            keystrlen = strlen(readstr);
            switch (keystrlen)
            {
            case 0:
                loadsettingstr.concat("NO_KEY ");
                break;
            case 1:
                if (readstr != NULL && readstr[0] < 128)
                {
                    int tnum = readstr[0];
                    keycombi_report[i].keycode[0] = conv_table[tnum][1];
                    if (conv_table[tnum][0] == 1)
                    {
                        keycombi_report[i].modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
                        loadsettingstr.concat("SHIFT ");
                    }
                    loadsettingstr.concat(readstr[0]);
                }
                else
                {
                    loadsettingstr.concat("NO_KEY ");
                }
                break;
            case 4:
                padpos = strstr(readstr, pad);
                padstrlen = strlen(padpos);
                if (padpos != NULL && padstrlen == 4)
                {
                    switch (padpos[3])
                    {
                    case '*':
                        keycombi_report[i].keycode[0] = HID_KEY_KEYPAD_MULTIPLY;
                        break;
                    case '+':
                        keycombi_report[i].keycode[0] = HID_KEY_KEYPAD_ADD;
                        break;
                    case '-':
                        keycombi_report[i].keycode[0] = HID_KEY_KEYPAD_SUBTRACT;
                        break;
                    default:
                        keycombi_report[i].keycode[0] = 0;
                        break;
                    }
                    loadsettingstr.concat(readstr);
                }
                else
                {
                    loadsettingstr.concat("NO_KEY ");
                }
                break;

            default:
                loadsettingstr.concat("NO_KEY ");
                break;
            }

            free(readstr);
            Serial.print(loadsettingstr);
            Serial.println();
        }
        file.close();
    }
}
// https://days-of-programming.blogspot.com/search/label/nRF52840

// XIAO BLEをArduino開発するときの2種のボードライブラリの違い
// https://zenn.dev/ukkz/articles/a9ec6fc37b68b7

// XIAO nRF52840 のVBUS判定
// https://lipoyang.hatenablog.com/entry/2023/01/01/102737
