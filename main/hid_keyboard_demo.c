/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "hid_keyboard_demo.c"
 
// *****************************************************************************
/* EXAMPLE_START(hid_keyboard_demo): HID Keyboard Classic
 *
 * @text This HID Device example demonstrates how to implement
 * an HID keyboard. Without a HAVE_BTSTACK_STDIN, a fixed demo text is sent
 * If HAVE_BTSTACK_STDIN is defined, you can type from the terminal
 */
// *****************************************************************************


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "hardware/gpio.h"

#include "btstack.h"

#ifdef HAVE_BTSTACK_STDIN
#include "btstack_stdin.h"
#endif

// GPIO PINS
#define PIN_BUTTON_W 10  // GPIO2
#define PIN_BUTTON_A 11 // GPIO3
#define PIN_BUTTON_S 12 // GPIO4
#define PIN_BUTTON_D 13 // GPIO5
#define PIN_LED      15  // GPI15 (blue LED)

// LED blinking
#define LED_BLINK_INTERVAL_MS 500
static btstack_timer_source_t led_blink_timer;
static bool led_is_blinking = false;
static bool led_on_for_blinking_state = false; // Manages the ON/OFF state during blinking


// to enable demo text on POSIX systems
// #undef HAVE_BTSTACK_STDIN

// timing of keypresses
#define TYPING_KEYDOWN_MS  40
#define TYPING_DELAY_MS    40

// When not set to 0xffff, sniff and sniff subrating are enabled
static uint16_t host_max_latency = 1600;
static uint16_t host_min_timeout = 3200;

#define REPORT_ID 0x01

// close to USB HID Specification 1.1, Appendix B.1
const uint8_t hid_descriptor_keyboard[] = {

    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xa1, 0x01,                    // Collection (Application)

    // Report ID

    0x85, REPORT_ID,               // Report ID

    // Modifier byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0xe0,                    //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,                    //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)

    // Reserved byte (input)

    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x03,                    //   Input (Constant, Variable, Absolute)

    // LED report + padding (output)

    0x95, 0x05,                    //   Report Count (5)
    0x75, 0x01,                    //   Report Size (1)
    0x05, 0x08,                    //   Usage Page (LEDs)
    0x19, 0x01,                    //   Usage Minimum (Num Lock)
    0x29, 0x05,                    //   Usage Maximum (Kana)
    0x91, 0x02,                    //   Output (Data, Variable, Absolute)

    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x03,                    //   Report Size (3)
    0x91, 0x03,                    //   Output (Constant, Variable, Absolute)

    // Keycodes (input)

    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0xff,                    //   Logical Maximum (1)
    0x05, 0x07,                    //   Usage Page (Key codes)
    0x19, 0x00,                    //   Usage Minimum (Reserved (no event indicated))
    0x29, 0xff,                    //   Usage Maximum (Reserved)
    0x81, 0x00,                    //   Input (Data, Array)

    0xc0,                          // End collection
};

// 
#define CHAR_ILLEGAL     0xff
#define CHAR_RETURN     '\n'
#define CHAR_ESCAPE      27
#define CHAR_TAB         '\t'
#define CHAR_BACKSPACE   0x7f

// Simplified US Keyboard with Shift modifier

/**
 * English (US)
 */
static const uint8_t keytable_us_none [] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*   0-3 */
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',                   /*  4-13 */
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',                   /* 14-23 */
    'u', 'v', 'w', 'x', 'y', 'z',                                       /* 24-29 */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '-', '=', '[', ']', '\\', CHAR_ILLEGAL, ';', '\'', 0x60, ',',       /* 45-54 */
    '.', '/', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xa7,                                 /* 97-100 */
}; 

static const uint8_t keytable_us_shift[] = {
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /*  0-3  */
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',                   /*  4-13 */
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',                   /* 14-23 */
    'U', 'V', 'W', 'X', 'Y', 'Z',                                       /* 24-29 */
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',                   /* 30-39 */
    CHAR_RETURN, CHAR_ESCAPE, CHAR_BACKSPACE, CHAR_TAB, ' ',            /* 40-44 */
    '_', '+', '{', '}', '|', CHAR_ILLEGAL, ':', '"', 0x7E, '<',         /* 45-54 */
    '>', '?', CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,   /* 55-60 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 61-64 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 65-68 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 69-72 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 73-76 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 77-80 */
    CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL, CHAR_ILLEGAL,             /* 81-84 */
    '*', '-', '+', '\n', '1', '2', '3', '4', '5',                       /* 85-97 */
    '6', '7', '8', '9', '0', '.', 0xb1,                                 /* 97-100 */
}; 

// STATE

static uint8_t hid_service_buffer[300];
static uint8_t device_id_sdp_service_buffer[100];
static const char hid_device_name[] = "BTstack HID Keyboard";
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid;
static uint8_t hid_boot_device = 0;

// HID Report sending
static uint8_t                send_buffer_storage[16];
static btstack_ring_buffer_t  send_buffer;
static btstack_timer_source_t send_timer;
static uint8_t                send_modifier;
static uint8_t                send_keycode;
static bool                   send_active;

#ifdef HAVE_BTSTACK_STDIN
static bd_addr_t device_addr;
static const char * device_addr_string = "BC:EC:5D:E6:15:03";
#endif

static enum {
    APP_BOOTING,
    APP_NOT_CONNECTED,
    APP_CONNECTING,
    APP_CONNECTED
} app_state = APP_BOOTING;

// HID Keyboard lookup
static bool lookup_keycode(uint8_t character, const uint8_t * table, int size, uint8_t * keycode){
    int i;
    for (i=0;i<size;i++){
        if (table[i] != character) continue;
        *keycode = i;
        return true;
    }
    return false;
}

static bool keycode_and_modifer_us_for_character(uint8_t character, uint8_t * keycode, uint8_t * modifier){
    bool found;
    found = lookup_keycode(character, keytable_us_none, sizeof(keytable_us_none), keycode);
    if (found) {
        *modifier = 0;  // none
        return true;
    }
    found = lookup_keycode(character, keytable_us_shift, sizeof(keytable_us_shift), keycode);
    if (found) {
        *modifier = 2;  // shift
        return true;
    }
    return false;
}

static void led_blink_timer_handler(btstack_timer_source_t *ts){
    led_on_for_blinking_state = !led_on_for_blinking_state;
    gpio_put(PIN_LED, led_on_for_blinking_state);

    // Reschedule the timer
    btstack_run_loop_set_timer(ts, LED_BLINK_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static void update_led_for_app_state(void){
    switch(app_state){
        case APP_CONNECTED:
            if (led_is_blinking){
                btstack_run_loop_remove_timer(&led_blink_timer);
                led_is_blinking = false;
            }
            gpio_put(PIN_LED, 1); // LED aceso continuamente
            // log_info("LED: Solid ON (Connected)"); // Opcional: log
            break;

        case APP_BOOTING:
        case APP_NOT_CONNECTED:
        case APP_CONNECTING:
        default: // Qualquer outro estado implica não conectado
            if (!led_is_blinking){
                led_is_blinking = true;
                led_on_for_blinking_state = false; // Começa o ciclo de piscar com LED apagado
                gpio_put(PIN_LED, led_on_for_blinking_state); 

                btstack_run_loop_set_timer_handler(&led_blink_timer, led_blink_timer_handler);
                btstack_run_loop_set_timer(&led_blink_timer, LED_BLINK_INTERVAL_MS); 
                btstack_run_loop_add_timer(&led_blink_timer);
                // O primeiro toggle para ON ocorrerá após LED_BLINK_INTERVAL_MS
                // log_info("LED: Start Blinking (Not Connected/Booting/Connecting) - Initial OFF"); // Opcional: log
            }
            break;
    }
}

static void send_report(int modifier, int keycode){
    // setup HID message: A1 = Input Report, Report ID, Payload
    uint8_t message[] = {0xa1, REPORT_ID, modifier, 0, keycode, 0, 0, 0, 0, 0};
    hid_device_send_interrupt_message(hid_cid, &message[0], sizeof(message));
}

static void trigger_key_up(btstack_timer_source_t * ts){
    UNUSED(ts);
    hid_device_request_can_send_now_event(hid_cid);
}

static void send_next(btstack_timer_source_t * ts) {
    // get next key from buffer
    uint8_t character;
    uint32_t num_bytes_read = 0;
    btstack_ring_buffer_read(&send_buffer, &character, 1, &num_bytes_read);
    if (num_bytes_read == 0) {
        // buffer empty, nothing to send
        send_active = false;
    } else {
        send_active = true;
        // lookup keycode and modifier using US layout
        bool found = keycode_and_modifer_us_for_character(character, &send_keycode, &send_modifier);
        if (found) {
            // request can send now
            hid_device_request_can_send_now_event(hid_cid);
        } else {
            // restart timer for next character
            btstack_run_loop_set_timer(ts, TYPING_DELAY_MS);
            btstack_run_loop_add_timer(ts);
        }
    }
}

static void queue_character(char character){
    btstack_ring_buffer_write(&send_buffer, (uint8_t *) &character, 1);
    if (send_active == false) {
        send_next(&send_timer);
    }
}

// Demo Application

#ifdef HAVE_BTSTACK_STDIN

// On systems with STDIN, we can directly type on the console

static void stdin_process(char character){
    switch (app_state){
        case APP_BOOTING:
        case APP_CONNECTING:
            // ignore
            break;
        case APP_CONNECTED:
            // add char to send buffer
            queue_character(character);
            break;
        case APP_NOT_CONNECTED:
            printf("Connecting to %s...\n", bd_addr_to_str(device_addr));
            hid_device_connect(device_addr, &hid_cid);
            break;
        default:
            btstack_assert(false);
            break;
    }
}
#else

// On embedded systems, send constant demo text

#define TYPING_DEMO_PERIOD_MS 100

static const char * demo_text = "\n\nHello World!\n\nThis is the BTstack HID Keyboard Demo running on an Embedded Device.\n\n";
static int demo_pos;
static btstack_timer_source_t demo_text_timer;

static void demo_text_timer_handler(btstack_timer_source_t * ts){
    UNUSED(ts);

    // queue character
    uint8_t character = demo_text[demo_pos++];
    if (demo_text[demo_pos] == 0){
        demo_pos = 0;
    }
    queue_character(character);

    // set timer for next character
    btstack_run_loop_set_timer_handler(&demo_text_timer, demo_text_timer_handler);
    btstack_run_loop_set_timer(&demo_text_timer, TYPING_DEMO_PERIOD_MS);
    btstack_run_loop_add_timer(&demo_text_timer);
}

#endif

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size){
    UNUSED(channel);
    UNUSED(packet_size);
    uint8_t status;
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
                    app_state = APP_NOT_CONNECTED;
                    update_led_for_app_state();
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // ssp: inform about user confirmation request
                    log_info("SSP User Confirmation Request with numeric value '%06"PRIu32"'\n", hci_event_user_confirmation_request_get_numeric_value(packet));
                    log_info("SSP User Confirmation Auto accept\n");                   
                    break; 

                case HCI_EVENT_HID_META:
                    switch (hci_event_hid_meta_get_subevent_code(packet)){
                        case HID_SUBEVENT_CONNECTION_OPENED:
                            status = hid_subevent_connection_opened_get_status(packet);
                            if (status != ERROR_CODE_SUCCESS) {
                                // outgoing connection failed
                                printf("Connection failed, status 0x%x\n", status);
                                app_state = APP_NOT_CONNECTED;
                                update_led_for_app_state();
                                hid_cid = 0;
                                return;
                            }
                            app_state = APP_CONNECTED;
                            update_led_for_app_state();
                            hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
#ifdef HAVE_BTSTACK_STDIN                        
                            printf("HID Connected, please start typing...\n");
#else                        
                            printf("HID Connected, sending demo text...\n");
                            demo_text_timer_handler(NULL);
#endif
                            break;
                        case HID_SUBEVENT_CONNECTION_CLOSED:
                            btstack_run_loop_remove_timer(&send_timer);
                            printf("HID Disconnected\n");
                            app_state = APP_NOT_CONNECTED;
                            update_led_for_app_state();
                            hid_cid = 0;
                            break;
                        case HID_SUBEVENT_CAN_SEND_NOW:
                            if (send_keycode){
                                send_report(send_modifier, send_keycode);
                                // schedule key up
                                send_keycode = 0;
                                send_modifier = 0;
                                btstack_run_loop_set_timer_handler(&send_timer, trigger_key_up);
                                btstack_run_loop_set_timer(&send_timer, TYPING_KEYDOWN_MS);
                            } else {
                                send_report(0, 0);
                                // schedule next key down
                                btstack_run_loop_set_timer_handler(&send_timer, send_next);
                                btstack_run_loop_set_timer(&send_timer, TYPING_DELAY_MS);
                            }
                            btstack_run_loop_add_timer(&send_timer);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////

void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x4) {
        if (gpio == PIN_BUTTON_W) {
            stdin_process('W');
        } else if (gpio == PIN_BUTTON_A) {
            stdin_process('A');
        } else if (gpio == PIN_BUTTON_S) {
            stdin_process('S');
        } else if (gpio == PIN_BUTTON_D) {
            stdin_process('D');
        }
    }
}

/* @section Main Application Setup
 *
 * @text Listing MainConfiguration shows main application code. 
 * To run a HID Device service you need to initialize the SDP, and to create and register HID Device record with it. 
 * At the end the Bluetooth stack is started.
 */

/* LISTING_START(MainConfiguration): Setup HID Device */

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    (void)argc;
    (void)argv;

    // allow to get found by inquiry
    gap_discoverable_control(1);
    // use Limited Discoverable Mode; Peripheral; Keyboard as CoD
    gap_set_class_of_device(0x2540);
    // set local name to be identified - zeroes will be replaced by actual BD ADDR
    gap_set_local_name("HID CAUÊ Keyboard Demo 00:00:00:00:00:00");
    // allow for role switch in general and sniff mode
    gap_set_default_link_policy_settings( LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE );
    // allow for role switch on outgoing connections - this allow HID Host to become master when we re-connect to it
    gap_set_allow_role_switch(true);

    // L2CAP
    l2cap_init();

#ifdef ENABLE_BLE
    // Initialize LE Security Manager. Needed for cross-transport key derivation
    sm_init();
#endif

    // SDP Server
    sdp_init();
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));

    uint8_t hid_virtual_cable = 0;
    uint8_t hid_remote_wake = 1;
    uint8_t hid_reconnect_initiate = 1;
    uint8_t hid_normally_connectable = 1;

    hid_sdp_record_t hid_params = {
        // hid sevice subclass 2540 Keyboard, hid counntry code 33 US
        0x2540, 33, 
        hid_virtual_cable, hid_remote_wake, 
        hid_reconnect_initiate, hid_normally_connectable,
        hid_boot_device,
        host_max_latency, host_min_timeout,
        3200,
        hid_descriptor_keyboard,
        sizeof(hid_descriptor_keyboard),
        hid_device_name
    };
    
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    btstack_assert(de_get_len( hid_service_buffer) <= sizeof(hid_service_buffer));
    sdp_register_service(hid_service_buffer);

    // See https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers if you don't have a USB Vendor ID and need a Bluetooth Vendor ID
    // device info: BlueKitchen GmbH, product 1, version 1
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len( device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
    sdp_register_service(device_id_sdp_service_buffer);

    // HID Device
    hid_device_init(hid_boot_device, sizeof(hid_descriptor_keyboard), hid_descriptor_keyboard);
       
    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for HID events
    hid_device_register_packet_handler(&packet_handler);

#ifdef HAVE_BTSTACK_STDIN
    sscanf_bd_addr(device_addr_string, device_addr);
    btstack_stdin_setup(stdin_process);
#endif

    btstack_ring_buffer_init(&send_buffer, send_buffer_storage, sizeof(send_buffer_storage));

    
    int btn_pins[] = {PIN_BUTTON_W, PIN_BUTTON_A, PIN_BUTTON_S, PIN_BUTTON_D};
    
    for (int i = 0; i < 4; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
    }

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    update_led_for_app_state();

    gpio_set_irq_enabled_with_callback(PIN_BUTTON_W, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(PIN_BUTTON_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_BUTTON_S, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_BUTTON_D, GPIO_IRQ_EDGE_FALL, true);

    // turn on!
    hci_power_control(HCI_POWER_ON);
    return 0;
}
/* LISTING_END */
/* EXAMPLE_END */
