import machine
import os
import sdcard
import sys

# SPI0 on Pico: sck=18, mosi=19, miso=16
spi = machine.SPI(0,
                  baudrate=1_000_000,
                  polarity=0,
                  phase=0,
                  sck=machine.Pin(18),
                  mosi=machine.Pin(19),
                  miso=machine.Pin(16))

cs = machine.Pin(17, machine.Pin.OUT)

# Initialize SD card
sd = sdcard.SDCard(spi, cs)

# Mount SD card
vfs = os.VfsFat(sd)
os.mount(vfs, "/sd")

# List files and print to serial
print("Files on SD card:")
for path in os.listdir("/sd"):
    print(path)
