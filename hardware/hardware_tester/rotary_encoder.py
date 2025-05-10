from machine import Pin
import time

# Define encoder pins
pin_a = Pin(1, Pin.IN, Pin.PULL_UP)  # Adjust GPIO as needed
pin_b = Pin(2, Pin.IN, Pin.PULL_UP)

position = 0
last_state = pin_a.value()

while True:
    current_state = pin_a.value()
    if current_state != last_state:
        if pin_b.value() != current_state:
            position += 1  # Clockwise
        else:
            position -= 1  # Counterclockwise
        print("Position:", position)
    last_state = current_state
    time.sleep_ms(1)
