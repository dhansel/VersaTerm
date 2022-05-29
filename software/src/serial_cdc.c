#include "tusb.h"
#include "config.h"
#include "terminal.h"
#include "serial_cdc.h"
#include "serial_uart.h"
#include "framebuf.h"


static void terminal_disabled_message(bool show)
{
  static bool messageShowing = false;
  char buf[100];

  if( config_get_usb_cdcmode()==3 && show )
    {
      snprintf(buf, 100, "\033[s\033[%i;1H\033[38;5;15;48;5;1m\033[K\033[%iCTerminal disabled for USB pass-through\033[?25l", 
               framebuf_get_nrows(), framebuf_get_ncols(-1)/2-18);
      terminal_receive_string(buf);
      messageShowing = true;
    }
  else if( messageShowing )
    {
      snprintf(buf, 100, "\033[u\033[%i;1H\033[K\033[u\033[?25h", framebuf_get_nrows());
      terminal_receive_string(buf);
      messageShowing = false;
    }
}


bool serial_cdc_is_connected()
{
  return tud_cdc_connected();
}


void serial_cdc_set_break(bool set)
{
  // not implemented
}


void serial_cdc_send_char(char c)
{
  if( tud_cdc_connected() )
    {
      tud_cdc_write(&c, 1);
      tud_cdc_write_flush();
    }
}


void serial_cdc_send_string(const char *s)
{
  if( tud_cdc_connected() )
    {
      tud_cdc_write(s, strlen(s));
      tud_cdc_write_flush();
    }
}


bool serial_cdc_readable()
{
  return tud_inited() && tud_cdc_available();
}


void serial_cdc_task(bool processInput)
{
  if( processInput && tud_inited() && tud_cdc_available() )
    {
      char count, buf[16];

      switch( config_get_usb_cdcmode() )
        {
        case 0: // disabled
          break;

        case 1: // regular serial
          count = tud_cdc_read(buf, sizeof(buf));
          for(int i=0; i<count; i++) terminal_receive_char(buf[i]);
          break;

        case 2: // pass-through
        case 3: // pass-through (terminal disabled)
          {
            count = MIN(serial_uart_can_send(), sizeof(buf));
            if( count>0 ) count = tud_cdc_read(buf, count);
            for(int i=0; i<count; i++) serial_uart_send_char(buf[i]);

            break;
          }
        }
    }
}


// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  if( dtr )
    {
      // Terminal connected
      terminal_disabled_message(true);
    }
  else
    {
      // Terminal disconnected
      terminal_disabled_message(false);
    }
}


// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
}


void serial_cdc_apply_settings()
{
  // executed after closing configuration dialog
  if( config_get_usb_cdcmode()==3 ) terminal_disabled_message(tud_cdc_connected());
}


void serial_cdc_init()
{
}


//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+


#define _PID_MAP(itf, n)  ((CFG_TUD_##itf) << (n))
#define USB_PID   (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4) )
#define USB_VID   0xCafe
#define USB_BCD   0x0200


tusb_desc_device_t const desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,

  // Use Interface Association Descriptor (IAD) for CDC
  // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};


// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

// full speed configuration
uint8_t const desc_fs_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};


// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
#if TUD_OPT_HIGH_SPEED
  // Although we are highspeed, host may be fullspeed.
  return (tud_speed_get() == TUSB_SPEED_HIGH) ?  desc_hs_configuration : desc_fs_configuration;
#else
  return desc_fs_configuration;
#endif
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const* string_desc_arr [] =
{
  (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
  "TinyUSB",                     // 1: Manufacturer
  "TinyUSB Device",              // 2: Product
  "123456789012",                // 3: Serials, should use chip ID
  "TinyUSB CDC"                  // 4: CDC Interface
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  uint8_t chr_count;

  if ( index == 0)
    {
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
    }
  else
    {
      // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
      // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

      if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

      const char* str = string_desc_arr[index];

      // Cap at max char
      chr_count = (uint8_t) strlen(str);
      if ( chr_count > 31 ) chr_count = 31;

      // Convert ASCII string into UTF-16
      for(uint8_t i=0; i<chr_count; i++)
        {
          _desc_str[1+i] = str[i];
        }
    }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

  return _desc_str;
}
