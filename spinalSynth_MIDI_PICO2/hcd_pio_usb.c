/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if CFG_TUH_ENABLED && (CFG_TUSB_MCU == OPT_MCU_RP2040 || CFG_TUSB_MCU == OPT_MCU_RP2350) && CFG_TUH_RPI_PIO_USB

#include "pico.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"

//--------------------------------------------------------------------+
// INCLUDE
//--------------------------------------------------------------------+
#include "osal/osal.h"

#include "host/hcd.h"
#include "host/usbh.h"

#define RHPORT_OFFSET     1
#define RHPORT_PIO(_x)    ((_x)-RHPORT_OFFSET)

static pio_usb_configuration_t pio_host_cfg = PIO_USB_DEFAULT_CONFIG;

//--------------------------------------------------------------------+
// HCD API
//--------------------------------------------------------------------+
bool hcd_configure(uint8_t rhport, uint32_t cfg_id, const void *cfg_param) {
  (void) rhport;
  TU_VERIFY(cfg_id == TUH_CFGID_RPI_PIO_USB_CONFIGURATION);
  memcpy(&pio_host_cfg, cfg_param, sizeof(pio_usb_configuration_t));
  return true;
}

bool hcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void) rhport;
  (void) rh_init;

  // To run USB SOF interrupt in core1, call this init in core1
  pio_usb_host_init(&pio_host_cfg);

  return true;
}

void hcd_port_reset(uint8_t rhport) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Port Reset on Port %d\n", rhport);
#endif
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  pio_usb_host_port_reset_start(pio_rhport);
}

void hcd_port_reset_end(uint8_t rhport) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Port Reset End on Port %d\n", rhport);
#endif
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  pio_usb_host_port_reset_end(pio_rhport);
}

bool hcd_port_connect_status(uint8_t rhport) {
  uint8_t const pio_rhport = RHPORT_PIO(rhport);

  root_port_t *root = PIO_USB_ROOT_PORT(pio_rhport);
  port_pin_status_t line_state = pio_usb_bus_get_line_state(root);
  bool connected = (line_state != PORT_PIN_SE0);
  
  static bool last_connected = false;
  if (connected != last_connected) {
#if USB_DEBUG_PRINTS
      printf("[HCD] Connect status changed on Port %d: %s (line_state = %d)\n", rhport, connected ? "CONNECTED" : "DISCONNECTED", line_state);
#endif
      last_connected = connected;
  }

  return connected;
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport) {
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  tusb_speed_t speed = PIO_USB_ROOT_PORT(pio_rhport)->is_fullspeed ? TUSB_SPEED_FULL : TUSB_SPEED_LOW;
#if USB_DEBUG_PRINTS
  printf("[HCD] Speed get on Port %d: %s\n", rhport, speed == TUSB_SPEED_FULL ? "FULL" : "LOW");
#endif
  return speed;
}

// Close all opened endpoint belong to this device
void hcd_device_close(uint8_t rhport, uint8_t dev_addr) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Device close on Port %d, addr %d\n", rhport, dev_addr);
#endif
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  pio_usb_host_close_device(pio_rhport, dev_addr);
}

uint32_t hcd_frame_number(uint8_t rhport) {
  (void) rhport;
  return pio_usb_host_get_frame_number();
}

void hcd_int_enable(uint8_t rhport) {
  (void) rhport;
}

void hcd_int_disable(uint8_t rhport) {
  (void) rhport;
}

//--------------------------------------------------------------------+
// Endpoint API
//--------------------------------------------------------------------+

bool hcd_edpt_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_endpoint_t const *desc_ep) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Edpt open on Port %d, dev_addr %d, ep 0x%02X\n", rhport, dev_addr, desc_ep->bEndpointAddress);
#endif
  hcd_devtree_info_t dev_tree;
  hcd_devtree_get_info(dev_addr, &dev_tree);
  bool const need_pre = (dev_tree.hub_addr && dev_tree.speed == TUSB_SPEED_LOW);

  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  return pio_usb_host_endpoint_open(pio_rhport, dev_addr, (uint8_t const *) desc_ep, need_pre);
}

bool hcd_edpt_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Edpt xfer: Port %d, dev_addr %d, ep 0x%02X, len %d\n", rhport, dev_addr, ep_addr, buflen);
#endif
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  return pio_usb_host_endpoint_transfer(pio_rhport, dev_addr, ep_addr, buffer, buflen);
}

bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  return pio_usb_host_endpoint_abort_transfer(pio_rhport, dev_addr, ep_addr);
}

bool hcd_setup_send(uint8_t rhport, uint8_t dev_addr, uint8_t const setup_packet[8]) {
#if USB_DEBUG_PRINTS
  printf("[HCD] Setup send: Port %d, dev_addr %d, request_type 0x%02X, request 0x%02X\n",
         rhport, dev_addr, setup_packet[0], setup_packet[1]);
#endif
  uint8_t const pio_rhport = RHPORT_PIO(rhport);
  return pio_usb_host_send_setup(pio_rhport, dev_addr, setup_packet);
}

//bool hcd_edpt_busy(uint8_t dev_addr, uint8_t ep_addr)
//{
//    // EPX is shared, so multiple device addresses and endpoint addresses share that
//    // so if any transfer is active on epx, we are busy. Interrupt endpoints have their own
//    // EPX so ep->active will only be busy if there is a pending transfer on that interrupt endpoint
//    // on that device
//    pico_trace("hcd_edpt_busy dev addr %d ep_addr 0x%x\r\n", dev_addr, ep_addr);
//    struct hw_endpoint *ep = get_dev_ep(dev_addr, ep_addr);
//    assert(ep);
//    bool busy = ep->active;
//    pico_trace("busy == %d\r\n", busy);
//    return busy;
//}

bool hcd_edpt_clear_stall(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  (void) rhport;
  (void) dev_addr;
  (void) ep_addr;

  return true;
}

static void __no_inline_not_in_flash_func(handle_endpoint_irq)(root_port_t *rport, xfer_result_t result,
                                                               volatile uint32_t *ep_reg) {
  (void) rport;
  const uint32_t ep_all = *ep_reg;

  for ( uint8_t ep_idx = 0; ep_idx < PIO_USB_EP_POOL_CNT; ep_idx++ ) {
    uint32_t const mask = (1u << ep_idx);

    if ( ep_all & mask ) {
      endpoint_t * ep = PIO_USB_ENDPOINT(ep_idx);
#if USB_DEBUG_PRINTS
      printf("[HCD IRQ] Ep xfer complete: dev %d, ep 0x%02X, actual_len %d, result %d\n",
             ep->dev_addr, ep->ep_num, ep->actual_len, result);
#endif
      hcd_event_xfer_complete(ep->dev_addr, ep->ep_num, ep->actual_len, result, true);
    }
  }

  // clear all
  (*ep_reg) &= ~ep_all;
}

// IRQ Handler
void __no_inline_not_in_flash_func(pio_usb_host_irq_handler)(uint8_t root_id) {
  uint8_t const tu_rhport = root_id + 1;
  root_port_t *rport = PIO_USB_ROOT_PORT(root_id);
  uint32_t const ints = rport->ints;

  if ( ints & PIO_USB_INTS_ENDPOINT_COMPLETE_BITS ) {
    handle_endpoint_irq(rport, XFER_RESULT_SUCCESS, &rport->ep_complete);
  }

  if ( ints & PIO_USB_INTS_ENDPOINT_STALLED_BITS ) {
    handle_endpoint_irq(rport, XFER_RESULT_STALLED, &rport->ep_stalled);
  }

  if ( ints & PIO_USB_INTS_ENDPOINT_ERROR_BITS ) {
    handle_endpoint_irq(rport, XFER_RESULT_FAILED, &rport->ep_error);
  }

  if ( ints & PIO_USB_INTS_CONNECT_BITS ) {
#if USB_DEBUG_PRINTS
    printf("[HCD] IRQ: Device Attach on Port %d\n", tu_rhport);
#endif
    hcd_event_device_attach(tu_rhport, true);
  }

  if ( ints & PIO_USB_INTS_DISCONNECT_BITS ) {
#if USB_DEBUG_PRINTS
    printf("[HCD] IRQ: Device Detach on Port %d\n", tu_rhport);
#endif
    hcd_event_device_remove(tu_rhport, true);
  }

  // clear all
  rport->ints &= ~ints;
}

#endif
