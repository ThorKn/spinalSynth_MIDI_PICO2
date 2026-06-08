#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
  #define CFG_TUSB_MCU             OPT_MCU_RP2350
#endif

#ifndef CFG_TUSB_OS
  #define CFG_TUSB_OS              OPT_OS_NONE
#endif

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUSB_RHPORT0_MODE
  #define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE | OPT_MODE_HOST)
#endif

// Enable USB MIDI Device Class Driver
#define CFG_TUD_ENABLED          1
#define CFG_TUD_MIDI             1
#define CFG_TUD_MIDI_RX_BUFSIZE  64
#define CFG_TUD_MIDI_TX_BUFSIZE  64

#define CFG_TUH_API_EDPT_XFER    1

// Enable USB Hub support (critical for connecting multiple MIDI devices)
#define CFG_TUH_HUB              1

// Maximum USB devices (e.g. 1 Hub + 2 MIDI devices + 1 spare)
#define CFG_TUH_DEVICE_MAX       4

// Enable USB MIDI Host Class Driver
#define CFG_TUH_MIDI             1

// Disable Stream API to avoid version mismatch compilation errors
#define CFG_TUH_MIDI_STREAM_API  0

// Workaround for SDK usbh.c bug: include the audio class header directly in the config
// so that usbh.c has access to AUDIO_SUBCLASS_CONTROL and AUDIO_FUNC_PROTOCOL_CODE_UNDEF
#include "class/audio/audio.h"

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
