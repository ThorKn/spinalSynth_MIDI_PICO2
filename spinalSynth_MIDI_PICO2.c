#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "usb_midi_host.h"

// Voice Base Address
#define VOICE_BASE_ADDR      0x10

// Voice-specific registers offsets
#define OSC_FREQ_LOW         0x05
#define OSC_FREQ_MID         0x06
#define OSC_FREQ_HIGH        0x07
#define OSC_WAVE_SEL         0x08
#define OSC_PWM_WIDTH        0x09
#define OSC_VOLUME           0x0A

#define ENV_CTRL             0x0D
#define ENV_ATTACK           0x0E
#define ENV_DECAY            0x0F
#define ENV_SUSTAIN          0x10
#define ENV_RELEASE          0x11
#define ENV_GATE             0x12

#define FILTER_CTRL          0x15
#define FILTER_MODE          0x16
#define FILTER_CUTOFF        0x17
#define FILTER_RESONANCE     0x18

// Computed Register Addresses
#define REG_FREQ_LOW         (VOICE_BASE_ADDR + OSC_FREQ_LOW)
#define REG_FREQ_MID         (VOICE_BASE_ADDR + OSC_FREQ_MID)
#define REG_FREQ_HIGH        (VOICE_BASE_ADDR + OSC_FREQ_HIGH)
#define REG_WAVE_SEL         (VOICE_BASE_ADDR + OSC_WAVE_SEL)
#define REG_PWM_WIDTH        (VOICE_BASE_ADDR + OSC_PWM_WIDTH)
#define REG_VOLUME           (VOICE_BASE_ADDR + OSC_VOLUME)

#define REG_ENV_CTRL         (VOICE_BASE_ADDR + ENV_CTRL)
#define REG_ENV_ATTACK       (VOICE_BASE_ADDR + ENV_ATTACK)
#define REG_ENV_DECAY        (VOICE_BASE_ADDR + ENV_DECAY)
#define REG_ENV_SUSTAIN      (VOICE_BASE_ADDR + ENV_SUSTAIN)
#define REG_ENV_RELEASE      (VOICE_BASE_ADDR + ENV_RELEASE)
#define REG_ENV_GATE         (VOICE_BASE_ADDR + ENV_GATE)

#define REG_FILTER_CTRL      (VOICE_BASE_ADDR + FILTER_CTRL)
#define REG_FILTER_MODE      (VOICE_BASE_ADDR + FILTER_MODE)
#define REG_FILTER_CUTOFF    (VOICE_BASE_ADDR + FILTER_CUTOFF)
#define REG_FILTER_RESONANCE (VOICE_BASE_ADDR + FILTER_RESONANCE)

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
static uint8_t env_ctrl_state = 0x00; // Bit 0 is always false(0), others 0

#define MODE_SELECT_PIN 2
#define USB_ROLE_SELECT_PIN 3

static bool is_host_mode = true;

#define NOTE_STACK_MAX 32
static uint8_t note_stack[NOTE_STACK_MAX];
static int note_stack_size = 0;

static void note_stack_push(uint8_t note) {
    for (int i = 0; i < note_stack_size; i++) {
        if (note_stack[i] == note) {
            for (int j = i; j < note_stack_size - 1; j++) {
                note_stack[j] = note_stack[j + 1];
            }
            note_stack_size--;
            break;
        }
    }
    if (note_stack_size < NOTE_STACK_MAX) {
        note_stack[note_stack_size] = note;
        note_stack_size++;
    }
}

static void note_stack_remove(uint8_t note) {
    for (int i = 0; i < note_stack_size; i++) {
        if (note_stack[i] == note) {
            for (int j = i; j < note_stack_size - 1; j++) {
                note_stack[j] = note_stack[j + 1];
            }
            note_stack_size--;
            i--;
        }
    }
}

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

static void process_midi_packet(uint8_t const packet[4]) {
    uint8_t status = packet[1];
    uint8_t byte1  = packet[2];
    uint8_t byte2  = packet[3];
    
    uint8_t msg_type = status & 0xF0;

    if (msg_type == 0x90) { // Note On
        uint8_t note = byte1;
        uint8_t velocity = byte2;
        if (velocity > 0) {
            if (gpio_get(MODE_SELECT_PIN)) { // Legato Mode (default / open)
                bool first_note = (note_stack_size == 0);
                note_stack_push(note);
                set_midi_note(note);
                if (first_note) {
                    set_env_gate(true);
                }
                printf("[MIDI IN] Note On: %d, Velocity: %d (Legato)\n", note, velocity);
            } else { // Retrigger Mode (jumper to GND)
                note_stack_size = 0;
                set_midi_note(note);
                set_env_gate(true);
                printf("[MIDI IN] Note On: %d, Velocity: %d (Retrigger)\n", note, velocity);
            }
        } else {
            if (gpio_get(MODE_SELECT_PIN)) { // Legato Mode
                note_stack_remove(note);
                if (note_stack_size == 0) {
                    set_env_gate(false);
                } else {
                    set_midi_note(note_stack[note_stack_size - 1]);
                }
            } else { // Retrigger Mode
                set_env_gate(false);
            }
            printf("[MIDI IN] Note Off (Note On with Velocity=0): %d\n", note);
        }
    } 
    else if (msg_type == 0x80) { // Note Off
        uint8_t note = byte1;
        if (gpio_get(MODE_SELECT_PIN)) { // Legato Mode
            note_stack_remove(note);
            if (note_stack_size == 0) {
                set_env_gate(false);
            } else {
                set_midi_note(note_stack[note_stack_size - 1]);
            }
        } else { // Retrigger Mode
            set_env_gate(false);
        }
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
            else if (ctrl == 9) { // Filter Bypass
                uint8_t bypass = (val >= 64) ? 2 : 0;
                write_register(REG_FILTER_CTRL, bypass);
                printf("[MIDI CC] CC 9 (Filter Bypass) -> %d (%s)\n", bypass, bypass ? "ON" : "OFF");
            }
            else if (ctrl == 10) { // Filter Mode
                uint8_t mode = val / 43;
                if (mode > 2) mode = 2;
                write_register(REG_FILTER_MODE, mode);
                const char* mode_str = (mode == 0) ? "LP" : ((mode == 1) ? "BP" : "HP");
                printf("[MIDI CC] CC 10 (Filter Mode) -> %d (%s)\n", mode, mode_str);
            }
            else if (ctrl == 11) { // Filter Cutoff
                uint8_t cutoff = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_FILTER_CUTOFF, cutoff);
                printf("[MIDI CC] CC 11 (Filter Cutoff) -> %d\n", cutoff);
            }
            else if (ctrl == 12) { // Filter Resonance
                uint8_t resonance = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_FILTER_RESONANCE, resonance);
                printf("[MIDI CC] CC 12 (Filter Resonance) -> %d\n", resonance);
            }
            else if (ctrl == 64) { // Volume
                uint8_t volume = (val * 2) > 255 ? 255 : (val * 2);
                write_register(REG_VOLUME, volume);
                printf("[MIDI CC] CC 64 (Volume) -> %d\n", volume);
            }
        }
    }

//--------------------------------------------------------------------
// TinyUSB Callback Implementations
//--------------------------------------------------------------------

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    (void) num_packets;
    uint8_t packet[4];
    while (tuh_midi_packet_read(dev_addr, packet)) {
        process_midi_packet(packet);
    }
}

void tud_midi_rx_cb(uint8_t itf) {
    (void) itf;
    uint8_t packet[4];
    while (tud_midi_packet_read(packet)) {
        process_midi_packet(packet);
    }
}

//--------------------------------------------------------------------
// Main Execution
//--------------------------------------------------------------------

int main()
{
    // Initialize standard IO (USB / UART)
    stdio_init_all();
    
    // Initialize physical UART link to spinalSynth
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Configure USB Role Selector Pin
    gpio_init(USB_ROLE_SELECT_PIN);
    gpio_set_dir(USB_ROLE_SELECT_PIN, GPIO_IN);
    gpio_pull_up(USB_ROLE_SELECT_PIN);
    
    // Let input settle and determine operational mode
    sleep_us(10);
    is_host_mode = gpio_get(USB_ROLE_SELECT_PIN);

    // Configure Legato Jumper Selector
    gpio_init(MODE_SELECT_PIN);
    gpio_set_dir(MODE_SELECT_PIN, GPIO_IN);
    gpio_pull_up(MODE_SELECT_PIN);
    
    // Initial default register values
    sleep_ms(250); // Let clock settle
    write_register(REG_VOLUME, 0x80);
    write_register(REG_ENV_CTRL, env_ctrl_state);
    write_register(REG_FILTER_CTRL, 0x00);
    write_register(REG_FILTER_MODE, 0x00);
    write_register(REG_FILTER_CUTOFF, 0xFF);
    write_register(REG_FILTER_RESONANCE, 0x00);

    if (is_host_mode) {
        tuh_init(BOARD_TUH_RHPORT);
        printf("spinalSynth USB MIDI Bridge Initialized [HOST MODE].\n");
    } else {
        tud_init(BOARD_TUH_RHPORT);
        printf("spinalSynth USB MIDI Bridge Initialized [DEVICE MODE].\n");
    }

    // Continuous execution loop
    while (true) {
        if (is_host_mode) {
            tuh_task();
        } else {
            tud_task();
        }
    }
}
