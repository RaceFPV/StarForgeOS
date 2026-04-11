# MAX2871+ Test

Bare-minimum diagnostic for the MAX2871/MAX2871+ wideband PLL synthesizer,
designed for use with a downstream mixer at a fixed IF.

The MAX2871 is programmed to **LO = channel − IF_OFFSET_MHZ** (low-side injection),
so the mixer output lands at a fixed IF regardless of channel. Auto-cycle steps
through all 8 Raceband channels in sequence, checking PLL lock before each hop.

## Pinout

| Signal | GPIO | Notes |
|--------|------|-------|
| DATA   | 6    | SPI MOSI |
| CLK    | 4    | SPI CLK |
| LE     | 7    | SPI Latch Enable — idles LOW, pulses HIGH to latch (opposite polarity from RX5808 CS) |
| LD     | 8    | Lock Detect input — HIGH = locked. GPIO8 is a strapping pin on ESP32-C3; safe after boot. |
| MUX    | 10   | MUXOUT input — power-on SPI check only. GPIO10 has no strapping function. |
| RSSI   | 3    | Analog IF / detector level (ADC1). ESP32-C3 ADC1 is only GPIO0–4; **GPIO4 is SPI CLK** — change `RSSI_ADC_PIN` in `main.cpp` if your wiring differs. |

## Configuration

Both constants live at the top of `src/main.cpp`:

```cpp
#define REF_FREQ_MHZ   50   // MAX2871 REF_IN (MHz); must match hardware (this board: 50 MHz, R=1)
#define IF_OFFSET_MHZ  434  // mixer IF; LO = channel - IF_OFFSET_MHZ
```

**1 MHz step resolution** is automatic: with **R divider = 1**, `f_PFD` equals
`REF_FREQ_MHZ`; `MOD` is set to that value in frac-N so each step is
`f_PFD / MOD = 1 MHz`. Works for any integer MHz reference (e.g. 10, 25, 40, 50 MHz).

**Mixer injection side:** the code uses low-side injection (`LO = channel − IF`).
To switch to high-side injection, change `setChannel()` to use `channel + IF_OFFSET_MHZ`.

## Raceband Channel → LO Mapping

| Channel | RF (MHz) | LO (MHz) | IF out |
|---------|----------|----------|--------|
| R1      | 5658     | 5224     | 434 MHz |
| R2      | 5695     | 5261     | 434 MHz |
| R3      | 5732     | 5298     | 434 MHz |
| R4\*    | 5800     | 5366     | 434 MHz |
| R5      | 5806     | 5372     | 434 MHz |
| R6      | 5843     | 5409     | 434 MHz |
| R7      | 5880     | 5446     | 434 MHz |
| R8      | 5917     | 5483     | 434 MHz |

\*Fourth table slot / `c 4` is **5800 MHz** for bench (auto-cycle B); standard Raceband R4 is **5769 MHz** if you need to match external gear.

## Serial Commands

| Command | Description |
|---------|-------------|
| `f <freq>` | Set channel frequency in MHz (LO offset applied automatically), e.g. `f 5658` |
| `c <1-8>` | Jump directly to a Raceband channel, e.g. `c 1` for R1. Disables auto-cycle. |
| `a` | Toggle auto-cycle — steps R1→R8, 5 sec per channel, checks LD before each hop |
| `l` | Read LD pin |
| `s` | Show status, full channel table with active marker, and current register values |
| `h` | Show help + full Raceband→LO mapping |
| `t <ms>` | Hop / fast re-lock timeout for auto-cycle and `b` (default **150** ms; range 10–5000) |
| `i <ms>` | Initial / conservative timeout for boot and manual `f` / `c` (default **3000** ms) |
| `m 0` / `m 1` | **0** = strict: µs **LD LOW→HIGH** (fails if LD never glitches low). **1** = **setLO end→LD HIGH** (for retunes that never show LOW; can be ~0 µs if LD stayed HIGH). |
| `b [n]` | Lock benchmark: **`n`** toggles A↔B (default 10); uses **`m`** for how time is measured. Warmup if you start on A. |

## References

Register bit-field positions and default register values were sourced from the
HackRF One open-source firmware:

- [`firmware/common/max2871_regs.c`](https://github.com/greatscottgadgets/hackrf/blob/main/firmware/common/max2871_regs.c) — bit-field set/get functions and power-on defaults for all six registers
- [`firmware/common/max2871.c`](https://github.com/greatscottgadgets/hackrf/blob/main/firmware/common/max2871.c) — SPI write sequence and DIVA/N frequency calculation logic
