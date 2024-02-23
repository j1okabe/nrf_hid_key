#include <Arduino.h>
#include <bluefruit.h>
#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
#include "SPI.h"
#include "SdFat.h"
#include "simpleiniread.h"
#include <OneButton.h>

// #define DEBUG
const char Ver_str[] = "100";
const char *basedevicename = "BTCUSTKBD_";
const char *inifilename = "config.ini";
char mydevicename[14] = {0};
#define KEYSNUM 10
#define KEYCLICKTIME (200)

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

#define COMMAND_WAKEUP (0xAB)
#define COMMAND_SLLEP (0xB9)
// Adafruit_SPIFlash flash(&flashTransport);
Adafruit_SPIFlash flash(&flashTransport);
// file system object from SdFat
FatFileSystem fatfs;

FatFile root;
FatFile file;

// USB Mass Storage object
Adafruit_USBD_MSC usb_msc;

// Set to true when PC write to flash
bool fs_changed = false;
uint32_t lastflashed;
#define FLASH_MONITOR_INTERVAL (1000)
#define BAT_MEASURE_INTERVAL (3000)
#define BAT_AVERAGE_COUNT 16
#define BAT_AVERAGE_MASK 0x000F
#define BAT_UPPER (1400)
// 1.8V ADC range and 12-bit ADC resolution = 1800mV/4096
#define VBAT_MV_PER_LSB \
    (0.439453126F)
#define PIN_INVCHG 23
#define PIN_HICHG 22
#define LI_CHG_LV1 (3.78)
#define LI_CHG_LV2 (4.02)
#define LI_CHG_LV3 (4.25)
#define LI_LV1 (3.50)
#define LI_LV2 (3.62)
#define LI_LV3 (3.80)
#define LI_LV4 (4.10)

BLEDis bledis;         // device information service
BLEHidAdafruit blehid; // Human Interface Device service
BLEBas blebas;         // battery Service
bool hasKeyPressed = false;
uint32_t lastMeasure;
int8_t vindex;
int8_t count;
uint8_t lastnotify;
uint16_t rawvalues[BAT_AVERAGE_COUNT] = {0};
bool lastIsCharging = false;
uint8_t const conv_table[128][2] = {HID_ASCII_TO_KEYCODE};

hid_keyboard_report_t keycombi_report[KEYSNUM];
int work_LED_status = HIGH;
enum BatType
{
    eBT_dry,
    eBT_NiMH,
    eBT_liIon
};
enum CurrentOperation
{
    eBatt,
    eUSB
};
BatType currentBtype;
CurrentOperation currentOperation;
uint16_t lifened[2] = {900, 1000};
const int my_pin_map[] = {D10, D9, D8, D7,
                          D6, D5, D4, PIN_NFC2,
                          D3, D2, D1, D0};

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
    MyKeyCombi_10,
    MyKeyCombi_11
};
OneButton *tactsw[KEYSNUM];
int tactpos[KEYSNUM];
// Setup buttons
OneButton button20(PIN_NFC1, true);

void loadmapfile(void);

/// @brief init report data
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

void myKeyboardReport(mycombi *combi)
{

    BLEConnection *connection = Bluefruit.Connection(0);
    if (connection && connection->connected() && connection->secured())
    {
        blehid.keyboardReport(&keycombi_report[*combi]);
    }
    hasKeyPressed = true;
    Serial.print("button");
    Serial.println((int)*combi);
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

void tactclick(void *position) { myKeyboardReport((mycombi *)position); }

void longpress20(void)
{
    Bluefruit.Periph.clearBonds();
    Serial.println("clear bonding infos");
    delay(5);
    // digitalWrite(LED_RED, HIGH);
}

void connect_callback(uint16_t conn_handle);
void disconnect_callback(uint16_t conn_handle, uint8_t reason);
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize);
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize);
void msc_flush_cb(void);
void blestart(void);
void usb_massstorage_start(void);
void pin_init_and_button_attach(void);
void measure_and_notify(void);
bool battery_isCharging(void);
void add_device_name_file(void);

/* setup */
void setup()
{
    // Enable DC-DC converter
    NRF_POWER->DCDCEN = 1;
    MyKeyCombi_init();
    pin_init_and_button_attach();

    delay(100);
    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk)
    {

        currentOperation = eUSB;
        Serial.begin(115200);
        delay(10);
#ifdef DEBUG
        while (!Serial)
            delay(10); // wait for native usb
#endif
        // VBUS present
        Serial.println("VBUS present");
        usb_massstorage_start();
#if 0
        while (!Serial)
            delay(10); // wait for native usb
#endif
        loadmapfile();
    }
    else
    {
        // battery operate
        currentOperation = eBatt;
#ifdef DEBUG
        while (!Serial)
            delay(10); // wait for native usb
#endif
        Serial.println("VBUS NOT present");
        flashTransport.begin();
        flashTransport.runCommand(COMMAND_WAKEUP);
        if (flash.begin())
        {
            fatfs.begin(&flash);
            loadmapfile();
        };
        flashTransport.runCommand(COMMAND_SLLEP); // sleep nor flash
        flashTransport.end();
    }
    if (currentBtype == eBT_liIon)
    {
        // High speed charging (100mA)
        pinMode(PIN_HICHG, OUTPUT);
        digitalWrite(PIN_HICHG, LOW);
        pinMode(PIN_INVCHG, INPUT);
    }
    blestart();
    if (currentOperation == eUSB)
    {
        add_device_name_file();
    }
    lastMeasure = 0;
}
/* main loop */
void loop()
{
    uint32_t ms;

    ms = millis();
    for (int i = 0; i < KEYSNUM; i++)
    {
        tactsw[i]->tick();
    }

    button20.tick();

    // Only send KeyRelease if previously pressed to avoid sending
    // multiple keyRelease reports (that consume memory and bandwidth)
    if (hasKeyPressed)
    {
        hasKeyPressed = false;
        blehid.keyRelease();

        // Delay a bit after a report
        delay(5);
    }
    if (ms - lastMeasure > BAT_MEASURE_INTERVAL)
    {
        lastMeasure = ms;
        measure_and_notify();
    }
    ms = millis();
    if (fs_changed && ((ms - lastflashed) > FLASH_MONITOR_INTERVAL))
    {
        loadmapfile();
        fs_changed = false;
    }
    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk)
    {
        if (currentOperation == eBatt)
        {
            // reboot
            Bluefruit.disconnect(0);
            NVIC_SystemReset();
        }
    }

    delay(50);
}

/* bat measure and notify batt state */
void measure_and_notify(void)
{
    float mV = 0.0;
    uint16_t volt1000 = 0;
    uint8_t value = 0;
    uint16_t rawtotal = 0;
    bool isCharging;
    double volt;
    Bluefruit.autoConnLed(false);
    digitalWrite(LED_RED, HIGH);
    switch (currentBtype)
    {
    case eBT_dry:
    /* FALLTHROUGH */
    case eBT_NiMH:
        analogReference(AR_INTERNAL_1_8);
        // Set the resolution to 12-bit (0..4095)
        analogReadResolution(12); // Can be 8, 10, 12 or 14
        // Let the ADC settle
        delay(1);

        rawvalues[vindex] = (uint16_t)analogRead(A0);
        // Set the ADC back to the default settings
        analogReference(AR_DEFAULT);
        analogReadResolution(10);
        vindex = (vindex + 1) & BAT_AVERAGE_MASK;
        count = min(count + 1, BAT_AVERAGE_COUNT);
        rawtotal = 0;
        for (int i = 0; i < count; i++)
        {
            rawtotal += rawvalues[i];
        }
        mV = (float)rawtotal / count * VBAT_MV_PER_LSB;
        volt1000 = (uint16_t)mV;
        if (volt1000 > BAT_UPPER)
        {
            volt1000 = BAT_UPPER;
        }
        if (volt1000 < lifened[currentBtype])
        {
            volt1000 = lifened[currentBtype];
        }
        value = (uint8_t)(map(volt1000, lifened[currentBtype], BAT_UPPER, 1, 100));
        if (lastnotify != value && currentOperation == eBatt)
        {
            lastnotify = value;
#if 1
            if (volt1000 <= lifened[currentBtype])
            {
                myBasNotyfy(1);
                if (currentBtype == eBT_NiMH)
                {
                    // system off
                    sd_power_system_off();
                }
            }
            else
            {
                myBasNotyfy(value);
            }
#endif
            break;

        case eBT_liIon:
            isCharging = battery_isCharging();
            if (lastIsCharging != isCharging)
            { // 充電状態が変わったらリセット
                vindex = 0;
                count = 0;
            }

            pinMode(VBAT_ENABLE, OUTPUT);
            digitalWrite(VBAT_ENABLE, LOW);
            analogReference(AR_DEFAULT);
            analogReadResolution(10);
            rawvalues[vindex] = (uint16_t)analogRead(PIN_VBAT);
            pinMode(VBAT_ENABLE, INPUT);

            vindex = (vindex + 1) & BAT_AVERAGE_MASK;
            count = min(count + 1, BAT_AVERAGE_COUNT);
            rawtotal = 0;
            for (int i = 0; i < count; i++)
            {
                rawtotal += rawvalues[i];
            }

            volt = (double)rawtotal / count / 1024 * 3.6 / 510 * 1510; // 10bit, Vref=3.6V, 分圧比1000:510
            volt1000 = (uint16_t)(volt * 1000);
            if (isCharging)
            {
                if (volt <= LI_CHG_LV1)
                    value = 1;
                else if (volt <= LI_CHG_LV2)
                    value = 3;
                else if (volt <= LI_CHG_LV3)
                    value = (uint8_t)((volt - 4) * 140 / 5 + 0.5) * 5; // 4.25Vで35%になるよう5%単位
                else
                    value = 70; // ここからは定電圧領域になるので電圧じゃほとんどわからない
            }
            else
            {
                if (volt <= LI_LV1)
                    value = 1;
                else if (volt <= LI_LV2)
                    value = 3;
                else if (volt <= LI_LV3)
                    value = (uint8_t)(((volt - LI_LV2) * 277.78 + 5) / 5 + 0.5) * 5; // 3.8Vで55%、3.62Vで5%
                else if (volt <= LI_LV4)
                    value = (uint8_t)(((volt - LI_LV3) * 150 + 55) / 5 + 0.5) * 5; // 4.1Vで100%、3.8Vで55%
                else
                    value = 100;
            }
            if (lastnotify != value)
            {
                lastnotify = value;
                myBasNotyfy(value);
            }
            lastIsCharging = isCharging;
            break;

        default:
            break;
        }
    }

    Serial.printf("battery mV %d notify %d", volt1000, value);
    Serial.println();
}
void pin_init_and_button_attach(void)
{
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, work_LED_status);
    for (int i = 0; i < KEYSNUM; i++)
    {
        tactpos[i] = i;
        tactsw[i] = new OneButton(my_pin_map[i], true);
        tactsw[i]->attachClick(tactclick, &tactpos[i]);
        tactsw[i]->setClickMs(KEYCLICKTIME);
    }
    button20.attachLongPressStart(longpress20);
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

void usb_massstorage_start(void)
{
    flashTransport.begin();
    // wake up nor flash
    flashTransport.runCommand(COMMAND_WAKEUP);

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
void add_device_name_file(void)
{
    const char myfilename[] = "name.txt";
    char lastletter[5] = {0};
    char myaddressstr[64] = {0};
    ble_gap_addr_t myaddres = Bluefruit.getAddr();
    snprintf(lastletter, 5, "%02x%02x", myaddres.addr[0], myaddres.addr[1]);
    memcpy(mydevicename, basedevicename, strlen(basedevicename));
    memcpy(&mydevicename[strlen(basedevicename)], lastletter, strlen(lastletter));
    if (!root.open("/"))
    {
        Serial.println("open root failed");
        return;
    }
    else
    {
        // fat format exits
        if (file.exists(myfilename) == false)
        {
            file.open(myfilename, O_WRONLY | O_CREAT | O_EXCL);
            file.write(mydevicename);
            file.write("\n");
            snprintf(myaddressstr, sizeof(myaddressstr),
                     "address %02x:%02x:%02x:%02x:%02x:%02x\n",
                     myaddres.addr[0], myaddres.addr[1], myaddres.addr[2],
                     myaddres.addr[3], myaddres.addr[4], myaddres.addr[5]);
            file.write(myaddressstr);
            file.write("\nVersion:");
            file.write(Ver_str);
            file.write("\n");
            file.close();
        }
    }
}
void loadmapfile(void)
{
    char iniheader[8] = {0};
    const char modifier[] = "modifier";
    const char *modifierstr[] = {"WIN", "CMD", "CTRL", "ALT", "OPT"};
    const char key[] = "key";
    const char pad[] = "PAD";
    const char strbatt[] = "BATT";
    const char strtype[] = "TYPE";
    const char strdry[] = "dry";
    const char strnimh[] = "NiMH";
    const char strliion[] = "LiIon";

    char *readstr;
    String loadsettingstr;
    // int keystrlen;
    char *padpos;

    if (file.open(inifilename, O_RDONLY))
    {

        // open succsess
        for (int i = 0; i < KEYSNUM; i++)
        {

            memclr(iniheader, sizeof(iniheader));
            memcpy(iniheader, "key", 3);
            char positionstr[3] = {0};
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
            int keystrlen = strlen(readstr);
            int padstrlen;
            switch (keystrlen)
            {
            case 0:
                loadsettingstr.concat("NO_KEY ");
                break;
            case 1:
                if (readstr != NULL && readstr[0] < 128)
                {
                    uint8_t tnum = readstr[0];
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
        readstr = inifileString(file, (char *)strbatt, (char *)strtype);
        // keystrlen = strlen(readstr);
        if (readstr != NULL)
        {
            if (strstr(readstr, strdry) != NULL)
            {
                currentBtype = eBT_dry;
                Serial.println("battery type is dry battery");
            }
            else if (strstr(readstr, strnimh) != NULL)
            {
                currentBtype = eBT_NiMH;
                Serial.println("battery type is NiMH battery");
            }
            else if (strstr(readstr, strliion) != NULL)
            {
                currentBtype = eBT_liIon;
                Serial.println("battery type is Litium-Ion battery");
            }
            //
        }

        free(readstr);
        file.close();
    }
}
void blestart(void)
{
    char lastletter[5] = {0};
    Bluefruit.Periph.setConnIntervalMS(30, 120);
    Bluefruit.begin();
    Bluefruit.autoConnLed(0);
    Bluefruit.setTxPower(0); // Check bluefruit.h for supported values
                             // Configure and Start Device Information Service
    bledis.setManufacturer("j1okabe");
    bledis.setModel("xiao ble");
    ble_gap_addr_t myaddres = Bluefruit.getAddr();
    snprintf(lastletter, 5, "%02x%02x", myaddres.addr[0], myaddres.addr[1]);
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
}
bool battery_isCharging()
{
    return digitalRead(PIN_INVCHG) == LOW;
}

/* Referenced Articles */
// https://days-of-programming.blogspot.com/search/label/nRF52840

// XIAO BLEをArduino開発するときの2種のボードライブラリの違い
// https://zenn.dev/ukkz/articles/a9ec6fc37b68b7

// XIAO nRF52840 のVBUS判定
// https://lipoyang.hatenablog.com/entry/2023/01/01/102737

/*
patch to work P25Q16H.
.pio\libdeps\xiaoble_adafruit_nrf52\Adafruit
SPIFlash\src\Adafruit_SPIFlashBase.cpp line 111 add  P25Q16H

.pio\libdeps\xiaoble_adafruit_nrf52\Adafruit SPIFlash\src\flash_devices.h line
537 .max_clock_speed_mhz = 104, .quad_enable_bit_mask = 0x02,

*/