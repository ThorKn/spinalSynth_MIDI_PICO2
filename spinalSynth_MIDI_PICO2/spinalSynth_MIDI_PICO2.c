#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "usb_midi_host.h"

// Synthesizer Register Addresses
#define REG_FREQ_LOW     0x00
#define REG_FREQ_MID     0x01
#define REG_FREQ_HIGH    0x02
#define REG_WAVE_SEL     0x03
#define REG_PWM_WIDTH    0x04
#define REG_VOLUME       0x05

#define REG_ENV_CTRL     0x40
#define REG_ENV_ATTACK   0x41
#define REG_ENV_DECAY    0x42
#define REG_ENV_SUSTAIN  0x43
#define REG_ENV_RELEASE  0x44
#define REG_ENV_GATE     0x45

#define CMD_WRITE        0x01

// Synthesizer Constants
#define UPDATE_RATE_HZ   480000.0f
#define PHASE_ACC_BITS   24
#define MAX_FREQ_WORD    ((1 << PHASE_ACC_BITS) - 1)

// Pico UART Defines
#define UART_ID          uart0
#define BAUD_RATE        115200
#define UART_TX_PIN      0
#define UART_RX_PIN      1

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 0
#endif

// Stateful register cache
static uint8_t env_ctrl_state = 0x01; // Bit 0 is always true (1), others 0

// Write 3-byte UART frame: [CMD_WRITE] [Address] [Data]
static void write_register(uint8_t address, uint8_t data) {
    uart_putc(UART_ID, CMD_WRITE);
    uart_putc(UART_ID, address);
    uart_putc(UART_ID, data);
    printf("[UART TX] Reg 0x%02X -> Value: 0x%02X (%d)\n", address, data, data);
}

// Convert MIDI note number to Frequency and write low, mid, high registers
static void set_midi_note(uint8_t note_number) {
    // f = 440.0 * 2^((note - 69) / 12)
    float freq_hz = 440.0f * powf(2.0f, (float)(note_number - 69) / 12.0f);
    
    // freqWord = round(f * 2^24 / updateRate)
    float word_f = (freq_hz * (float)(1 << PHASE_ACC_BITS)) / UPDATE_RATE_HZ;
    uint32_t freq_word = (uint32_t)roundf(word_f);
    if (freq_word > MAX_FREQ_WORD) {
        freq_word = MAX_FREQ_WORD;
    }

    uint8_t low_byte  = freq_word & 0xFF;
    uint8_t mid_byte  = (freq_word >> 8) & 0xFF;
    uint8_t high_byte = (freq_word >> 16) & 0xFF;

    // Atomic write order
    write_register(REG_FREQ_LOW, low_byte);
    write_register(REG_FREQ_MID, mid_byte);
    write_register(REG_FREQ_HIGH, high_byte);
}

static void set_env_loop_mode(bool enabled) {
    if (enabled) {
        env_ctrl_state |= (1 << 2);
    } else {
        env_ctrl_state &= ~(1 << 2);
    }
    write_register(REG_ENV_CTRL, env_ctrl_state);
}

static void set_env_curve_model(uint8_t curve_model) {
    if (curve_model > 3) curve_model = 3;
    env_ctrl_state &= ~0x30; // Clear bits 4 and 5
    env_ctrl_state |= (curve_model << 4);
    write_register(REG_ENV_CTRL, env_ctrl_state);
}

static void set_env_gate(bool gate_on) {
    uint8_t gate_byte = gate_on ? 0x01 : 0x00;
    write_register(REG_ENV_GATE, gate_byte);
}

// Implementation of TinyUSB weak callback to register custom MIDI class driver
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    static usbh_class_driver_t const midi_driver = {
        .name = "MIDI",
        .init = midih_init,
        .deinit = midih_deinit,
        .open = midih_open,
        .set_config = midih_set_config,
        .xfer_cb = midih_xfer_cb,
        .close = midih_close
    };
    *driver_count = 1;
    return &midi_driver;
}

//--------------------------------------------------------------------
// TinyUSB Host MIDI Callbacks
//--------------------------------------------------------------------

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx) {
    printf("MIDI Device address %u mounted (IN Ep: %u, OUT Ep: %u)\n", dev_addr, in_ep, out_ep);
    (void) num_cables_rx;
    (void) num_cables_tx;
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("MIDI Device address %u unmounted\n", dev_addr);
    (void) instance;
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    (void) num_packets;
    uint8_t packet[4];

    while (tuh_midi_packet_read(dev_addr, packet)) {
        uint8_t status = packet[1];
        uint8_t byte1  = packet[2];
        uint8_t byte2  = packet[3];
        
        uint8_t msg_type = status & 0xF0;

        if (msg_type == 0x90) { // Note On
            uint8_t note = byte1;
            uint8_t velocity = byte2;
            if (velocity > 0) {
                set_midi_note(note);
                set_env_gate(true);
                printf("[MIDI IN] Note On: %d, Velocity: %d\n", note, velocity);
            } else {
                set_env_gate(false);
                printf("[MIDI IN] Note Off (Note On with Velocity=0): %d\n", note);
            }
        } 
        else if (msg_type == 0x80) { // Note Off
            uint8_t note = byte1;
            set_env_gate(false);
            printf("[MIDI IN] Note Off: %d\n", note);
        } 
        else if (msg_type == 0xB0) { // Control Change (CC)
            uint8_t ctrl = byte1;
            uint8_t val  = byte2;

            if (ctrl == 1) { // PWM Width
                uint8_t pwm = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_PWM_WIDTH, pwm);
                printf("[MIDI CC] CC 1 (PWM Width) -> %d\n", pwm);
            } 
            else if (ctrl == 2) { // Wave Select
                uint8_t wave = val / 22;
                if (wave > 5) wave = 5;
                write_register(REG_WAVE_SEL, wave);
                printf("[MIDI CC] CC 2 (Wave Mux) -> %d\n", wave);
            } 
            else if (ctrl == 3) { // Attack
                uint8_t attack = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_ENV_ATTACK, attack);
                printf("[MIDI CC] CC 3 (Attack) -> %d\n", attack);
            } 
            else if (ctrl == 4) { // Decay
                uint8_t decay = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_ENV_DECAY, decay);
                printf("[MIDI CC] CC 4 (Decay) -> %d\n", decay);
            } 
            else if (ctrl == 5) { // Sustain
                uint8_t sustain = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_ENV_SUSTAIN, sustain);
                printf("[MIDI CC] CC 5 (Sustain) -> %d\n", sustain);
            } 
            else if (ctrl == 6) { // Release
                uint8_t release = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_ENV_RELEASE, release);
                printf("[MIDI CC] CC 6 (Release) -> %d\n", release);
            } 
            else if (ctrl == 7) { // Loop Mode
                bool loop_enabled = (val >= 64);
                set_env_loop_mode(loop_enabled);
                printf("[MIDI CC] CC 7 (Loop Mode) -> %s\n", loop_enabled ? "ON" : "OFF");
            } 
            else if (ctrl == 8) { // Curve Model
                uint8_t curve = val / 32;
                if (curve > 3) curve = 3;
                set_env_curve_model(curve);
                printf("[MIDI CC] CC 8 (Curve Model) -> %d\n", curve);
            }
        }
    }
}

//--------------------------------------------------------------------
// Main Execution
//--------------------------------------------------------------------

int main()
{
    // Initialize standard IO (USB / UART)
    stdio_init_all();
    
    // Initialize USB Host Stack via TinyUSB
    tuh_init(BOARD_TUH_RHPORT);

    // Initialize physical UART link to spinalSynth
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Initial default register values
    sleep_ms(250); // Let clock settle
    write_register(REG_VOLUME, 0xFF);
    write_register(REG_ENV_CTRL, env_ctrl_state);

    printf("spinalSynth USB MIDI Host UART Bridge Initialized.\n");

    // Continuous execution loop
    while (true) {
        tuh_task();
    }
}
