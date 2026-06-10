#include "tusb.h"

// Device Descriptor
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00, // class-specific
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2e8a, // Raspberry Pi
    .idProduct          = 0x0003, // TinyUSB MIDI
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// Configuration Descriptor
#define EPNUM_MIDI   1

enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64),
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

// String Descriptors
char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Supported language is English (0x0409)
    "spinalSynth",                 // 1: Manufacturer
    "spinalSynth MIDI Interface",  // 2: Product
    "123456",                      // 3: Serial Number
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

        char const* str = string_desc_arr[index];

        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;

        for(uint8_t i=0; i<chr_count; i++) {
            _desc_str[1+i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2*chr_count + 2);

    return _desc_str;
}
