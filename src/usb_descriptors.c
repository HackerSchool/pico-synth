#include "device/usbd.h"
#include "tusb.h"

#ifndef USBD_VID
#define USBD_VID (0x2E8A) // Raspberry Pi
#endif

#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USBD_PID                                                               \
    (0x6942 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) |         \
     _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4))
#ifndef USBD_MANUFACTURER
#define USBD_MANUFACTURER "HackerSchool"
#endif

#ifndef USBD_PRODUCT
#define USBD_PRODUCT "Pico-Synth"
#endif

#define USBD_DESC_LEN                                                          \
    (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_CDC_DESC_LEN +              \
     TUD_MSC_DESC_LEN)
#define TUD_RPI_RESET_DESC_LEN 9
#if !PICO_STDIO_USB_DEVICE_SELF_POWERED
#define USBD_CONFIGURATION_DESCRIPTOR_ATTRIBUTE (0)
#define USBD_MAX_POWER_MA (250)
#else
#define USBD_CONFIGURATION_DESCRIPTOR_ATTRIBUTE                                \
    TUSB_DESC_CONFIG_ATT_SELF_POWERED
#define USBD_MAX_POWER_MA (1)
#endif

#define USBD_ITF_MIDI (0) // needs 2 interfaces
#define USBD_ITF_CDC (2)  // needs 2 interfaces
#define USBD_ITF_MSC (4)  // needs 2 interfaces
#define USBD_ITF_MAX (5)

#define USBD_CDC_EP_CMD (0x81)

#define USBD_CDC_CMD_MAX_SIZE (8)
#define USBD_CDC_IN_OUT_MAX_SIZE (64)

#define USBD_CDC_EP_OUT (0x03)
#define USBD_CDC_EP_IN (0x83)

#define USBD_MIDI_EP_OUT (0x02)
#define USBD_MIDI_EP_IN (0x82)

#define USBD_MIDI_IN_OUT_MAX_SIZE (64)

#define USBD_MSC_OUT 0x04
#define USBD_MSC_IN 0x84

#define USBD_STR_0 (0x00)
#define USBD_STR_MANUF (0x01)
#define USBD_STR_PRODUCT (0x02)
#define USBD_STR_SERIAL (0x03)
#define USBD_STR_RPI_RESET (0x06)

tusb_desc_device_t const usbd_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0xCafe,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01};
static const uint8_t usbd_desc_cfg[USBD_DESC_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, USBD_ITF_MAX, USBD_STR_0, USBD_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          USBD_MAX_POWER_MA),

    TUD_MIDI_DESCRIPTOR(
        USBD_ITF_MIDI, USBD_STR_0, // USDB_STR_MIDI is not working with Windows
        USBD_MIDI_EP_OUT, USBD_MIDI_EP_IN, USBD_MIDI_IN_OUT_MAX_SIZE),

    TUD_CDC_DESCRIPTOR(USBD_ITF_CDC,
                       USBD_STR_0, // USDB_STR_CDC is not working with Windows
                       USBD_CDC_EP_CMD, USBD_CDC_CMD_MAX_SIZE, USBD_CDC_EP_OUT,
                       USBD_CDC_EP_IN, USBD_CDC_IN_OUT_MAX_SIZE),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(USBD_ITF_MSC, USBD_STR_0, USBD_MSC_OUT, USBD_MSC_IN, 64),
};

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

char const *usbd_string_desc[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "HackerSchool",             // 1: Manufacturer
    "Pico-Synth",               // 2: Product
    NULL,                       // 3: Serials, should use chip ID
};

static uint16_t usbd_string_desc_len[32];

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&usbd_desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; // for multiple configurations

    return usbd_desc_cfg;
}

#define USBDusbd_string_desc_len_MAX 20

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;

    if (index == 0) {
        memcpy(&usbd_string_desc_len[1], usbd_string_desc[0], 2);
        chr_count = 1;
    } else {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(usbd_string_desc) / sizeof(usbd_string_desc[0])))
            return NULL;

        const char *str = usbd_string_desc[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++) {
            usbd_string_desc_len[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    usbd_string_desc_len[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

    return usbd_string_desc_len;
}
