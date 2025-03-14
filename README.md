# pico-synth
Pico based I2S Synthesizer


## 📥 1. Clone the Repository & Initialize Submodules

```sh
git clone --recursive https://github.com/HackerSchool/pico-synth.git
cd pico-synth
```

If you already cloned without `--recursive`, initialize submodules manually:

```sh
git submodule update --init --recursive
```

## 📦 2. Install the Pico SDK and Pico Extras

### **Option 1: Install via Raspberry Pi Repo (Recommended on Raspberry Pi OS)**
```sh
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### **Option 2: Manually Install (For Other Linux/Windows/macOS)**
#### **Clone & Set Up the Pico SDK**
```sh
mkdir -p ~/pico && cd ~/pico
git clone --recursive https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
```

#### **Clone & Set Up Pico Extras**
```sh
cd ~/pico
git clone --recursive https://github.com/raspberrypi/pico-extras.git
```

## 🌍 3. Set Environment Variables (Each Terminal Session)

Before building, set the paths so CMake can find the SDK and extras:

```sh
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_EXTRAS_PATH=~/pico/pico-extras
```

To make this **permanent**, add these lines to `~/.bashrc` or `~/.zshrc`:

```sh
echo 'export PICO_SDK_PATH=~/pico/pico-sdk' >> ~/.bashrc
echo 'export PICO_EXTRAS_PATH=~/pico/pico-extras' >> ~/.bashrc
source ~/.bashrc
```

## 🛠 4. Build the Project

1. **Create a Build Directory**
```sh
mkdir -p build && cd build
```

2. **Run CMake to Configure the Build**
```sh
cmake ..
```

3. **Compile the Code**
```sh
make -j$(nproc)
```

## 🔥 5. Flash to the Raspberry Pi Pico

### **Step 1: Put the Pico in Bootloader Mode**
- Hold down the **BOOTSEL** button on the Pico.
- Plug it into your computer via **USB**.
- It should appear as a **USB mass storage device** (RPI-RP2).

### **Step 2: Copy the `.uf2` Binary to the Pico**

Once built, the `pico-synth` binary (`.uf2` file) will be in `build/`:
```sh
cp build/pico-synth.uf2 /media/$USER/RPI-RP2/
```

The Pico will automatically reboot and start running the synthesizer!

---

## 🛠 Troubleshooting

- **"pico_sdk_import.cmake not found" error?**
  - Make sure `PICO_SDK_PATH` is set correctly.
  - Try running `source ~/.bashrc`.

- **No `*.uf2` file after `make`?**
  - Check the build log for errors.
  - Ensure `pico_enable_stdio_usb(${bin_name} 1)` is set in `CMakeLists.txt`.

---

## 📜 License
[GPLV3 License](LICENSE)
