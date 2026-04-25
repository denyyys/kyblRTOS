# kyblRTOS

A small FreeRTOS-based "user OS" for the **Raspberry Pi Pico WH** with a real
shell, WiFi, an SD-card filesystem, and a built-in text editor — all driven
over USB serial.

```
kybl> help
kybl> wifi connect myssid mypass
kybl> mount
kybl> kbltext notes.txt
```

## What's in it

- **Shell with history & line editing** — arrow keys, Ctrl-C, Ctrl-L, quoted
  args, hex/binary literals.
- **Pluggable command kernel** — every command is a `kybl_program_t` registered
  at boot; SD-loaded programs slot into the same registry.
- **WiFi (lwIP + cyw43)** — `wifi scan`, `wifi menu` (live AP picker),
  `wifi connect`, plus `ping`, `nslookup`, `netstat`, `traceroute`. Background
  watchdog auto-reconnects on silent de-auth via active gateway probe.
- **kyblFS over FatFs R0.15a + SPI SD card** — `mount`, `format`, `ls`, `cat`,
  `write`, `append`, `mkdir`, `rm`, `mv`, `stat`, `df`, `label`, `sdinfo`. All
  filesystem traffic serialised by a single recursive mutex.
- **kyblText** — minimal terminal editor (Ctrl-S save, Ctrl-Q quit, Ctrl-O
  open, arrows / Home / End / PgUp / PgDn).
- **Visible activity LEDs** — 4 binary-display LEDs on GP16–GP19 plus a red
  SD-activity LED on GP1 that lights only during real SPI bus traffic.
- **Built-in toys & diagnostics** — `snake`, `blinker`, `bounce`, `stress`,
  `fragcheck`, `stackcheck`, `mem`, `tasks`, `uptime`, `calc`, `rand`,
  `note`/`notes`.

## Hardware

| GPIO          | Function                                              |
|---------------|-------------------------------------------------------|
| GP1           | Red SD activity LED (active high, with 330 Ω resistor)|
| GP2 / GP3 / GP4 / GP5 | SD card SPI: CLK / MOSI / MISO / CS           |
| GP16–GP19     | 4-bit binary LED bar (LSB → MSB)                      |
| Built-in CYW43| WiFi (no extra wiring — Pico WH includes the chip)    |

USB CDC for the shell — no UART cable needed.

## Build & flash

Full step-by-step is in [`BUILD_GUIDE.md`](BUILD_GUIDE.md). The short version:

```bash
# Prerequisites: arm-none-eabi-gcc, cmake, the Pico SDK, FreeRTOS-Kernel.
git clone https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init
export PICO_SDK_PATH="$PWD/pico-sdk"

# Inside this repo:
git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git \
          lib/FreeRTOS-Kernel
mkdir build && cd build
cmake .. && make -j

# Flash:
# 1. Hold BOOTSEL on the Pico WH while plugging in USB.
# 2. Drag build/kyblRTOS.uf2 onto the RPI-RP2 drive.
```

Then connect:

```bash
screen /dev/tty.usbmodem* 115200      # macOS
screen /dev/ttyACM0       115200      # Linux
```

If your terminal swallows Ctrl-S as XOFF (most do), run `stty -ixon` first or
add `defflow off` to `~/.screenrc` — otherwise saving in `kbltext` will
freeze the screen.

## Project layout

```
kyblRTOS/
├── CMakeLists.txt
├── BUILD_GUIDE.md
├── README.md
├── include/                 # public headers (kyblrtos.h, kyblFS.h, ffconf.h, …)
├── src/
│   ├── main.c               # boot + scheduler + crash hooks
│   ├── shell.c              # USB CDC line editor
│   ├── kernel.c             # command registry / dispatcher
│   ├── commands.c           # built-in commands (help, mem, snake, …)
│   ├── net_commands.c       # wifi / ping / dns / traceroute / netstat
│   ├── fs_commands.c        # mount / ls / cat / write / label …
│   ├── kbltext.c            # the kyblText editor
│   ├── kyblFS.c             # mutex-protected FatFs wrapper
│   ├── sd_spi.c             # SD-over-SPI driver + activity LED
│   ├── fatfs_diskio.c       # FatFs disk-IO bridge
│   ├── fatfs_glue.c         # FatFs heap + sync glue
│   ├── wifi_manager.c       # cyw43 lifecycle + watchdog
│   └── led_display.c        # 4-bit LED bar
└── lib/                     # NOT committed — populated by the commands above
    ├── FreeRTOS-Kernel/     # cloned manually
    └── fatfs/               # auto-fetched at cmake time (R0.15a)
```

## Status

Personal project; everything in the table above works on real hardware
(Pico WH + 32 GB SDHC). File size in `kbltext` is capped at 16 KB, the
filesystem at one mounted volume, the program registry at 64 entries.
WiFi watchdog detects silent disconnects in ~10 s via active gateway
ping and auto-reconnects with exponential backoff.

## License

MIT.
