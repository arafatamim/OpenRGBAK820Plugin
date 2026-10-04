/*---------------------------------------------------------*\
| AK820Plugin.cpp                                          |
|                                                           |
|   Ajazz AK820 Max Plus for OpenRGB 1.0                    |
|                                                           |
|   64-byte reports on the vendor interface (usage page     |
|   0xFF02), each one aa 55 cc 33 <cmd> <payload>.          |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AK820Plugin.h"

#include <hidapi.h>

#include <QLabel>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#define AK820_VID           0x1A2C
#define AK820_USAGE_PAGE    0xFF02
#define AK820_LOCATION      "HID: AK820 plugin"

static const unsigned short pids[] = { 0x8FFF, 0xA036 };   /* 2.4G dongle (tested), wired (untested) */

/*---------------------------------------------------------*\
| The dongle drops packets it can't relay over 2.4G in      |
| time; this is the knob if rows get skipped (20ms was safe).|
\*---------------------------------------------------------*/
static const std::chrono::milliseconds gap(5);

static const unsigned int PER_PKT   = 18;   /* room for 19, but the keyboard reads 18 */
static const unsigned int ROWS      = 6;
static const unsigned int COLS      = 15;

struct ak820_key { const char* name; unsigned char slot; unsigned char row; unsigned char col; };

/*---------------------------------------------------------*\
| Slots from the vendor app's layouts/kb-k600t.xml          |
| (key_index); rows/cols placed to match the physical board.|
\*---------------------------------------------------------*/
static const ak820_key keys[] =
{
    { "Key: Escape", 15, 0, 0 },        { "Key: F1", 14, 0, 1 },            { "Key: F2", 13, 0, 2 },
    { "Key: F3", 12, 0, 3 },            { "Key: F4", 11, 0, 4 },            { "Key: F5", 10, 0, 5 },
    { "Key: F6", 9, 0, 6 },             { "Key: F7", 8, 0, 7 },             { "Key: F8", 7, 0, 8 },
    { "Key: F9", 6, 0, 9 },             { "Key: F10", 5, 0, 10 },           { "Key: F11", 4, 0, 11 },
    { "Key: F12", 3, 0, 12 },           { "Key: Delete", 43, 0, 14 },
    { "Key: `", 16, 1, 0 },             { "Key: 1", 17, 1, 1 },             { "Key: 2", 18, 1, 2 },
    { "Key: 3", 19, 1, 3 },             { "Key: 4", 20, 1, 4 },             { "Key: 5", 21, 1, 5 },
    { "Key: 6", 22, 1, 6 },             { "Key: 7", 23, 1, 7 },             { "Key: 8", 24, 1, 8 },
    { "Key: 9", 25, 1, 9 },             { "Key: 0", 26, 1, 10 },            { "Key: -", 27, 1, 11 },
    { "Key: =", 28, 1, 12 },            { "Key: Backspace", 29, 1, 13 },    { "Key: Home", 31, 1, 14 },
    { "Key: Tab", 57, 2, 0 },           { "Key: Q", 56, 2, 1 },             { "Key: W", 55, 2, 2 },
    { "Key: E", 54, 2, 3 },             { "Key: R", 53, 2, 4 },             { "Key: T", 52, 2, 5 },
    { "Key: Y", 51, 2, 6 },             { "Key: U", 50, 2, 7 },             { "Key: I", 49, 2, 8 },
    { "Key: O", 48, 2, 9 },             { "Key: P", 47, 2, 10 },            { "Key: [", 46, 2, 11 },
    { "Key: ]", 45, 2, 12 },            { "Key: \\ (ANSI)", 44, 2, 13 },    { "Key: Page Up", 32, 2, 14 },
    { "Key: Caps Lock", 58, 3, 0 },     { "Key: A", 59, 3, 1 },             { "Key: S", 60, 3, 2 },
    { "Key: D", 61, 3, 3 },             { "Key: F", 62, 3, 4 },             { "Key: G", 63, 3, 5 },
    { "Key: H", 64, 3, 6 },             { "Key: J", 65, 3, 7 },             { "Key: K", 66, 3, 8 },
    { "Key: L", 67, 3, 9 },             { "Key: ;", 68, 3, 10 },            { "Key: '", 69, 3, 11 },
    { "Key: Enter", 70, 3, 13 },        { "Key: Page Down", 41, 3, 14 },
    { "Key: Left Shift", 90, 4, 0 },    { "Key: Z", 89, 4, 2 },             { "Key: X", 88, 4, 3 },
    { "Key: C", 87, 4, 4 },             { "Key: V", 86, 4, 5 },             { "Key: B", 85, 4, 6 },
    { "Key: N", 84, 4, 7 },             { "Key: M", 83, 4, 8 },             { "Key: ,", 82, 4, 9 },
    { "Key: .", 81, 4, 10 },            { "Key: /", 80, 4, 11 },            { "Key: Right Shift", 79, 4, 12 },
    { "Key: Up Arrow", 78, 4, 13 },     { "Key: End", 42, 4, 14 },
    { "Key: Left Control", 91, 5, 0 },  { "Key: Left Windows", 92, 5, 1 },  { "Key: Left Alt", 93, 5, 2 },
    { "Key: Space", 94, 5, 6 },         { "Key: Right Alt", 95, 5, 9 },     { "Key: Right Fn", 97, 5, 10 },
    { "Key: Right Control", 98, 5, 11 },{ "Key: Left Arrow", 99, 5, 12 },   { "Key: Down Arrow", 100, 5, 13 },
    { "Key: Right Arrow", 101, 5, 14 },
};

static const unsigned int KEYS = sizeof(keys) / sizeof(keys[0]);

/*---------------------------------------------------------*\
| Built-in effects: id sent to the keyboard, name, and      |
| whether it takes speed / a colour (the vendor XML's       |
| attribute bits 1 and 6).                                  |
\*---------------------------------------------------------*/
struct ak820_effect { unsigned char id; const char* name; bool speed; bool colour; };

static const ak820_effect effects[] =
{
    { 6,  "Static",                  false, true  },
    { 7,  "Breathing",               true,  true  },
    { 4,  "Spectrum Cycle",          true,  false },
    { 0,  "Left and Right Ripples",  true,  true  },
    { 1,  "Starry Dots",             true,  false },
    { 2,  "Up and Down Ripples",     true,  true  },
    { 3,  "Bidirectional Ejection",  true,  true  },
    { 5,  "Kaleidoscope",            true,  true  },
    { 8,  "Cross Operation",         true,  true  },
    { 10, "Single Key Breathing",    true,  true  },
    { 11, "Picture Scroll",          true,  true  },
    { 14, "Top Three Bottom Three",  true,  true  },
    { 15, "Rain Falling",            true,  true  },
    { 16, "Windmill",                true,  true  },
    { 17, "Sine Wave",               true,  true  },
    { 18, "Galloping Horse",         true,  true  },
};

/*---------------------------------------------------------*\
| Effects only take a palette index; 7 is rainbow.          |
\*---------------------------------------------------------*/
static const RGBColor palette[] =
{
    ToRGBColor(255, 0, 0),   ToRGBColor(0, 255, 0),   ToRGBColor(0, 0, 255),   ToRGBColor(255, 255, 0),
    ToRGBColor(255, 0, 255), ToRGBColor(0, 255, 255), ToRGBColor(255, 255, 255),
};

static const unsigned char RAINBOW     = 7;
static const unsigned char MODE_CUSTOM = 0x17;
static const int           DIRECT      = -1;

static unsigned char nearest_palette(RGBColor c)
{
    unsigned char best      = 0;
    int           best_dist = INT32_MAX;

    for(unsigned char i = 0; i < 7; i++)
    {
        int dr   = (int)RGBGetRValue(c) - (int)RGBGetRValue(palette[i]);
        int dg   = (int)RGBGetGValue(c) - (int)RGBGetGValue(palette[i]);
        int db   = (int)RGBGetBValue(c) - (int)RGBGetBValue(palette[i]);
        int dist = dr * dr + dg * dg + db * db;

        if(dist < best_dist)
        {
            best      = i;
            best_dist = dist;
        }
    }

    return best;
}

static hid_device* open_ak820()
{
    hid_device_info* list = hid_enumerate(AK820_VID, 0);
    hid_device*      dev  = nullptr;

    for(hid_device_info* d = list; d && !dev; d = d->next)
    {
        for(unsigned short pid : pids)
        {
            if(d->product_id == pid && d->usage_page == AK820_USAGE_PAGE)
            {
                dev = hid_open_path(d->path);
                break;
            }
        }
    }

    hid_free_enumeration(list);
    return dev;
}

static bool send(hid_device* dev, unsigned char cmd, const unsigned char* payload = nullptr, size_t len = 0)
{
    unsigned char buf[65] = { 0, 0xAA, 0x55, 0xCC, 0x33, cmd };   /* leading 0: no report ID */
    if(len)
    {
        memcpy(buf + 6, payload, len);
    }
    return dev && hid_write(dev, buf, sizeof(buf)) == (int)sizeof(buf);
}

/*---------------------------------------------------------*\
| Device state. OpenRGB 1.0 owns the controller (built from |
| an RGBController_Setup) and calls back into these         |
| functions; colours and mode settings are read back        |
| through the RGBControllerInterface it hands us.           |
\*---------------------------------------------------------*/
struct AK820Device
{
    RGBControllerInterface* rgb       = nullptr;
    hid_device*             dev       = nullptr;
    std::mutex              mutex;
    bool                    in_custom = false;

    /*-----------------------------------------------------*\
    | Opened lazily so the keyboard can be off at startup;  |
    | a stale handle (dongle unplugged) is dropped on the   |
    | first failed write and reopened on the next update.   |
    \*-----------------------------------------------------*/
    bool write(unsigned char cmd, const unsigned char* payload = nullptr, size_t len = 0)
    {
        if(!dev)
        {
            dev       = open_ak820();
            in_custom = false;
        }

        if(send(dev, cmd, payload, len))
        {
            return true;
        }

        hid_close(dev);
        dev = nullptr;
        return false;
    }
};

static AK820Device device;

/*---------------------------------------------------------*\
| Custom mode: six packets of 18 RGB slots, gap apart.      |
\*---------------------------------------------------------*/
static void update_leds(void*)
{
    unsigned char frame[6][PER_PKT * 3] = {};
    unsigned int  level                 = device.rgb->GetModeBrightness(0);   /* Direct's 0-100 slider, done by scaling */

    for(unsigned int i = 0; i < KEYS; i++)
    {
        RGBColor       c    = device.rgb->GetColor(i);
        unsigned int   slot = keys[i].slot;
        unsigned char* p    = &frame[slot / PER_PKT][slot % PER_PKT * 3];

        /* never send 0xFF: the vendor app doesn't, and some firmware misbehaves on it */
        p[0] = std::min<unsigned int>(RGBGetRValue(c) * level / 100, 254);
        p[1] = std::min<unsigned int>(RGBGetGValue(c) * level / 100, 254);
        p[2] = std::min<unsigned int>(RGBGetBValue(c) * level / 100, 254);
    }

    std::lock_guard<std::mutex> lock(device.mutex);

    /*-----------------------------------------------------*\
    | Selecting custom mode briefly shows the stored custom |
    | layout, so only do it when entering Direct.           |
    | ponytail: an Fn-key mode change on the keyboard goes  |
    | unnoticed; pick an effect, then Direct, to recover.   |
    \*-----------------------------------------------------*/
    bool ok = true;

    if(!device.in_custom)
    {
        const unsigned char select[] = { MODE_CUSTOM, 4, 3, 0, RAINBOW };
        ok = device.write(0x07, select, sizeof(select));
        std::this_thread::sleep_for(gap);
        ok = ok && device.write(0x0B);
        device.in_custom = ok;
    }

    for(unsigned char i = 0; i < 6 && ok; i++)
    {
        std::this_thread::sleep_for(gap);
        ok = device.write(0xF6 + i, frame[i], sizeof(frame[i]));
    }
}

static void update_zone_leds(void*, int)    { update_leds(nullptr); }

static void update_mode(void*)
{
    int m = device.rgb->GetActiveMode();

    if(m <= 0)
    {
        update_leds(nullptr);
        return;
    }

    unsigned char colour = RAINBOW;
    if(device.rgb->GetModeColorMode(m) == MODE_COLORS_MODE_SPECIFIC && device.rgb->GetModeColorsCount(m) > 0)
    {
        colour = nearest_palette(device.rgb->GetModeColor(m, 0));
    }

    const unsigned char payload[] =
    {
        effects[m - 1].id,
        (unsigned char)device.rgb->GetModeBrightness(m),
        (unsigned char)device.rgb->GetModeSpeed(m),
        0,
        colour,
    };

    std::lock_guard<std::mutex> lock(device.mutex);
    device.in_custom = false;
    device.write(0x07, payload, sizeof(payload));
}

static RGBController_Setup make_setup()
{
    RGBController_Setup setup = {};
    setup.name        = "Ajazz AK820 Max Plus";
    setup.vendor      = "Ajazz";
    setup.description = "Ajazz AK820 Max Plus (OpenRGB plugin)";
    setup.location    = AK820_LOCATION;
    setup.type        = DEVICE_TYPE_KEYBOARD;
    setup.flags       = CONTROLLER_FLAG_VIRTUAL;
    setup.active_mode = 0;

    mode direct;
    direct.name           = "Direct";
    direct.value          = DIRECT;
    direct.flags          = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    direct.color_mode     = MODE_COLORS_PER_LED;
    direct.brightness_min = 0;
    direct.brightness_max = 100;
    direct.brightness     = 100;
    setup.modes.push_back(direct);

    for(const ak820_effect& e : effects)
    {
        mode m;
        m.name           = e.name;
        m.value          = e.id;
        m.flags          = MODE_FLAG_HAS_BRIGHTNESS;
        m.brightness_min = 0;
        m.brightness_max = 4;
        m.brightness     = 4;
        m.color_mode     = MODE_COLORS_NONE;

        if(e.speed)
        {
            m.flags     |= MODE_FLAG_HAS_SPEED;
            m.speed_min  = 0;
            m.speed_max  = 4;
            m.speed      = 3;
        }

        if(e.colour)
        {
            m.flags     |= MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_RANDOM_COLOR;
            m.colors_min = 1;
            m.colors_max = 1;
            m.color_mode = MODE_COLORS_RANDOM;
            m.colors.push_back(palette[0]);
        }

        setup.modes.push_back(m);
    }

    unsigned int map[ROWS * COLS];
    std::fill(map, map + ROWS * COLS, 0xFFFFFFFF);

    for(unsigned int i = 0; i < KEYS; i++)
    {
        map[keys[i].row * COLS + keys[i].col] = i;

        led l;
        l.name  = keys[i].name;
        l.value = keys[i].slot;
        setup.leds.push_back(l);
    }

    zone z;
    z.name       = "Keyboard";
    z.type       = ZONE_TYPE_MATRIX;
    z.leds_min   = KEYS;
    z.leds_max   = KEYS;
    z.leds_count = KEYS;
    z.matrix_map.Set(ROWS, COLS, map);
    setup.zones.push_back(z);

    setup.object_ptr            = &device;
    setup.DeviceUpdateLEDs      = update_leds;
    setup.DeviceUpdateZoneLEDs  = update_zone_leds;
    setup.DeviceUpdateSingleLED = update_zone_leds;
    setup.DeviceUpdateMode      = update_mode;

    return setup;
}

/*---------------------------------------------------------*\
| Plugin glue                                               |
\*---------------------------------------------------------*/
static OpenRGBPluginAPIInterface*   api;
static std::atomic<int>             battery(-1);
static std::atomic<bool>            running;
static std::thread                  battery_thread;
static std::mutex                   battery_mutex;
static std::condition_variable      battery_cv;

/*---------------------------------------------------------*\
| Battery poll on its own handle so a slow reply never      |
| holds up lighting. The keyboard answers 0e with           |
| aa 55 cc 33 0e <percent> <?>, and also pushes it unasked. |
\*---------------------------------------------------------*/
static void poll_battery()
{
    hid_device* dev = nullptr;

    while(running)
    {
        if(!dev)
        {
            dev = open_ak820();
        }

        int percent = -1;

        if(send(dev, 0x0E))
        {
            unsigned char buf[64];
            for(int i = 0; i < 10 && percent < 0; i++)
            {
                int n = hid_read_timeout(dev, buf, sizeof(buf), 200);
                if(n < 0)
                {
                    break;
                }
                if(n >= 6 && buf[0] == 0xAA && buf[1] == 0x55 && buf[2] == 0xCC && buf[3] == 0x33 && buf[4] == 0x0E)
                {
                    percent = buf[5];
                }
            }
        }

        if(percent < 0 && dev)
        {
            hid_close(dev);
            dev = nullptr;
        }

        battery = percent;

        std::unique_lock<std::mutex> lock(battery_mutex);
        battery_cv.wait_for(lock, std::chrono::seconds(30), [] { return !running; });
    }

    if(dev)
    {
        hid_close(dev);
    }
}

OpenRGBPluginInfo AK820Plugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name            = "Ajazz AK820 Max Plus";
    info.Description     = "Lighting and battery for the Ajazz AK820 Max Plus";
    info.Version         = "0.2.0";
    info.Location        = OPENRGB_PLUGIN_LOCATION_INFORMATION;
    info.Label           = "AK820 Battery";
    info.ProtocolVersion = 0;   /* no SDK commands */
    return info;
}

unsigned int AK820Plugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void AK820Plugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api = plugin_api_ptr;

    RGBController_Setup setup = make_setup();
    device.rgb = api->CreateVirtualRGBController(&setup);
    api->RegisterVirtualRGBControllerInThread(device.rgb);   /* Load runs on the UI thread */

    running        = true;
    battery_thread = std::thread(poll_battery);
}

QWidget* AK820Plugin::GetWidget()
{
    QLabel* label = new QLabel("Battery: checking...");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 20px;");

    QTimer* timer = new QTimer(label);
    QObject::connect(timer, &QTimer::timeout, label, [label]
    {
        int b = battery;
        label->setText(b < 0 ? "Battery: keyboard not found" : QString("Battery: %1%").arg(b));
    });
    timer->start(1000);

    return label;
}

QMenu* AK820Plugin::GetTrayMenu()
{
    return nullptr;
}

void AK820Plugin::Unload()
{
    {
        std::lock_guard<std::mutex> lock(battery_mutex);
        running = false;
    }
    battery_cv.notify_all();
    if(battery_thread.joinable())
    {
        battery_thread.join();
    }

    if(device.rgb)
    {
        api->UnregisterVirtualRGBController(device.rgb);
        api->DeleteVirtualRGBController(device.rgb);
        device.rgb = nullptr;
    }

    std::lock_guard<std::mutex> lock(device.mutex);
    hid_close(device.dev);
    device.dev = nullptr;
}
