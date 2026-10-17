# ESLAB HW4: STM32 accelerometer BLE service

This repository contains the STM32 peripheral firmware and the Raspberry Pi
BLE central demo for the B-L475E-IOT01A board.

## Repository layout

- `stm32/`: STM32CubeL4 HeartRate BLE application with `TASK_BLE` and
  `TASK_ACC`, real LSM6DSL XYZ samples, sampling frequency control, and
  debugger-visible diagnostics.
- `rpi/`: Python Bleak central that subscribes to XYZ and motion notifications
  and writes the requested sampling frequency.
- `setup.sh`: one setup entry point for the host toolchain/SDK and the Pi BLE
  Python environment.

The STM32 firmware is based on ST's STM32CubeL4 v1.18.2 B-L475E-IOT01A
HeartRate example. The SDK is fetched from the official ST repository during
setup and is not copied into this repository.

## One-command setup and build

On macOS, run:

```sh
./setup.sh all
```

This creates the local Python environment, fetches STM32CubeL4 v1.18.2 if it is
not already present, and builds the firmware. The script uses an installed
STM32CubeIDE ARM toolchain or Homebrew's `arm-none-eabi-gcc`.

On Raspberry Pi OS, run `./setup.sh rpi` to install BlueZ/Python requirements
and create `rpi/.venv`. The firmware cross-build can also be prepared on Linux
with `./setup.sh stm32` (it installs the ARM cross-compiler through apt).

Build modes:

```sh
./setup.sh rpi       # BLE Python runtime and OS packages on Raspberry Pi OS
./setup.sh stm32     # STM32 toolchain, SDK, and firmware build
./setup.sh all       # both environments and the firmware build
```

The firmware artifacts are written to `stm32/build/eslab_hw4.elf` and
`stm32/build/eslab_hw4.bin`. To use an SDK already installed elsewhere:

```sh
make -C stm32 SDK_ROOT=/path/to/STM32Cube_FW_L4_V1.18.2
```

## Demo on the Raspberry Pi

Connect and power the STM32 board, then run:

```sh
./rpi/.venv/bin/python rpi/demo.py --sequence
```

The sequence subscribes to notifications, sets 10 Hz for 15 seconds, then
2 Hz for 15 seconds, and waits 30 seconds for significant motion. To stream at
one chosen rate until interrupted:

```sh
./rpi/.venv/bin/python rpi/demo.py --frequency 10
```

Use `Ctrl+C` to stop. Shut down the Pi with `sudo shutdown -h now` before
removing power to reduce SD-card corruption risk.

## GATT values

| Item | UUID | Format |
| --- | --- | --- |
| Service | `12345678-1234-5678-1234-56789abcdef0` | Primary service |
| XYZ | `12345678-1234-5678-1234-56789abcdef1` | Notification; 3 little-endian signed 16-bit values in mg |
| Frequency | `12345678-1234-5678-1234-56789abcdef2` | Write with response; little-endian unsigned 16-bit Hz (1–100) |
| Motion | `12345678-1234-5678-1234-56789abcdef3` | Optional notification; `01` indicates significant motion |

The course basic requirements are the XYZ and frequency characteristics.
Motion is an extra demonstration feature.

## Flash and hardware verification

Open `stm32/build/eslab_hw4.elf` in STM32CubeIDE or STM32CubeProgrammer and
flash it to the B-L475E-IOT01A through the CN1 ST-LINK USB connector using SWD.
Then run the Pi demo. `TASK_ACC` reads the board's LSM6DSL sensor; the source
has debugger counters for sample count, queue delivery, CCCD state, ACI notify
status, frequency writes, and motion interrupts. A successful compile does not
itself confirm behavior on physical hardware, so verify the notification flow
on the board after flashing.
