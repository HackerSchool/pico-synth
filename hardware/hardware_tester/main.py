from machine import Pin, I2C
import time
import ssd1306

# Initialize I2C_0 on pins 20 (SDA) and 21 (SCL)
i2c_0 = I2C(0, scl=Pin(21), sda=Pin(20))

# Initialize I2C_1 on pins 26 (SDA) and 27 (SCL)
i2c_1 = I2C(1, scl=Pin(27), sda=Pin(26))

# PCF8574 I/O expanders at addresses 0x20 and 0x21 on I2C_1
# These are the I/O expanders used to control de LEDs
pcf8574_addr_1 = 0x20
pcf8574_addr_2 = 0x21

# This array maps an index to a an LED pin in the I/O Expanders
# The indexes are the keys: from left to right, top to bottom
# Look at the key numbers diagram:
#####################################
# 0 | 1 | 2 | 3 | 8  | 9  | 10 | 11 #
# 4 | 5 | 6 | 7 | 12 | 13 | 14 | 15 #
#####################################
# The first 8 numbers are the pins of the Left I/0 expander
# Example: LED_MAP[2] = 4
# This controls LED at key 2
# Which is connected to pin 4 of the PCF8574
LED_MAP = [1,3,4,7,0,2,5,6,0,2,4,6,1,3,5,7]

# PCF8574 I2C address
# This I/O expander is used for the 4x4 switch matrix
pcf8574_addr_3 = 0x20

# Keymap layout
keys = [
    ['0', '4', '8', '12'],
    ['1', '5', '9', '13'],
    ['2', '6', '10', '14'],
    ['3', '7', '11', '15']
]

# PCF8574 pin mapping
# This is similar to the LEDs stuff
# Look at schematics to understand ig
COL_PINS = [0, 1, 6, 4]  # GP pins connected to rows
ROW_PINS = [2, 3, 5, 7]  # GP pins connected to columns

# Array to map a key number to a note 
key_to_note = ["Ctrl","C#","D#","Ctrl","C","D","E","F","F#","G#","A#","Ctrl","G","A","B","C"]

# Create an SSD1306 display object for I2C_1 (128x64 resolution, I2C address 0x3C)
oled = ssd1306.SSD1306_I2C(128, 64, i2c_1, addr=0x3C)
oled.rotate180()

# Rotary encoder setup
# Each item in the encoder list has the pins of the encoder
# i.e. encoder_0:
# clk pin (A) is connected to gpio 10
# dt pin (B) is connected to gpio 11
# switch of the encoder is connected to gpio 9

ENCODERS = [
    {"clk": 10, "dt": 11, "sw": 9},  # Encoder 0
    {"clk": 7,  "dt": 8,   "sw": 6},    # Encoder 1
    {"clk": 4,  "dt": 5,   "sw": 3},    # Encoder 2
    {"clk": 1,  "dt": 2,   "sw": 0},    # Encoder 3
]

#################################
#Helper functions below#
#################################

# Function to write data to a PCF8574 device
def write_pcf8574(i2c_bus, addr, data):
    i2c_bus.writeto(addr, bytes([data]))

def pwm_pulse(i2c_bus, addr, pin, duty_cycle, duration_ms=25):
    high_time = int((duty_cycle / 100.0) * duration_ms)
    low_time = duration_ms - high_time

    # Set pin LOW (active)
    data = 0xFF & ~(1 << pin)
    write_pcf8574(i2c_bus, addr, data)
    time.sleep_ms(high_time)

    # Set pin HIGH (inactive)
    data = 0xFF | (1 << pin)
    write_pcf8574(i2c_bus, addr, data)
    time.sleep_ms(low_time)


# Read the GPIO states on PCF8574 at address 0x20 (on I2C_0)
def read_pcf8574(i2c_bus, addr):
    # Read one byte from the PCF8574 at the given address
    data = i2c_bus.readfrom(addr, 1)
    return data[0]  # Return the byte containing the pin states

def scan_all_keys():
    """Scan the keypad and return all currently pressed keys."""
    pressed_keys = []
    for col_idx, col_pin in enumerate(COL_PINS):
        # Set all pins HIGH
        data = 0xFF
        # Drive current column LOW
        data &= ~(1 << col_pin)
        write_pcf8574(i2c_0, pcf8574_addr_3, data)
        time.sleep_us(1)
        val = read_pcf8574(i2c_0, pcf8574_addr_3)
        for row_idx, row_pin in enumerate(ROW_PINS):
            if not (val & (1 << row_pin)):
                pressed_keys.append(keys[row_idx][col_idx])
    return pressed_keys

def sw_on_state(current_keys):
    """Update and return state transitions for keys."""
    now = time.ticks_ms()
    events = []

    # Handle currently pressed keys
    for key in current_keys:
        if key not in key_states:
            key_states[key] = {"pressed_time": now, "held": False}
            events.append(("PRESSED", key))
        else:
            elapsed = time.ticks_diff(now, key_states[key]["pressed_time"])
            if elapsed > 1:
                key_states[key]["held"] = True
                events.append(("HOLD", key))

    # Handle released keys
    released_keys = [k for k in key_states if k not in current_keys]
    for key in released_keys:
        state = "RELEASED+HOLD" if key_states[key]["held"] else "RELEASED"
        events.append((state, key))
        del key_states[key]

    return events

# Encoder update function
def update_encoders():
    for enc in encoder_data:
        a = enc["clk"].value()
        b = enc["dt"].value()
        state = ENC_STATES[(a, b)]
        delta = (enc["last_state"] - state) % 4
        if delta == 1:
            enc["position"] += 1
        elif delta == 3:
            enc["position"] -= 1
        enc["last_state"] = state
        
    for enc in encoder_data:
        sw_val = enc["sw"].value()
        if sw_val == 0 and enc["last_sw"] == 1:
            enc["sw_pressed"] = True  # Button was just pressed
        else:
            enc["sw_pressed"] = False
        enc["last_sw"] = sw_val



#################################
#"Main" Function starts Here#
#################################
        
# Clear the display
oled.fill(0)

oled.text("Scanning for I2C", 0, 0)
oled.text("devices...", 0, 10)

# Update the display to show the text
oled.show()

time.sleep(1)

#Now start a procedure to detect I2C devices connect
#Also list the address of the connected devices

print("Scanning for I2C devices on I2C_1...")

# Scan for devices on I2C_1
devices_1 = i2c_1.scan()
if devices_1:
    print("I2C_1 devices found:")
    for device in devices_1:
        print("0x{:02X}".format(device))
else:
    print("No I2C devices found on I2C_1.")

print("Scanning for I2C devices on I2C_0...")

# Scan for devices on I2C_0
devices_0 = i2c_0.scan()
if devices_0:
    print("I2C_0 devices found:")
    for device in devices_0:
        print("0x{:02X}".format(device))
else:
    print("No I2C devices found on I2C_0.")

# Clear the display
oled.fill(0)

oled.text("Found I2C_0:", 0, 0)

# Print found devices on I2C_0
y_position = 10
for device in devices_0:
    oled.text("0x{:02X}".format(device), 0, y_position)
    y_position += 10
    
# Write text on the screen
oled.text("Found I2C_1:", 0, y_position)

# Print found devices on I2C_1
y_position += 10
for device in devices_1:
    oled.text("0x{:02X}".format(device), 0, y_position)
    y_position += 10

# Update the display to show the text
oled.show()

time.sleep(0.1)

# Now this is a for loop to light up the LEDS in a sequence
# From left to right
# First the top row then the bottom row
for i in range(0,1):
    dc = 10 + i*20
    duration = 50
    # The LEDs are very bright
    # Using fake PWM to control the brightness
    # Top Row
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[0], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[1], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[2], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[3], duty_cycle=dc, duration_ms=duration)

    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[8], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[9], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[10], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[11], duty_cycle=dc, duration_ms=duration)
    
    # Bottom Row
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[4], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[5], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[6], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_1, LED_MAP[7], duty_cycle=dc, duration_ms=duration)

    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[12], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[13], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[14], duty_cycle=dc, duration_ms=duration)
    pwm_pulse(i2c_1, pcf8574_addr_2, LED_MAP[15], duty_cycle=dc, duration_ms=duration)
    
# Key state tracking
last_key = None
key_held = False
last_pressed_time = 0

# Track key states using a dictionary
key_states = {}

# Encoder state lookup
ENC_STATES = {
    (0, 0): 0b00,
    (0, 1): 0b01,
    (1, 0): 0b10,
    (1, 1): 0b11
}

# Initialize encoder state
encoder_data = []

for conf in ENCODERS:
    clk_pin = Pin(conf["clk"], Pin.IN, Pin.PULL_UP)
    dt_pin = Pin(conf["dt"], Pin.IN, Pin.PULL_UP)
    sw_pin = Pin(conf["sw"], Pin.IN, Pin.PULL_UP)
    state = ENC_STATES[(clk_pin.value(), dt_pin.value())]
    encoder_data.append({
        "clk": clk_pin,
        "dt": dt_pin,
        "sw": sw_pin,
        "last_state": state,
        "position": 0,
        "last_sw": sw_pin.value(),
        "sw_pressed": False
    })


print("Press any key...")
write_pcf8574(i2c_0, pcf8574_addr_3, 0xFF)  # Default high state

# This below is the main loop
# Basically detect key presses and the encoder
while True:
    keys_pressed = scan_all_keys()
    events = sw_on_state(keys_pressed)
    oled.fill(0)
    y_position = 0
    
    for state, key in events:
        #print("State:", state)
        #print("Key:", key)
        # Write text on the screen
        oled.text(key_to_note[int(key)], 0, y_position)
        y_position += 10
        if key.isdigit() and (state == "PRESSED" or state == "HOLD"):
            idx = int(key)
            if 0 <= idx < len(LED_MAP):
                led_pin = LED_MAP[idx]
                if idx < 8:
                    pwm_pulse(i2c_1, pcf8574_addr_1, led_pin, 30, 50)
                else:
                    pwm_pulse(i2c_1, pcf8574_addr_2, led_pin, 30, 50)
            else:
                print("Key index out of LED_MAP range:", idx)
    
    update_encoders()
    # Display encoder positions
    for i, enc in enumerate(encoder_data):
        oled.text("E{}: {}".format(i, enc["position"]), 60, 10 * i)
        if enc["sw_pressed"]:
            oled.text("P", 50, 10 * i)
            #print(f"Encoder {i} pressed")

    oled.show()
    #time.sleep_ms(10)

