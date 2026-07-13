/*
 * Ravager / MAX2871+ Test - Superhet Receiver Diagnostic
 *
 * Bring-up firmware for Ravager, a discrete 5.8 GHz superheterodyne
 * RSSI receiver for FPV race gate timing. The receive path is:
 * antenna/U.FL → 5.8 GHz filter/LNA → LTC5562 mixer → 433.92 MHz SAW/IF
 * filter → LT5581 detector → ESP32-C3 ADC.
 *
 * Tests MAX2871/MAX2871+ synthesizer with the downstream mixer/RSSI path.
 * The chip is programmed to LO = channel - IF_OFFSET_MHZ so that the
 * mixer output lands at a fixed IF (default 434 MHz).
 *
 * Supports 1 MHz step resolution across the full 23–6000 MHz range.
 * Auto-cycle toggles two Raceband channels with a short hop lock timeout
 * (default 150 ms) and timing readout; boot uses a long initial window (3 s).
 *
 * !! CRITICAL: Set REF_FREQ_MHZ to the frequency at the MAX2871 REF_IN pin !!
 *    With R divider = 1 in R2, f_PFD = REF_FREQ_MHZ. MOD = that value in frac-N
 *    gives 1 MHz steps (f_PFD/MOD = 1 MHz). This board: 50 MHz REF_IN, R=1.
 *
 * Commands:
 *   f <freq>  Set channel frequency in MHz (LO offset applied automatically)
 *   c <1-8>   Jump to a Raceband channel (disables auto-cycle)
 *   a         Toggle auto-cycle through all Raceband channels (5 sec/step)
 *   l         Read Lock Detect pin
 *   p         Toggle MAX2871 on/off (register R2 SHDN — software shutdown, SPI stays up)
 *   s         Show status + current register values
 *   h         Show help
 *   t <ms>    Hop / fast re-lock timeout (auto-cycle, benchmark); default 150
 *   i <ms>    Initial / conservative timeout (boot, manual f/c); default 3000
 *   w <0-3>   LO output power: 0=-4 dBm, 1=-1 dBm, 2=+2 dBm, 3=+5 dBm
 *   b [n]     Benchmark re-lock (see m); default 10 hops
 *   m <0|1>   Lock timing: 0=LOW→HIGH (strict), 1=setLO end→HIGH (no unlock required)
 *   sweep [freq] [ms]  Generator-sweep RSSI stream (peak-hold CSV for plotting)
 *             sweep              Toggle stream at current channel
 *             sweep off          Stop stream
 *             sweep 5800         Set channel (LO = RF - IF), start stream
 *             sweep 5800 100     Print peak RSSI every 100 ms (50–2000)
 *   scan4 [duration_s] [settle_us] [f1 f2 f3 f4]
 *             500 Hz/channel proof-of-concept; default 10 seconds, 250 µs settle,
 *             F-band F1-F4 (5740/5760/5780/5800). Forces LO power to w 0.
 *             Uses R0-only fast hops and prints summary stats after the run.
 *   scan4raw [duration_s] [settle_us] [f1 f2 f3 f4]
 *             Same fast scan, stores raw samples in RAM, then dumps CSV.
 *
 * SPI protocol (MAX2871):
 *   - 32-bit words, MSB first
 *   - Data clocked in on rising CLK edge while LE is LOW
 *   - LE pulses HIGH to latch (active HIGH, opposite of RX5808 CS)
 *   - Must write registers in order: R5 → R4 → R3 → R2 → R1 → R0
 *
 * Register bit-field sources:
 *   - greatscottgadgets/hackrf firmware/common/max2871_regs.c
 *   - greatscottgadgets/hackrf firmware/common/max2871.c
 *   - Analog Devices MAX2871 datasheet
 */

#include <Arduino.h>
#include <stdlib.h>

// ─── Pin Definitions ────────────────────────────────────────────────────────
#define MAX2871_DATA_PIN    6     // SPI MOSI
#define MAX2871_CLK_PIN     4     // SPI CLK
#define MAX2871_LE_PIN      7     // SPI LE  (idles LOW, pulse HIGH to latch)
#define MAX2871_LD_PIN      8     // Lock Detect input — HIGH = locked
//                                // NOTE: GPIO8 is a strapping pin on ESP32-C3.
//                                // Sampled only at reset; safe to use after boot.
#define MAX2871_MUX_PIN     10    // MUXOUT input — GPIO10 has no strapping function
// RSSI from IF/detector analog output (ADC input). ESP32-C3 ADC1: GPIO0–4 only;
// GPIO4 is SPI CLK — use 0–3 or move SPI. Default GPIO3 — change to match wiring.
#define RSSI_ADC_PIN        3
// Match receiver_test: wait after LO change before RSSI sample (same ms as RX5808_MIN_TUNETIME).
#define RSSI_MIN_TUNETIME_MS 35

// Generator-sweep assist: peak-hold ADC per print interval (loop spins on ADC while streaming).
#define RSSI_STREAM_INTERVAL_MS_DEFAULT 100
#define RSSI_STREAM_INTERVAL_MS_MIN     50
#define RSSI_STREAM_INTERVAL_MS_MAX   2000

// Four-channel live-VTX scan: proof-of-concept target is 500 Hz per channel.
// With 4 channels, that means 2000 tuned slots/sec = 500 µs/slot.
#define SCAN4_NUM_CHANNELS              4
#define SCAN4_DURATION_S_DEFAULT       10
#define SCAN4_DURATION_S_MAX         3600
#define SCAN4_SETTLE_US_DEFAULT       250
#define SCAN4_SETTLE_US_MIN             0
#define SCAN4_SETTLE_US_MAX          5000
#define SCAN4_TARGET_HZ_PER_CHANNEL   500
#define SCAN4_TARGET_SLOT_US          (1000000UL / (SCAN4_TARGET_HZ_PER_CHANNEL * SCAN4_NUM_CHANNELS))
#define SCAN4_REPORT_INTERVAL_MS      500
#define SCAN4_ON_DELTA_ADC             25
#define SCAN4_OFF_DELTA_ADC            12
#define SCAN4_RAW_MAX_SAMPLES       30000

// ─── Configuration ──────────────────────────────────────────────────────────
// REF_IN frequency in MHz (into MAX2871). With R=1 in R2, f_PFD equals this.
// Frac-N: MOD = REF_FREQ_MHZ → step = f_PFD/MOD = 1 MHz (any integer REF MHz).
#define REF_FREQ_MHZ        50    // This hardware: 50 MHz reference, R divider 1

// Mixer IF offset in MHz.
// MAX2871 is programmed to LO = channel - IF_OFFSET_MHZ (low-side injection).
// Mixer output: IF = RF - LO = channel - (channel - offset) = IF_OFFSET_MHZ.
// Flip sign or swap formula here if your mixer uses high-side injection.
#define IF_OFFSET_MHZ       434

// Auto-cycle alternates between these two channels (by index into RACEBAND_CHANNELS).
// All 8 channels are still reachable via 'c <n>' and 'f <freq>'.
#define CYCLE_CHAN_A        0   // R1 = 5658 MHz
#define CYCLE_CHAN_B        3   // slot 4 = 5800 MHz (bench; std Raceband R4 is 5769)

// Lock detect: long window for first bring-up & manual tunes vs short window
// for measuring re-lock after hops (target often ≤150 ms on a tight loop).
#define LD_TIMEOUT_INITIAL_MS_DEFAULT   3000
#define LD_TIMEOUT_HOP_MS_DEFAULT       150
#define CYCLE_INTERVAL_MS               5000

static uint32_t g_ld_timeout_initial_ms = LD_TIMEOUT_INITIAL_MS_DEFAULT;
static uint32_t g_ld_timeout_hop_ms     = LD_TIMEOUT_HOP_MS_DEFAULT;

// 0 = time LOW→HIGH after tune (missed if LD never glitches low). 1 = time setLO end→LD HIGH.
#define LOCK_TIMING_RETUNE_STRICT   0
#define LOCK_TIMING_PROGRAM_EDGE    1
static uint8_t g_lock_timing_mode       = LOCK_TIMING_RETUNE_STRICT;

// ─── Raceband Channel Table ──────────────────────────────────────────────────
// Standard 8-channel Raceband (R-band) frequencies in MHz.
// LO = channel - IF_OFFSET_MHZ is calculated at runtime.
const uint16_t RACEBAND_CHANNELS[] = {
    5658,  // R1
    5695,  // R2
    5732,  // R3
    5800,  // slot 4 — 5800 MHz for this test fw (not std R4 5769)
    5806,  // R5
    5843,  // R6
    5880,  // R7
    5917,  // R8
};
const uint8_t NUM_CHANNELS = sizeof(RACEBAND_CHANNELS) / sizeof(RACEBAND_CHANNELS[0]);

// FatShark/IRC F-band channels. scan4 defaults to F1-F4 so 5800 MHz is included.
const uint16_t FBAND_CHANNELS[] = {
    5740,  // F1
    5760,  // F2
    5780,  // F3
    5800,  // F4
    5820,  // F5
    5840,  // F6
    5860,  // F7
    5880,  // F8
};
const uint8_t NUM_F_CHANNELS = sizeof(FBAND_CHANNELS) / sizeof(FBAND_CHANNELS[0]);

// ─── MAX2871 Default Register Bank ──────────────────────────────────────────
// Register address is embedded in bits[2:0] of each 32-bit word.
// Write order is ALWAYS R5 → R4 → R3 → R2 → R1 → R0 (per datasheet).
//
//   R5 0x00400005  LD[23:22]=01  digital lock detect, HIGH when locked
//   R4 0x638E823C  DIVA[22:20]=0 ÷1 (VCO direct, 3–6 GHz range)
//                  BS=1000     band-select clock divider for 50 MHz PFD → 50 kHz
//                  FB[23]=1      fundamental VCO feedback to N divider
//                  RFA_EN[5]=1   RF output A enabled
//                  APWR[4:3]=11  +5 dBm output power
//   R3 0x0000000B  CDM[16:15]=0  clock divider off; VAS autocalibration on
//   R2 0xC0005E42  LDS[31]=1    lock-detect speed for fPFD > 32 MHz
//                  SDN[30:29]=10 low-spur mode 1 (sigma-delta; vs 00 low-noise)
//                  R[23:14]=1    reference divider = 1 (no division)
//                  CP=1111       5.12 mA charge pump for ADI 40 kHz loop filter
//                  LDF[8]=0      boot default; setLO() sets 0=frac-N / 1=int-N
//                  PDP[6]=1      positive phase detector polarity
//   R1 0x2000FFF9  M[14:3]=4095  MOD placeholder — overwritten by setLO()
//                  P[26:15]=1    phase value
//   R0 0x007D0000  N placeholder — always overwritten by setLO()
//
// Source: greatscottgadgets/hackrf firmware/common/max2871_regs.c
uint32_t regs[6] = {
    0x007D0000,  // R0 — overwritten by setLO()
    0x2000FFF9,  // R1 — MOD=4095, Phase=1
    0xC0005E42,  // R2 — LDS=1; SDN=10 low-spur 1; R=1; CP=5.12 mA; LDF patched by setLO()
    0x0000000B,  // R3 — CDM off, VAS on
    0x638E823C,  // R4 — BS=1000 for 50 kHz band-select clock; DIVA patched by setLO()
    0x00400005,  // R5 — LD = digital lock detect
};

// APWR = R4[4:3]. Output power on RFOUTA.
static const int8_t APWR_DBM[4] = { -4, -1, +2, +5 };

struct __attribute__((packed)) Scan4RawSample {
    uint32_t t_us;
    uint16_t slot_adc;  // bits[15:14]=slot 0-3, bits[11:0]=ADC
};

// ─── State ──────────────────────────────────────────────────────────────────
uint32_t current_channel = RACEBAND_CHANNELS[0]; // actual RF channel freq (MHz)
uint32_t current_lo      = 0;                     // what MAX2871 is tuned to (MHz)
bool     auto_cycle_enabled = true;
bool     on_cycle_a         = true;   // if true, next auto hop → CYCLE_CHAN_B; else → CYCLE_CHAN_A
uint32_t last_cycle_time    = 0;

// micros() timestamp after last successful setLO() register burst (retune timing)
static uint32_t g_last_lo_program_us = 0;
// RSSI path mirrors receiver_test: first sample after setLO() waits RSSI_MIN_TUNETIME_MS
static bool     g_rssi_after_lo_change = false;
static uint32_t g_last_lo_change_ms    = 0;

// false = R2 SHDN=1 (datasheet low-power / shutdown — RF off; registers retained)
static bool     g_synth_powered        = true;

// Generator-sweep RSSI CSV stream (external sig gen sweeps; LO fixed at channel - IF).
static bool     g_rssi_stream_enabled  = false;
static uint32_t g_rssi_stream_interval_ms = RSSI_STREAM_INTERVAL_MS_DEFAULT;
static uint32_t g_rssi_stream_last_ms     = 0;
static uint16_t g_rssi_stream_peak_adc    = 0;
static uint32_t g_rssi_stream_sum_adc     = 0;
static uint32_t g_rssi_stream_read_count  = 0;

// ─── Prototypes ─────────────────────────────────────────────────────────────
void    spiWriteWord(uint32_t word);
void    spiWriteWordFast(uint32_t word);
bool    checkChip();
void    runHealthCheck();
void    initSynth();
void    setChannel(uint32_t channel_mhz, bool verbose = true);
void    setRssiStreamEnabled(bool on);
void    resetRssiStreamPeakHold();
void    printRssiStreamPeakLine();
void    setLO(uint32_t lo_mhz, bool verbose = true);
void    setLoPower(uint8_t apwr);
bool    waitForLock(uint32_t timeout_ms, uint32_t *out_lock_ms);
bool    waitForLockAfterRetune(uint32_t timeout_ms, uint32_t *out_relock_us);
bool    waitForLockProgramEdgeToHigh(uint32_t timeout_ms, uint32_t *out_us);
bool    waitForLockTimed(uint32_t timeout_ms, uint32_t *out_us);
void    runLockBenchmark(uint16_t num_hops);
void    runFourChannelScan(uint16_t duration_s, uint16_t settle_us, const uint8_t f_channel_indices[SCAN4_NUM_CHANNELS]);
void    runFourChannelRawCapture(uint16_t duration_s, uint16_t settle_us, const uint8_t f_channel_indices[SCAN4_NUM_CHANNELS]);
void    setSynthPowered(bool on);
bool    readLD();
static uint16_t readRssiAdcRaw();
static uint16_t readRssiAdcClamped();
uint8_t readRSSI();
int     readRssiMilliVolts();
void    processCommand(String cmd);
void    showHelp();
void    showStatus();

// ────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(921600);
    delay(100);

    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════════╗");
    Serial.println("║      MAX2871+ TEST - Synthesizer Diagnostic        ║");
    Serial.println("╚════════════════════════════════════════════════════╝");
    Serial.println();
    Serial.printf("Ref: %d MHz | IF offset: -%d MHz | LO = channel - %d\n",
                  REF_FREQ_MHZ, IF_OFFSET_MHZ, IF_OFFSET_MHZ);
    Serial.println("Type 'h' for help\n");

    pinMode(MAX2871_DATA_PIN, OUTPUT);
    pinMode(MAX2871_CLK_PIN,  OUTPUT);
    pinMode(MAX2871_LE_PIN,   OUTPUT);
    pinMode(MAX2871_LD_PIN,   INPUT);
    pinMode(MAX2871_MUX_PIN,  INPUT_PULLDOWN);

    pinMode(RSSI_ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // same as receiver_test (full-scale ADC path)

    digitalWrite(MAX2871_LE_PIN,   LOW);
    digitalWrite(MAX2871_CLK_PIN,  LOW);
    digitalWrite(MAX2871_DATA_PIN, LOW);

    // ── PLL init ─────────────────────────────────────────────────────────────
    // Full R5→R0 sequence must come first — output drivers (including MUXOUT)
    // are not active until R4 has been written. Running the SPI check before
    // this would leave MUXOUT unable to drive LOW regardless of MUX setting.
    Serial.println("Initializing PLL...");
    initSynth();

    // ── SPI presence check ───────────────────────────────────────────────────
    // Now that output drivers are active, toggle MUXOUT via R2 MUX field.
    // Two matching reads confirm SPI is reaching the chip.
    Serial.println("SPI check...");
    if (checkChip()) {
        Serial.println("✓ MAX2871 online\n");
    } else {
        Serial.println("✗ SPI check FAILED — chip not responding");
        Serial.println("  Check: power, DATA/CLK/LE/MUXOUT wiring\n");
        // Continue anyway — lock attempt below may give more info
    }

    // ── Initial channel ──────────────────────────────────────────────────────
    setChannel(current_channel);

    uint32_t lock_us = 0;
    if (waitForLockTimed(g_ld_timeout_initial_ms, &lock_us)) {
        Serial.printf("✓ PLL locked (initial health, %lu µs / %.2f ms)\n\n",
                      (unsigned long)lock_us, lock_us / 1000.0f);
    } else {
        Serial.printf("✗ Lock timeout (initial %lu ms window)\n",
                      (unsigned long)g_ld_timeout_initial_ms);
        runHealthCheck();
        Serial.printf("\n  If SPI is ok, check: REF_FREQ_MHZ (%d MHz), reference present, loop filter\n\n",
                      REF_FREQ_MHZ);
    }

    showStatus();
    Serial.println();
    showHelp();
    last_cycle_time = millis();
}

// ────────────────────────────────────────────────────────────────────────────

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) processCommand(cmd);
    }

    if (g_synth_powered && auto_cycle_enabled && !g_rssi_stream_enabled
        && (millis() - last_cycle_time >= CYCLE_INTERVAL_MS)) {
        uint8_t  next_idx = on_cycle_a ? CYCLE_CHAN_B : CYCLE_CHAN_A;
        uint32_t next     = RACEBAND_CHANNELS[next_idx];

        // Note lock state before hopping, but always proceed — this is a
        // diagnostic tool and we want to see frequency changes regardless
        if (!readLD()) {
            Serial.printf("\n! Not locked at %lu MHz before hop — hopping anyway\n",
                          current_channel);
        }

        Serial.printf("\n→ AUTO-CYCLE: %lu MHz → %lu MHz  (LO: %lu → %lu MHz)\n",
                      current_channel, next,
                      current_lo, next - IF_OFFSET_MHZ);

        setChannel(next);
        on_cycle_a = !on_cycle_a;
        last_cycle_time = millis();

        uint32_t hop_us = 0;
        if (waitForLockTimed(g_ld_timeout_hop_ms, &hop_us)) {
            Serial.printf("✓ Re-locked in %lu µs (%.2f ms)  [hop ≤%lu ms]  ch=%lu  LO=%lu MHz\n\n",
                          (unsigned long)hop_us,
                          hop_us / 1000.0f,
                          (unsigned long)g_ld_timeout_hop_ms,
                          current_channel, current_lo);
        } else {
            Serial.printf("✗ No lock within %lu ms  ch=%lu MHz  LO=%lu MHz\n",
                          (unsigned long)g_ld_timeout_hop_ms,
                          current_channel, current_lo);
            Serial.println("  → extended wait (initial timeout)…");
            uint32_t slow_ms = 0;
            uint32_t remain  = (g_ld_timeout_initial_ms > g_ld_timeout_hop_ms)
                                   ? (g_ld_timeout_initial_ms - g_ld_timeout_hop_ms)
                                   : g_ld_timeout_initial_ms;
            if (remain > 0 && waitForLock(remain, &slow_ms)) {
                Serial.printf("  ✓ Locked after hop miss → %lu ms extra (total from hop ~%lu ms)\n\n",
                              (unsigned long)slow_ms,
                              (unsigned long)(g_ld_timeout_hop_ms + slow_ms));
            } else {
                runHealthCheck();
                Serial.println();
            }
        }
    }

    if (g_rssi_stream_enabled) {
        uint16_t adc = readRssiAdcRaw();
        if (adc > g_rssi_stream_peak_adc) {
            g_rssi_stream_peak_adc = adc;
        }
        g_rssi_stream_sum_adc += adc;
        g_rssi_stream_read_count++;

        uint32_t now = millis();
        if (now - g_rssi_stream_last_ms >= g_rssi_stream_interval_ms) {
            printRssiStreamPeakLine();
            resetRssiStreamPeakHold();
            g_rssi_stream_last_ms = now;
        }
    } else {
        // Periodic status line (suppressed during generator-sweep stream)
        static uint32_t last_print = 0;
        if (millis() - last_print >= 1000) {
            uint16_t rssi_adc = readRssiAdcClamped();
            uint8_t  rssi     = rssi_adc >> 3;
            int      rssi_mv  = (int)((uint32_t)rssi_adc * 3300 / 4095);
            Serial.printf("[RSSI] %u | [LD: %s] ch=%lu MHz  LO=%lu MHz | AUTO: %s | SYNTH: %s | %.2f V\n",
                          rssi,
                          readLD() ? "LOCKED  " : "UNLOCKED",
                          current_channel, current_lo,
                          auto_cycle_enabled ? "ON" : "OFF",
                          g_synth_powered ? "ON " : "OFF",
                          rssi_mv / 1000.0f);
            last_print = millis();
        }
    }

    if (!g_rssi_stream_enabled) {
        delay(10);
    }
}

// ─── SPI ────────────────────────────────────────────────────────────────────
// MAX2871 SPI: 32-bit word MSB first, data valid on rising CLK, LE pulses
// HIGH to latch. LE idles LOW. Mirrors greatscottgadgets/hackrf max2871.c.
void spiWriteWord(uint32_t word) {
    // Brief LE HIGH discards any stale shift-register state, then start fresh
    digitalWrite(MAX2871_LE_PIN,   HIGH);
    digitalWrite(MAX2871_CLK_PIN,  LOW);
    digitalWrite(MAX2871_DATA_PIN, LOW);
    digitalWrite(MAX2871_LE_PIN,   LOW);

    for (int i = 31; i >= 0; i--) {
        digitalWrite(MAX2871_DATA_PIN, (word >> i) & 1 ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(MAX2871_CLK_PIN, HIGH);
        delayMicroseconds(1);
        digitalWrite(MAX2871_CLK_PIN, LOW);
        delayMicroseconds(1);
    }

    // Pulse LE HIGH to latch the addressed register
    digitalWrite(MAX2871_DATA_PIN, LOW);
    delayMicroseconds(1);
    digitalWrite(MAX2871_LE_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(MAX2871_LE_PIN, LOW);
}

// Fast path for scan4 only. ESP32 digitalWrite overhead is already far above
// MAX2871 setup/hold timing, so the conservative per-bit delays are omitted.
void spiWriteWordFast(uint32_t word) {
    digitalWrite(MAX2871_LE_PIN,   HIGH);
    digitalWrite(MAX2871_CLK_PIN,  LOW);
    digitalWrite(MAX2871_DATA_PIN, LOW);
    digitalWrite(MAX2871_LE_PIN,   LOW);

    for (int i = 31; i >= 0; i--) {
        digitalWrite(MAX2871_DATA_PIN, (word >> i) & 1 ? HIGH : LOW);
        digitalWrite(MAX2871_CLK_PIN, HIGH);
        digitalWrite(MAX2871_CLK_PIN, LOW);
    }

    digitalWrite(MAX2871_DATA_PIN, LOW);
    digitalWrite(MAX2871_LE_PIN, HIGH);
    digitalWrite(MAX2871_LE_PIN, LOW);
}

// ─── SPI Presence Check ──────────────────────────────────────────────────────
// Runs two independent checks, both without needing a locked PLL:
//
// CHECK 1 — LD pin toggle via R5 (GPIO 8)
//   Forces the LD pin HIGH then LOW using R5 LD[23:22] field, then reads it back.
//   R5 LD field:  00=low  01=digital LD (normal)  10=analog LD  11=high
//   Proves R5 writes are reaching the chip on a pin with a known connection.
//
// CHECK 2 — MUXOUT toggle via R2 (GPIO 10)
//   Drives MUXOUT to tristate, VDD, and GND using R2 MUX[28:26] (MUX[2:0]).
//   MUX[3] lives in R5[18]; values 0–7 only touch R2 (see hackrf max2871_set_MUX).
//   GPIO10 uses INPUT_PULLDOWN so tristate reads LOW when chip stops driving.
//   R2 MUX[2:0]:  000=tristate  001=VDD  010=GND
//   Proves R2 writes are reaching the chip on the MUXOUT pin.
//
// Both checks passing = SPI confirmed on two separate registers and two pins.
bool checkChip() {
    bool ld_ok  = true;
    bool mux_ok = true;

    // ── Check 1: LD pin forced states via R5 ─────────────────────────────────
    Serial.println("  [LD pin / R5]");
    const uint32_t LD_MASK = 0x3UL << 22;
    uint32_t r5_high = (regs[5] & ~LD_MASK) | (0x3UL << 22);  // LD = always HIGH
    uint32_t r5_low  = (regs[5] & ~LD_MASK) | (0x0UL << 22);  // LD = always LOW

    Serial.print("    LD forced HIGH ... ");
    spiWriteWord(r5_high);
    delayMicroseconds(50);
    bool ld_high = (digitalRead(MAX2871_LD_PIN) == HIGH);
    Serial.println(ld_high ? "HIGH ✓" : "LOW  ✗ (expected HIGH)");

    Serial.print("    LD forced LOW  ... ");
    spiWriteWord(r5_low);
    delayMicroseconds(50);
    bool ld_low = (digitalRead(MAX2871_LD_PIN) == LOW);
    Serial.println(ld_low ? "LOW  ✓" : "HIGH ✗ (expected LOW)");

    spiWriteWord(regs[5]);  // restore R5 to digital lock detect
    ld_ok = ld_high && ld_low;

    // ── Check 2: MUXOUT states via R2 ────────────────────────────────────────
    Serial.println("  [MUXOUT / R2]");
    // MUX[2:0] is R2[28:26], NOT [26:24] (those bits are RDIV2, DBR, etc.).
    const uint32_t MUX_MASK = 0x7UL << 26;
    uint32_t r2_tri = (regs[2] & ~MUX_MASK) | (0x0UL << 26);  // MUX = tristate
    uint32_t r2_vdd = (regs[2] & ~MUX_MASK) | (0x1UL << 26);  // MUX = VDD
    uint32_t r2_gnd = (regs[2] & ~MUX_MASK) | (0x2UL << 26);  // MUX = GND

    Serial.print("    MUXOUT = TRI ... ");
    spiWriteWord(r2_tri);
    delayMicroseconds(50);
    bool mux_tri = (digitalRead(MAX2871_MUX_PIN) == LOW);
    Serial.println(mux_tri ? "LOW  ✓"
                           : "HIGH ✗ — pull-up present or pin not connected");

    Serial.print("    MUXOUT = VDD ... ");
    spiWriteWord(r2_vdd);
    delayMicroseconds(50);
    bool mux_high = (digitalRead(MAX2871_MUX_PIN) == HIGH);
    Serial.println(mux_high ? "HIGH ✓" : "LOW  ✗ (expected HIGH)");

    Serial.print("    MUXOUT = GND ... ");
    spiWriteWord(r2_gnd);
    delayMicroseconds(50);
    bool mux_low = (digitalRead(MAX2871_MUX_PIN) == LOW);
    Serial.println(mux_low ? "LOW  ✓"
                           : "HIGH ✗ — chip can't sink or pin not connected");

    spiWriteWord(regs[2]);  // restore R2
    mux_ok = mux_tri && mux_high && mux_low;

    // If tristate and GND both read LOW but VDD also reads LOW, the pin is
    // almost certainly floating/unconnected rather than an SPI failure —
    // a connected chip driving VDD would overcome the pulldown.
    if (mux_tri && !mux_high && mux_low) {
        Serial.println("    ^ all LOW: MUXOUT likely not connected to GPIO 10");
    }

    return ld_ok && mux_ok;
}

// ─── Inline Health Check ─────────────────────────────────────────────────────
// Called on any lock failure. Runs the MUXOUT toggle test to distinguish
// "SPI broken / chip dead" from "chip alive but reference or loop filter issue".
// Re-writes all registers after the test because writing R2 during the MUX
// toggle briefly disturbs the PLL.
void runHealthCheck() {
    Serial.println("  ┌─ health check ──────────────────────────────");
    bool ok = checkChip();
    if (ok) {
        Serial.println("  │ ✓ SPI ok — likely cause: reference oscillator or loop filter");
    } else {
        Serial.println("  │ ✗ SPI failed — check power and DATA/CLK/LE wiring");
    }
    // Restore full register state after MUX toggle disturbed R2
    for (int r = 5; r >= 0; r--) {
        spiWriteWord(regs[r]);
        delayMicroseconds(100);
    }
    Serial.println("  └─ registers restored ───────────────────────");
}

// ─── Synth Init ──────────────────────────────────────────────────────────────
void initSynth() {
    const uint32_t saved_r4 = regs[4];
    const uint32_t rf_output_enable_mask = (1UL << 8) | (1UL << 5);  // RFB_EN, RFA_EN

    // Datasheet power-up: two full R5→R0 writes with RF outputs disabled.
    // The first pass powers/configures the device; the second pass starts VCO selection.
    regs[4] = saved_r4 & ~rf_output_enable_mask;
    for (uint8_t pass = 0; pass < 2; pass++) {
        for (int r = 5; r >= 0; r--) {
            spiWriteWord(regs[r]);
            delayMicroseconds(100);
        }
        if (pass == 0) {
            delay(25);  // datasheet requires >=20 ms between the two power-up writes
        }
    }

    // Restore the configured R4 output enables after VCO selection has been kicked.
    regs[4] = saved_r4;
    spiWriteWord(regs[4]);
    delayMicroseconds(100);

    Serial.println("  Datasheet power-up sequence complete (2× R5→R0 with RF outputs off, then R4 enable)");
}

// ─── Channel → LO Mapping ────────────────────────────────────────────────────
// Takes the actual RF channel frequency, subtracts IF_OFFSET_MHZ, and programs
// the MAX2871. Mixer output: IF = RF_in - LO = IF_OFFSET_MHZ.
void setChannel(uint32_t channel_mhz, bool verbose) {
    if (channel_mhz <= (uint32_t)IF_OFFSET_MHZ) {
        Serial.printf("ERROR: channel %lu MHz is <= IF offset (%d MHz)\n",
                      channel_mhz, IF_OFFSET_MHZ);
        return;
    }
    uint32_t lo = channel_mhz - IF_OFFSET_MHZ;
    if (verbose) {
        Serial.printf("  channel=%lu MHz  LO=%lu MHz  IF=%d MHz\n",
                      channel_mhz, lo, IF_OFFSET_MHZ);
    }
    current_channel = channel_mhz;
    setLO(lo, verbose);
}

// ─── LO Frequency Programming ────────────────────────────────────────────────
// Programs the MAX2871 directly to lo_mhz.
//
// 1 MHz resolution: MOD = REF_FREQ_MHZ → each FRAC step = f_REF/MOD = 1 MHz.
//
// MAX2871 frequency formula (R=1 ⇒ f_PFD = REF_FREQ_MHZ):
//   f_VCO = f_PFD × (N + FRAC/MOD)    [VCO must be 3000–6000 MHz]
//   f_OUT = f_VCO / 2^DIVA
//
// Bit-field positions (hackrf max2871_regs.c):
//   R0: INT[31], N[30:15], FRAC[14:3], addr[2:0]=000
//   R1: MOD[14:3]   (upper bits preserved)
//   R4: DIVA[22:20] (all other bits preserved)
void setLO(uint32_t lo_mhz, bool verbose) {
    if (lo_mhz < 23 || lo_mhz > 6000) {
        Serial.printf("ERROR: LO %lu MHz out of range (23–6000 MHz)\n", lo_mhz);
        return;
    }

    // Scale vco up with DIVA until it falls in the 3000–6000 MHz VCO range
    uint8_t  diva = 0;
    uint32_t vco  = lo_mhz;
    while (vco < 3000 && diva < 7) {
        vco *= 2;
        diva++;
    }
    if (vco < 3000) {
        Serial.printf("ERROR: LO %lu MHz cannot reach VCO band (3000–6000 MHz) with DIVA≤7\n",
                      lo_mhz);
        return;
    }

    uint32_t pfd  = REF_FREQ_MHZ;  // f_PFD with R=1 in R2 (see default regs[2])
    uint32_t N    = vco / pfd;
    uint32_t rem  = vco - (N * pfd);    // fractional remainder at f_PFD grid (MHz)

    // MOD = f_PFD (MHz) ⇒ each FRAC step = 1 MHz at the VCO.
    // In integer mode (rem==0) MOD is irrelevant; use minimum valid value of 2.
    bool     int_mode = (rem == 0);
    uint16_t MOD      = int_mode ? 2 : (uint16_t)pfd;
    uint16_t FRAC     = int_mode ? 0 : (uint16_t)rem;  // 0 ≤ FRAC < MOD ✓

    if (verbose) {
        Serial.printf("  → LO=%lu MHz: VCO=%lu, N=%lu, FRAC=%u, MOD=%u, DIVA=%u (%s-N)\n",
                      lo_mhz, vco, N, FRAC, MOD, diva, int_mode ? "integer" : "frac");
    }

    // Patch R0: INT[31], N[30:15], FRAC[14:3], addr=0
    regs[0] = (int_mode ? (1UL << 31) : 0UL) |
              ((uint32_t)N    << 15) |
              ((uint32_t)FRAC <<  3);

    // Patch R1: update MOD[14:3], preserve CPL/CPT/Phase and addr bits
    regs[1] = (regs[1] & ~(0xFFFUL << 3)) | ((uint32_t)MOD << 3);

    // Patch R4: update DIVA[22:20], preserve everything else
    regs[4] = (regs[4] & ~(0x7UL << 20)) | ((uint32_t)diva << 20);

    // LDF[8]: 1 = integer-N lock detect, 0 = frac-N (datasheet / HackRF convention)
    regs[2] = (regs[2] & ~(1UL << 8)) | (int_mode ? (1UL << 8) : 0UL);

    // Write all registers in required order R5→R0
    for (int r = 5; r >= 0; r--) {
        spiWriteWord(regs[r]);
        delayMicroseconds(100);
    }

    g_last_lo_program_us = micros();
    g_last_lo_change_ms   = millis();
    g_rssi_after_lo_change = true;
    current_lo = lo_mhz;
}

// R2 bit 5 SHDN: 1 = device shutdown (SPI/I²C registers retained per datasheet).
static void flushRegsToChip() {
    for (int r = 5; r >= 0; r--) {
        spiWriteWord(regs[r]);
        delayMicroseconds(100);
    }
}

void setLoPower(uint8_t apwr) {
    if (apwr > 3) return;

    regs[4] = (regs[4] & ~(0x3UL << 3)) | ((uint32_t)apwr << 3);
    spiWriteWord(regs[4]);  // R4 only: APWR update does not retrigger VAS/PLL tuning.
    delayMicroseconds(100);

    Serial.printf("\n✓ APWR=%u (%+d dBm)  R4=0x%08lX  LD=%s\n\n",
                  apwr, APWR_DBM[apwr], regs[4],
                  readLD() ? "LOCKED" : "UNLOCKED");
}

void setSynthPowered(bool on) {
    if (on) {
        regs[2] &= ~(1u << 5);
        g_synth_powered = true;
        setLO(current_lo);
        Serial.printf("\n✓ MAX2871 enabled (R2 SHDN=0)  LO=%lu MHz\n\n", current_lo);
    } else {
        regs[2] |= (1u << 5);
        g_synth_powered = false;
        flushRegsToChip();
        Serial.println("\n✓ MAX2871 software shutdown (R2 SHDN=1). RF/PLL off; registers kept in RAM.");
        Serial.println("  Type `p` again to turn back on and restore LO.\n");
    }
}

// ─── Lock Detect ─────────────────────────────────────────────────────────────
bool readLD() {
    return digitalRead(MAX2871_LD_PIN) == HIGH;
}

static void settleRssiAfterLoChange() {
    if (g_rssi_after_lo_change) {
        uint32_t elapsed = millis() - g_last_lo_change_ms;
        if (elapsed < RSSI_MIN_TUNETIME_MS) {
            delay(RSSI_MIN_TUNETIME_MS - elapsed);
        }
        g_rssi_after_lo_change = false;
    }
}

// Full 12-bit ADC read for sweep peak-hold logging (0-4095, no clamp).
static uint16_t readRssiAdcRaw() {
    settleRssiAfterLoChange();
    return analogRead(RSSI_ADC_PIN);
}

// Same ADC handling as receiver_test readRSSI(): settle delay, 12-bit read, clamp to 2047.
static uint16_t readRssiAdcClamped() {
    uint16_t adc_value = readRssiAdcRaw();
    if (adc_value > 2047) {
        adc_value = 2047;
    }
    return adc_value;
}

uint8_t readRSSI() {
    return readRssiAdcClamped() >> 3;
}

int readRssiMilliVolts() {
    uint16_t adc_value = readRssiAdcClamped();
    return (int)((uint32_t)adc_value * 3300 / 4095);
}

void resetRssiStreamPeakHold() {
    g_rssi_stream_peak_adc   = 0;
    g_rssi_stream_sum_adc    = 0;
    g_rssi_stream_read_count = 0;
}

void printRssiStreamPeakLine() {
    uint16_t peak = g_rssi_stream_peak_adc;
    uint32_t mean_x100 = (g_rssi_stream_read_count > 0)
                             ? (uint32_t)(((uint64_t)g_rssi_stream_sum_adc * 100ULL)
                                          / g_rssi_stream_read_count)
                             : 0;
    int      mv   = (int)((uint32_t)peak * 3300 / 4095);
    Serial.printf("%lu,%lu,%lu,%u,%lu.%02lu,%d,%lu\n",
                  (unsigned long)millis(),
                  (unsigned long)current_channel,
                  (unsigned long)current_lo,
                  (unsigned)peak,
                  (unsigned long)(mean_x100 / 100),
                  (unsigned long)(mean_x100 % 100),
                  mv,
                  (unsigned long)g_rssi_stream_read_count);
}

void setRssiStreamEnabled(bool on) {
    if (on == g_rssi_stream_enabled) {
        return;
    }
    g_rssi_stream_enabled = on;
    g_rssi_stream_last_ms = millis();
    resetRssiStreamPeakHold();
    if (on) {
        Serial.println();
        Serial.println("# RSSI stream ON — CSV: time_ms,channel_mhz,lo_mhz,adc_peak_raw_0_4095,adc_mean_raw_0_4095,mv,reads");
        Serial.printf("# LO fixed at channel-%d MHz; sweep external generator (e.g. 4900–5900 MHz)\n",
                      IF_OFFSET_MHZ);
        Serial.printf("# Expect sharp peaks at channel (~5800) and image (LO-%d ≈ %lu MHz at ch 5800)\n",
                      IF_OFFSET_MHZ,
                      (unsigned long)(current_lo > (uint32_t)IF_OFFSET_MHZ
                                          ? current_lo - IF_OFFSET_MHZ
                                          : 0));
        Serial.printf("# Peak-hold: raw 12-bit ADC sampled every loop pass; max and mean printed every %lu ms — type `sweep off` to stop\n",
                      (unsigned long)g_rssi_stream_interval_ms);
        Serial.println("# time_ms,channel_mhz,lo_mhz,adc_peak_raw_0_4095,adc_mean_raw_0_4095,mv,reads");
    } else {
        Serial.println();
        Serial.println("# RSSI stream OFF");
        Serial.println();
    }
}

// Waits up to timeout_ms for LD HIGH (e.g. first lock when LD may already be LOW).
// Sets *out_lock_ms to elapsed ms (rounded from µs).
bool waitForLock(uint32_t timeout_ms, uint32_t *out_lock_ms) {
    const uint32_t start_us = micros();
    const uint32_t limit_us = timeout_ms * 1000UL;
    if (out_lock_ms) {
        *out_lock_ms = 0;
    }
    while (!readLD()) {
        if ((micros() - start_us) >= limit_us) {
            return false;
        }
        delayMicroseconds(250);
    }
    uint32_t elapsed_ms = (micros() - start_us) / 1000UL;
    if (out_lock_ms) {
        *out_lock_ms = elapsed_ms;
    }
    return true;
}

// After setLO(): LD often stays HIGH briefly for the *old* lock, so a dumb wait-for-HIGH
// reads true immediately (~0 ms). Here we wait for LD LOW (unlock) then HIGH, timing
// the re-acquire in µs. Budget is total from g_last_lo_program_us.
// If LD is already LOW right after programming, t_low ≈ t_prog so this measures full
// program-edge → lock time.
bool waitForLockAfterRetune(uint32_t timeout_ms, uint32_t *out_relock_us) {
    const uint32_t t_prog   = g_last_lo_program_us;
    const uint32_t limit_us = timeout_ms * 1000UL;
    if (out_relock_us) {
        *out_relock_us = 0;
    }

    while (readLD()) {
        if ((micros() - t_prog) >= limit_us) {
            return false;
        }
        delayMicroseconds(5);
    }

    const uint32_t t_low = micros();

    while (!readLD()) {
        if ((micros() - t_prog) >= limit_us) {
            return false;
        }
        delayMicroseconds(5);
    }

    if (out_relock_us) {
        *out_relock_us = micros() - t_low;
    }
    return true;
}

// Time from end of setLO() until LD is HIGH (single PLL-aware lock). If LD was already
// HIGH before the new tune completes, returns quickly (~0 µs) — use for paths where
// digital LD does not pulse LOW (e.g. some int-N hops). Budget still from g_last_lo_program_us.
bool waitForLockProgramEdgeToHigh(uint32_t timeout_ms, uint32_t *out_us) {
    const uint32_t t_prog    = g_last_lo_program_us;
    const uint32_t limit_us  = timeout_ms * 1000UL;
    if (out_us) {
        *out_us = 0;
    }
    while (!readLD()) {
        if ((micros() - t_prog) >= limit_us) {
            return false;
        }
        delayMicroseconds(5);
    }
    if (out_us) {
        *out_us = micros() - t_prog;
    }
    return true;
}

bool waitForLockTimed(uint32_t timeout_ms, uint32_t *out_us) {
    if (g_lock_timing_mode == LOCK_TIMING_PROGRAM_EDGE) {
        return waitForLockProgramEdgeToHigh(timeout_ms, out_us);
    }
    return waitForLockAfterRetune(timeout_ms, out_us);
}

// Hop repeatedly CYCLE_CHAN_A ↔ CYCLE_CHAN_B; reports lock-time stats using hop timeout.
void runLockBenchmark(uint16_t num_hops) {
    if (num_hops < 1) {
        num_hops = 1;
    }
    Serial.printf("\n── Lock benchmark: %u hops  | timeout %lu ms  | mode %s ──\n", num_hops,
                  (unsigned long)g_ld_timeout_hop_ms,
                  g_lock_timing_mode == LOCK_TIMING_PROGRAM_EDGE
                      ? "setLO→LD HIGH"
                      : "LD LOW→HIGH (strict)");
    uint64_t    sum_us = 0;
    uint32_t    min_us = 0xFFFFFFFFUL, max_us = 0;
    uint16_t    ok = 0, fail = 0;

    // Hop [0] always targets A; if we're already there, LD may never glitch → bogus 0 / timeout.
    if (current_channel == RACEBAND_CHANNELS[CYCLE_CHAN_A]) {
        Serial.println("  (warmup: jump to B so first timed hop is a real retune)");
        setChannel(RACEBAND_CHANNELS[CYCLE_CHAN_B]);
        if (!waitForLockTimed(g_ld_timeout_hop_ms * 3u, nullptr)) {
            Serial.println("  ! warmup did not lock — benchmark results may be noisy");
        }
        delay(5);
    }

    for (uint16_t i = 0; i < num_hops; i++) {
        bool to_b       = (i & 1u) != 0;
        uint8_t idx     = to_b ? CYCLE_CHAN_B : CYCLE_CHAN_A;
        uint32_t target = RACEBAND_CHANNELS[idx];
        Serial.printf("  [%u/%u] → %lu MHz … ", (unsigned)(i + 1), (unsigned)num_hops, (unsigned long)target);
        setChannel(target);
        uint32_t t_us = 0;
        if (waitForLockTimed(g_ld_timeout_hop_ms, &t_us)) {
            Serial.printf("%lu µs (%.2f ms) ✓\n", (unsigned long)t_us, t_us / 1000.0f);
            sum_us += t_us;
            if (t_us < min_us) {
                min_us = t_us;
            }
            if (t_us > max_us) {
                max_us = t_us;
            }
            ok++;
        } else {
            Serial.printf("TIMEOUT (>%lu ms) ✗  [%s]\n",
                          (unsigned long)g_ld_timeout_hop_ms,
                          g_lock_timing_mode == LOCK_TIMING_PROGRAM_EDGE
                              ? "setLO→HIGH"
                              : "strict LOW→HIGH");
            fail++;
        }
        delayMicroseconds(500);
    }

    if (ok > 0) {
        uint32_t avg_us = (uint32_t)(sum_us / ok);
        Serial.printf("── Summary: ok=%u  fail=%u  avg=%lu µs (%.2f ms)  min=%lu µs  max=%lu µs ──\n\n",
                      ok, fail,
                      (unsigned long)avg_us, avg_us / 1000.0f,
                      (unsigned long)min_us,
                      (unsigned long)max_us);
    } else {
        Serial.printf("── Summary: all %u fail (hop timeout %lu ms) ──\n\n",
                      fail, (unsigned long)g_ld_timeout_hop_ms);
    }
}

void runFourChannelScan(uint16_t duration_s, uint16_t settle_us, const uint8_t f_channel_indices[SCAN4_NUM_CHANNELS]) {
    if (duration_s < 1) {
        duration_s = 1;
    }
    if (settle_us > SCAN4_SETTLE_US_MAX) {
        settle_us = SCAN4_SETTLE_US_MAX;
    }

    if (((regs[4] >> 3) & 0x3) != 0) {
        Serial.println("\n→ scan4 setting LO power to w 0 (-4 dBm) per Ravager bring-up notes");
        setLoPower(0);
    }

    uint32_t r0_words[SCAN4_NUM_CHANNELS];
    uint32_t channels[SCAN4_NUM_CHANNELS];
    uint32_t los[SCAN4_NUM_CHANNELS];

    // Prepare each F-band tune once with the full, safe register path, then use
    // R0-only writes in the timed loop. F-band scan channels all stay DIVA=0,
    // MOD=50, frac-N; only N/FRAC in R0 changes per slot.
    Serial.println("\n→ scan4 precomputing F-band tune words");
    for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
        uint8_t idx = f_channel_indices[i];
        channels[i] = FBAND_CHANNELS[idx];
        los[i] = channels[i] - IF_OFFSET_MHZ;
        setChannel(channels[i], false);
        r0_words[i] = regs[0];
        Serial.printf("  slot %u: F%u %lu MHz → LO %lu MHz  R0=0x%08lX\n",
                      (unsigned)(i + 1),
                      (unsigned)(idx + 1),
                      (unsigned long)channels[i],
                      (unsigned long)los[i],
                      (unsigned long)r0_words[i]);
    }

    Serial.printf("\n── scan4 rapid-hop POC: duration=%u s  settle=%u µs  target=%u Hz/channel (%lu µs/slot) ──\n",
                  (unsigned)duration_s,
                  (unsigned)settle_us,
                  SCAN4_TARGET_HZ_PER_CHANNEL,
                  (unsigned long)SCAN4_TARGET_SLOT_US);
    Serial.print("# Listening on:");
    for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
        uint8_t idx = f_channel_indices[i];
        Serial.printf(" F%u=%uMHz", idx + 1, FBAND_CHANNELS[idx]);
    }
    Serial.println();
    Serial.println("# Hot loop: F1→F2→F3→F4→F1 until time expires; R0-only PLL writes, no lock wait, no 35 ms RSSI settle.");
    Serial.println("# No per-sample serial output during the run; summary prints when scan4 completes.");
    Serial.printf("# Live detection: ON at baseline+%u ADC, OFF at baseline+%u ADC. Start with generators OFF for clean baselines.\n",
                  SCAN4_ON_DELTA_ADC,
                  SCAN4_OFF_DELTA_ADC);

    uint16_t min_adc[SCAN4_NUM_CHANNELS];
    uint16_t max_adc[SCAN4_NUM_CHANNELS];
    uint64_t sum_adc[SCAN4_NUM_CHANNELS];
    uint16_t interval_min_adc[SCAN4_NUM_CHANNELS];
    uint16_t interval_max_adc[SCAN4_NUM_CHANNELS];
    uint64_t interval_sum_adc[SCAN4_NUM_CHANNELS];
    uint32_t interval_reads[SCAN4_NUM_CHANNELS];
    uint16_t baseline_adc[SCAN4_NUM_CHANNELS];
    uint16_t last_adc[SCAN4_NUM_CHANNELS];
    uint16_t on_events[SCAN4_NUM_CHANNELS];
    uint16_t off_events[SCAN4_NUM_CHANNELS];
    bool signal_on[SCAN4_NUM_CHANNELS];
    for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
        min_adc[i] = 0xFFFFu;
        max_adc[i] = 0;
        sum_adc[i] = 0;
        interval_min_adc[i] = 0xFFFFu;
        interval_max_adc[i] = 0;
        interval_sum_adc[i] = 0;
        interval_reads[i] = 0;
        baseline_adc[i] = 0;
        last_adc[i] = 0;
        on_events[i] = 0;
        off_events[i] = 0;
        signal_on[i] = false;
    }

    uint32_t total_slots = 0;
    uint32_t completed_cycles = 0;
    uint32_t late_slots = 0;
    uint32_t max_slot_us = 0;
    uint32_t min_slot_us = 0xFFFFFFFFUL;
    uint64_t sum_slot_us = 0;

    const uint32_t run_start_ms = millis();
    const uint32_t run_start_us = micros();
    const uint32_t run_duration_us = (uint32_t)duration_s * 1000000UL;
    uint32_t last_report_ms = run_start_ms;

    while ((uint32_t)(micros() - run_start_us) < run_duration_us) {
        for (uint8_t slot = 0; slot < SCAN4_NUM_CHANNELS; slot++) {
            const uint32_t slot_start_us = micros();

            spiWriteWordFast(r0_words[slot]);
            current_channel = channels[slot];
            current_lo = los[slot];

            if (settle_us > 0) {
                delayMicroseconds(settle_us);
            }

            uint16_t adc = analogRead(RSSI_ADC_PIN);
            if (adc < min_adc[slot]) {
                min_adc[slot] = adc;
            }
            if (adc > max_adc[slot]) {
                max_adc[slot] = adc;
            }
            sum_adc[slot] += adc;
            last_adc[slot] = adc;

            if (adc < interval_min_adc[slot]) {
                interval_min_adc[slot] = adc;
            }
            if (adc > interval_max_adc[slot]) {
                interval_max_adc[slot] = adc;
            }
            interval_sum_adc[slot] += adc;
            interval_reads[slot]++;

            if (baseline_adc[slot] == 0) {
                baseline_adc[slot] = adc;
            }
            if (!signal_on[slot]) {
                if (adc < baseline_adc[slot]) {
                    baseline_adc[slot] = adc;
                } else {
                    // Slowly track floor drift while OFF without chasing short pulses.
                    baseline_adc[slot] = (uint16_t)(((uint32_t)baseline_adc[slot] * 255UL + adc) / 256UL);
                }

                if (adc >= (uint16_t)(baseline_adc[slot] + SCAN4_ON_DELTA_ADC)) {
                    signal_on[slot] = true;
                    on_events[slot]++;
                    Serial.printf("# EVENT t=%lu ms F%u ON  adc=%u base=%u delta=%d\n",
                                  (unsigned long)(millis() - run_start_ms),
                                  (unsigned)(f_channel_indices[slot] + 1),
                                  (unsigned)adc,
                                  (unsigned)baseline_adc[slot],
                                  (int)adc - (int)baseline_adc[slot]);
                }
            } else if (adc <= (uint16_t)(baseline_adc[slot] + SCAN4_OFF_DELTA_ADC)) {
                signal_on[slot] = false;
                off_events[slot]++;
                Serial.printf("# EVENT t=%lu ms F%u OFF adc=%u base=%u delta=%d\n",
                              (unsigned long)(millis() - run_start_ms),
                              (unsigned)(f_channel_indices[slot] + 1),
                              (unsigned)adc,
                              (unsigned)baseline_adc[slot],
                              (int)adc - (int)baseline_adc[slot]);
            }

            uint32_t elapsed_slot_us = micros() - slot_start_us;
            if (elapsed_slot_us < min_slot_us) {
                min_slot_us = elapsed_slot_us;
            }
            if (elapsed_slot_us > max_slot_us) {
                max_slot_us = elapsed_slot_us;
            }
            sum_slot_us += elapsed_slot_us;
            if (elapsed_slot_us > SCAN4_TARGET_SLOT_US) {
                late_slots++;
            }
            total_slots++;

            uint32_t now_ms = millis();
            if ((now_ms - last_report_ms) >= SCAN4_REPORT_INTERVAL_MS) {
                const uint32_t elapsed_us = micros() - run_start_us;
                const float aggregate_hz_now = elapsed_us > 0
                                                   ? ((float)total_slots * 1000000.0f / (float)elapsed_us)
                                                   : 0.0f;
                Serial.printf("# scan4 t=%lu ms  %.1f Hz/ch",
                              (unsigned long)(now_ms - run_start_ms),
                              aggregate_hz_now / (float)SCAN4_NUM_CHANNELS);

                for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
                    uint32_t mean = interval_reads[i] > 0
                                        ? (uint32_t)(interval_sum_adc[i] / interval_reads[i])
                                        : 0;
                    Serial.printf("  F%u:%s cur=%u min=%u max=%u mean=%lu on/off=%u/%u",
                                  (unsigned)(f_channel_indices[i] + 1),
                                  signal_on[i] ? "ON " : "off",
                                  (unsigned)last_adc[i],
                                  (unsigned)(interval_min_adc[i] == 0xFFFFu ? 0 : interval_min_adc[i]),
                                  (unsigned)interval_max_adc[i],
                                  (unsigned long)mean,
                                  (unsigned)on_events[i],
                                  (unsigned)off_events[i]);

                    interval_min_adc[i] = 0xFFFFu;
                    interval_max_adc[i] = 0;
                    interval_sum_adc[i] = 0;
                    interval_reads[i] = 0;
                }
                Serial.println();
                last_report_ms = now_ms;
            }
        }
        completed_cycles++;
    }

    const uint32_t run_elapsed_us = micros() - run_start_us;
    const float aggregate_hz = (run_elapsed_us > 0)
                                   ? ((float)total_slots * 1000000.0f / (float)run_elapsed_us)
                                   : 0.0f;
    const float hz_per_channel = aggregate_hz / (float)SCAN4_NUM_CHANNELS;
    const uint32_t avg_slot_us = total_slots > 0 ? (uint32_t)(sum_slot_us / total_slots) : 0;

    Serial.println("# Summary CSV: slot,f_channel,channel_mhz,lo_mhz,min_adc,max_adc,mean_adc,mv_at_mean,baseline_adc,on_events,off_events,final_state");
    for (uint8_t slot = 0; slot < SCAN4_NUM_CHANNELS; slot++) {
        uint8_t idx = f_channel_indices[slot];
        uint32_t mean_adc = completed_cycles > 0 ? (uint32_t)(sum_adc[slot] / completed_cycles) : 0;
        int mean_mv = (int)(mean_adc * 3300UL / 4095UL);
        Serial.printf("%u,F%u,%lu,%lu,%u,%u,%lu,%d,%u,%u,%u,%s\n",
                      (unsigned)(slot + 1),
                      (unsigned)(idx + 1),
                      (unsigned long)channels[slot],
                      (unsigned long)los[slot],
                      (unsigned)min_adc[slot],
                      (unsigned)max_adc[slot],
                      (unsigned long)mean_adc,
                      mean_mv,
                      (unsigned)baseline_adc[slot],
                      (unsigned)on_events[slot],
                      (unsigned)off_events[slot],
                      signal_on[slot] ? "ON" : "OFF");
    }

    Serial.printf("── scan4 timing: cycles=%lu  slots=%lu  elapsed=%lu µs  aggregate=%.1f slots/s  per_channel=%.1f Hz  target=%u Hz/channel ──\n",
                  (unsigned long)completed_cycles,
                  (unsigned long)total_slots,
                  (unsigned long)run_elapsed_us,
                  aggregate_hz,
                  hz_per_channel,
                  SCAN4_TARGET_HZ_PER_CHANNEL);
    Serial.printf("── slot work time: avg=%lu µs  min=%lu µs  max=%lu µs  late_slots=%lu/%lu  result=%s ──\n\n",
                  (unsigned long)avg_slot_us,
                  (unsigned long)min_slot_us,
                  (unsigned long)max_slot_us,
                  (unsigned long)late_slots,
                  (unsigned long)total_slots,
                  hz_per_channel >= (float)SCAN4_TARGET_HZ_PER_CHANNEL ? "PASS" : "MISS");
}

void runFourChannelRawCapture(uint16_t duration_s, uint16_t settle_us, const uint8_t f_channel_indices[SCAN4_NUM_CHANNELS]) {
    if (duration_s < 1) {
        duration_s = 1;
    }
    if (settle_us > SCAN4_SETTLE_US_MAX) {
        settle_us = SCAN4_SETTLE_US_MAX;
    }

    if (((regs[4] >> 3) & 0x3) != 0) {
        Serial.println("\n→ scan4raw setting LO power to w 0 (-4 dBm)");
        setLoPower(0);
    }

    Scan4RawSample *samples = (Scan4RawSample *)malloc(sizeof(Scan4RawSample) * SCAN4_RAW_MAX_SAMPLES);
    if (!samples) {
        Serial.printf("\n✗ scan4raw could not allocate %lu bytes for raw capture\n\n",
                      (unsigned long)(sizeof(Scan4RawSample) * SCAN4_RAW_MAX_SAMPLES));
        return;
    }

    uint32_t r0_words[SCAN4_NUM_CHANNELS];
    uint32_t channels[SCAN4_NUM_CHANNELS];
    uint32_t los[SCAN4_NUM_CHANNELS];

    Serial.println("\n→ scan4raw precomputing F-band tune words");
    for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
        uint8_t idx = f_channel_indices[i];
        channels[i] = FBAND_CHANNELS[idx];
        los[i] = channels[i] - IF_OFFSET_MHZ;
        setChannel(channels[i], false);
        r0_words[i] = regs[0];
        Serial.printf("  slot %u: F%u %lu MHz → LO %lu MHz\n",
                      (unsigned)(i + 1),
                      (unsigned)(idx + 1),
                      (unsigned long)channels[i],
                      (unsigned long)los[i]);
    }

    Serial.printf("\n── scan4raw capture: duration=%u s  settle=%u µs  max_samples=%lu ──\n",
                  (unsigned)duration_s,
                  (unsigned)settle_us,
                  (unsigned long)SCAN4_RAW_MAX_SAMPLES);
    Serial.println("# Capturing raw samples to RAM; CSV dumps after capture completes.");

    uint32_t count = 0;
    uint32_t dropped = 0;
    uint32_t total_slots = 0;
    uint32_t max_slot_us = 0;
    uint32_t min_slot_us = 0xFFFFFFFFUL;
    uint64_t sum_slot_us = 0;

    const uint32_t run_start_us = micros();
    const uint32_t run_duration_us = (uint32_t)duration_s * 1000000UL;

    while ((uint32_t)(micros() - run_start_us) < run_duration_us) {
        for (uint8_t slot = 0; slot < SCAN4_NUM_CHANNELS; slot++) {
            const uint32_t slot_start_us = micros();

            spiWriteWordFast(r0_words[slot]);
            current_channel = channels[slot];
            current_lo = los[slot];

            if (settle_us > 0) {
                delayMicroseconds(settle_us);
            }

            uint16_t adc = analogRead(RSSI_ADC_PIN) & 0x0FFFu;
            if (count < SCAN4_RAW_MAX_SAMPLES) {
                samples[count].t_us = micros() - run_start_us;
                samples[count].slot_adc = (uint16_t)(((uint16_t)slot << 14) | adc);
                count++;
            } else {
                dropped++;
            }

            uint32_t elapsed_slot_us = micros() - slot_start_us;
            if (elapsed_slot_us < min_slot_us) {
                min_slot_us = elapsed_slot_us;
            }
            if (elapsed_slot_us > max_slot_us) {
                max_slot_us = elapsed_slot_us;
            }
            sum_slot_us += elapsed_slot_us;
            total_slots++;
        }
    }

    const uint32_t run_elapsed_us = micros() - run_start_us;
    const float aggregate_hz = run_elapsed_us > 0
                                   ? ((float)total_slots * 1000000.0f / (float)run_elapsed_us)
                                   : 0.0f;
    const uint32_t avg_slot_us = total_slots > 0 ? (uint32_t)(sum_slot_us / total_slots) : 0;

    Serial.println("# SCAN4RAW_BEGIN");
    Serial.println("time_us,slot,f_channel,channel_mhz,lo_mhz,adc");
    for (uint32_t i = 0; i < count; i++) {
        uint8_t slot = (samples[i].slot_adc >> 14) & 0x3;
        uint16_t adc = samples[i].slot_adc & 0x0FFFu;
        uint8_t f_idx = f_channel_indices[slot];
        Serial.printf("%lu,%u,F%u,%lu,%lu,%u\n",
                      (unsigned long)samples[i].t_us,
                      (unsigned)(slot + 1),
                      (unsigned)(f_idx + 1),
                      (unsigned long)channels[slot],
                      (unsigned long)los[slot],
                      (unsigned)adc);
    }
    Serial.printf("# SCAN4RAW_END samples=%lu dropped=%lu elapsed_us=%lu aggregate_hz=%.1f per_channel_hz=%.1f avg_slot_us=%lu min_slot_us=%lu max_slot_us=%lu\n\n",
                  (unsigned long)count,
                  (unsigned long)dropped,
                  (unsigned long)run_elapsed_us,
                  aggregate_hz,
                  aggregate_hz / (float)SCAN4_NUM_CHANNELS,
                  (unsigned long)avg_slot_us,
                  (unsigned long)min_slot_us,
                  (unsigned long)max_slot_us);

    free(samples);
}

// ─── Commands ────────────────────────────────────────────────────────────────
void processCommand(String cmd) {
    cmd.trim();

    // Lowercase only the command letter, not the value
    String cmdLower = cmd;
    cmdLower.toLowerCase();

    if (cmdLower.startsWith("f ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable, then set frequency.\n");
            return;
        }
        int freq = cmd.substring(2).toInt();
        if (freq >= (IF_OFFSET_MHZ + 23) && freq <= (6000 + IF_OFFSET_MHZ)) {
            if (auto_cycle_enabled) {
                auto_cycle_enabled = false;
                Serial.println("\n→ AUTO-CYCLE DISABLED (manual freq set)");
            }
            Serial.printf("→ Setting channel %d MHz (LO = %d MHz)...\n",
                          freq, freq - IF_OFFSET_MHZ);
            setChannel((uint32_t)freq);
            uint32_t t_man_us = 0;
            if (waitForLockTimed(g_ld_timeout_initial_ms, &t_man_us)) {
                Serial.printf("✓ Locked in %lu µs (%.2f ms; window %lu ms)  ch=%d  LO=%d MHz\n\n",
                              (unsigned long)t_man_us,
                              t_man_us / 1000.0f,
                              (unsigned long)g_ld_timeout_initial_ms,
                              freq, freq - IF_OFFSET_MHZ);
            } else {
                Serial.printf("✗ Lock timeout (initial %lu ms)  ch=%d  LO=%d MHz\n",
                              (unsigned long)g_ld_timeout_initial_ms,
                              freq, freq - IF_OFFSET_MHZ);
                runHealthCheck();
                Serial.println();
            }
        } else {
            Serial.println("✗ Frequency out of range for current IF offset\n");
        }
    }
    else if (cmdLower.startsWith("c ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable, then select channel.\n");
            return;
        }
        int ch = cmd.substring(2).toInt();
        if (ch >= 1 && ch <= NUM_CHANNELS) {
            if (auto_cycle_enabled) {
                auto_cycle_enabled = false;
                Serial.println("\n→ AUTO-CYCLE DISABLED (manual channel set)");
            }
            uint32_t freq = RACEBAND_CHANNELS[ch - 1];
            Serial.printf("→ R%d: %lu MHz (LO = %lu MHz)...\n",
                          ch, freq, freq - IF_OFFSET_MHZ);
            setChannel(freq);
            uint32_t t_ch_us = 0;
            if (waitForLockTimed(g_ld_timeout_initial_ms, &t_ch_us)) {
                Serial.printf("✓ Locked in %lu µs (%.2f ms; window %lu ms)  R%d  ch=%lu MHz  LO=%lu MHz\n\n",
                             (unsigned long)t_ch_us,
                              t_ch_us / 1000.0f,
                              (unsigned long)g_ld_timeout_initial_ms,
                              ch, current_channel, current_lo);
            } else {
                Serial.printf("✗ Lock timeout (initial %lu ms)  R%d  channel=%lu MHz\n",
                              (unsigned long)g_ld_timeout_initial_ms,
                              ch, current_channel);
                runHealthCheck();
                Serial.println();
            }
        } else {
            Serial.printf("✗ Invalid channel. Use 1–%d\n\n", NUM_CHANNELS);
        }
    }
    else if (cmdLower == "a") {
        auto_cycle_enabled = !auto_cycle_enabled;
        if (auto_cycle_enabled && !g_synth_powered) {
            Serial.println("\n  (Auto-cycle enabled — synth is off; type `p` to run PLL.)\n");
        }
        if (auto_cycle_enabled) {
            // next_idx = on_cycle_a ? B : A — must point at the *other* channel, not where we already are
            // (otherwise re-enable after benchmark on 5800 would “hop” 5800→5800 for 5 s).
            on_cycle_a = (current_channel == RACEBAND_CHANNELS[CYCLE_CHAN_A]);
            Serial.printf("\n✓ AUTO-CYCLE ENABLED — R%d (%d MHz) ↔ R%d (%d MHz)\n\n",
                          CYCLE_CHAN_A + 1, RACEBAND_CHANNELS[CYCLE_CHAN_A],
                          CYCLE_CHAN_B + 1, RACEBAND_CHANNELS[CYCLE_CHAN_B]);
            last_cycle_time = millis();
        } else {
            Serial.println("\n✓ AUTO-CYCLE DISABLED\n");
        }
    }
    else if (cmdLower.startsWith("m ")) {
        int m = cmd.substring(2).toInt();
        if (m == 0) {
            g_lock_timing_mode = LOCK_TIMING_RETUNE_STRICT;
            Serial.println("\n✓ Lock timing: strict (wait LD LOW, then HIGH — re-lock window)\n");
        } else if (m == 1) {
            g_lock_timing_mode = LOCK_TIMING_PROGRAM_EDGE;
            Serial.println("\n✓ Lock timing: program edge → LD HIGH (no unlock pulse required)\n");
        } else {
            Serial.println("\n✗ m <0|1> — 0=strict LOW→HIGH  1=setLO end→HIGH\n");
        }
    }
    else if (cmdLower.startsWith("t ")) {
        int ms = cmd.substring(2).toInt();
        if (ms >= 10 && ms <= 5000) {
            g_ld_timeout_hop_ms = (uint32_t)ms;
            Serial.printf("\n✓ Hop / fast lock timeout = %lu ms\n\n",
                          (unsigned long)g_ld_timeout_hop_ms);
        } else {
            Serial.println("\n✗ t <ms> — use 10–5000 (typical 50–200 for fast re-lock)\n");
        }
    }
    else if (cmdLower.startsWith("i ")) {
        int ms = cmd.substring(2).toInt();
        if (ms >= 200 && ms <= 60000) {
            g_ld_timeout_initial_ms = (uint32_t)ms;
            Serial.printf("\n✓ Initial / conservative timeout = %lu ms\n\n",
                          (unsigned long)g_ld_timeout_initial_ms);
        } else {
            Serial.println("\n✗ i <ms> — use 200–60000\n");
        }
    }
    else if (cmdLower.startsWith("w ")) {
        int v = cmd.substring(2).toInt();
        if (v >= 0 && v <= 3) {
            setLoPower((uint8_t)v);
        } else {
            Serial.println("\n✗ w <0-3>  APWR: 0=-4 dBm  1=-1 dBm  2=+2 dBm  3=+5 dBm\n");
        }
    }
    else if (cmdLower == "b" || cmdLower.startsWith("b ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable before benchmark.\n");
            return;
        }
        uint16_t n = 10;
        if (cmdLower.startsWith("b ") && cmd.length() > 2) {
            int v = cmd.substring(2).toInt();
            if (v >= 1 && v <= 500) {
                n = (uint16_t)v;
            }
        }
        if (auto_cycle_enabled) {
            auto_cycle_enabled = false;
            Serial.println("\n→ AUTO-CYCLE DISABLED (benchmark)");
        }
        runLockBenchmark(n);
    }
    else if (cmdLower == "scan4raw" || cmdLower.startsWith("scan4raw ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable before scan4raw.\n");
            return;
        }

        char op[16] = {0};
        int duration_arg = SCAN4_DURATION_S_DEFAULT;
        int settle_arg = SCAN4_SETTLE_US_DEFAULT;
        int ch_arg[SCAN4_NUM_CHANNELS] = {1, 2, 3, 4};
        int parsed = sscanf(cmd.c_str(), "%15s %d %d %d %d %d %d",
                            op,
                            &duration_arg,
                            &settle_arg,
                            &ch_arg[0],
                            &ch_arg[1],
                            &ch_arg[2],
                            &ch_arg[3]);

        if (parsed >= 2 && (duration_arg < 1 || duration_arg > SCAN4_DURATION_S_MAX)) {
            Serial.printf("\n✗ scan4raw duration_s must be 1–%d\n\n", SCAN4_DURATION_S_MAX);
            return;
        }
        if (parsed >= 3 && (settle_arg < SCAN4_SETTLE_US_MIN || settle_arg > SCAN4_SETTLE_US_MAX)) {
            Serial.printf("\n✗ scan4raw settle_us must be %d–%d\n\n",
                          SCAN4_SETTLE_US_MIN,
                          SCAN4_SETTLE_US_MAX);
            return;
        }
        if (parsed > 3 && parsed < 7) {
            Serial.println("\n✗ scan4raw channel list requires exactly four F-band numbers: scan4raw 10 250 1 3 4 7\n");
            return;
        }

        uint8_t f_channel_indices[SCAN4_NUM_CHANNELS] = {0, 1, 2, 3};
        if (parsed >= 7) {
            for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
                if (ch_arg[i] < 1 || ch_arg[i] > NUM_F_CHANNELS) {
                    Serial.printf("\n✗ scan4raw channels must be F-band numbers 1–%d\n\n", NUM_F_CHANNELS);
                    return;
                }
                f_channel_indices[i] = (uint8_t)(ch_arg[i] - 1);
            }
        }

        if (g_rssi_stream_enabled) {
            setRssiStreamEnabled(false);
        }
        if (auto_cycle_enabled) {
            auto_cycle_enabled = false;
            Serial.println("\n→ AUTO-CYCLE DISABLED (scan4raw)");
        }

        runFourChannelRawCapture((uint16_t)duration_arg, (uint16_t)settle_arg, f_channel_indices);
    }
    else if (cmdLower == "scan4" || cmdLower.startsWith("scan4 ")
             || cmdLower == "q" || cmdLower.startsWith("q ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable before scan4.\n");
            return;
        }

        char op[16] = {0};
        int duration_arg = SCAN4_DURATION_S_DEFAULT;
        int settle_arg = SCAN4_SETTLE_US_DEFAULT;
        int ch_arg[SCAN4_NUM_CHANNELS] = {1, 2, 3, 4};
        int parsed = sscanf(cmd.c_str(), "%15s %d %d %d %d %d %d",
                            op,
                            &duration_arg,
                            &settle_arg,
                            &ch_arg[0],
                            &ch_arg[1],
                            &ch_arg[2],
                            &ch_arg[3]);

        if (parsed >= 2 && (duration_arg < 1 || duration_arg > SCAN4_DURATION_S_MAX)) {
            Serial.printf("\n✗ scan4 duration_s must be 1–%d\n\n", SCAN4_DURATION_S_MAX);
            return;
        }
        if (parsed >= 3 && (settle_arg < SCAN4_SETTLE_US_MIN || settle_arg > SCAN4_SETTLE_US_MAX)) {
            Serial.printf("\n✗ scan4 settle_us must be %d–%d\n\n",
                          SCAN4_SETTLE_US_MIN,
                          SCAN4_SETTLE_US_MAX);
            return;
        }
        if (parsed > 3 && parsed < 7) {
            Serial.println("\n✗ scan4 channel list requires exactly four F-band numbers: scan4 10 250 1 3 4 7\n");
            return;
        }

        uint8_t f_channel_indices[SCAN4_NUM_CHANNELS] = {0, 1, 2, 3};
        if (parsed >= 7) {
            for (uint8_t i = 0; i < SCAN4_NUM_CHANNELS; i++) {
                if (ch_arg[i] < 1 || ch_arg[i] > NUM_F_CHANNELS) {
                    Serial.printf("\n✗ scan4 channels must be F-band numbers 1–%d\n\n", NUM_F_CHANNELS);
                    return;
                }
                f_channel_indices[i] = (uint8_t)(ch_arg[i] - 1);
            }
        }

        if (g_rssi_stream_enabled) {
            setRssiStreamEnabled(false);
        }
        if (auto_cycle_enabled) {
            auto_cycle_enabled = false;
            Serial.println("\n→ AUTO-CYCLE DISABLED (four-channel scan)");
        }

        runFourChannelScan((uint16_t)duration_arg, (uint16_t)settle_arg, f_channel_indices);
    }
    else if (cmdLower == "sweep" || cmdLower.startsWith("sweep ")) {
        if (!g_synth_powered) {
            Serial.println("\n✗ Synth is shut down — type `p` to enable before RSSI stream.\n");
            return;
        }

        String args = cmd.substring(cmdLower.startsWith("sweep ") ? 6 : 5);
        args.trim();

        if (args.length() == 0) {
            if (!g_rssi_stream_enabled && auto_cycle_enabled) {
                auto_cycle_enabled = false;
                Serial.println("\n→ AUTO-CYCLE DISABLED (generator-sweep RSSI stream)");
            }
            setRssiStreamEnabled(!g_rssi_stream_enabled);
            return;
        }

        String argsLower = args;
        argsLower.toLowerCase();
        if (argsLower == "off" || argsLower == "stop" || argsLower == "0") {
            setRssiStreamEnabled(false);
            return;
        }

        int space = args.indexOf(' ');
        String freqStr = (space >= 0) ? args.substring(0, space) : args;
        String rest    = (space >= 0) ? args.substring(space + 1) : "";
        rest.trim();

        int freq = freqStr.toInt();
        if (freq < (IF_OFFSET_MHZ + 23) || freq > (6000 + IF_OFFSET_MHZ)) {
            Serial.println("\n✗ sweep <freq> [interval_ms] — channel out of range for IF offset\n");
            return;
        }

        g_rssi_stream_interval_ms = RSSI_STREAM_INTERVAL_MS_DEFAULT;

        if (rest.length() > 0) {
            int interval = rest.toInt();
            if (interval < RSSI_STREAM_INTERVAL_MS_MIN
                || interval > RSSI_STREAM_INTERVAL_MS_MAX) {
                Serial.printf("\n✗ Interval must be %d–%d ms\n\n",
                              RSSI_STREAM_INTERVAL_MS_MIN,
                              RSSI_STREAM_INTERVAL_MS_MAX);
                return;
            }
            g_rssi_stream_interval_ms = (uint32_t)interval;
        }

        if (auto_cycle_enabled) {
            auto_cycle_enabled = false;
            Serial.println("\n→ AUTO-CYCLE DISABLED (generator-sweep RSSI stream)");
        }

        Serial.printf("\n→ Generator-sweep setup: channel %d MHz → LO %d MHz (IF %d MHz)\n",
                      freq, freq - IF_OFFSET_MHZ, IF_OFFSET_MHZ);
        setChannel((uint32_t)freq, false);

        uint32_t t_g_us = 0;
        if (waitForLockTimed(g_ld_timeout_initial_ms, &t_g_us)) {
            Serial.printf("✓ Locked in %lu µs (%.2f ms)\n",
                          (unsigned long)t_g_us, t_g_us / 1000.0f);
        } else {
            Serial.printf("! Lock timeout (%lu ms) — streaming anyway (RSSI may be noisy)\n",
                          (unsigned long)g_ld_timeout_initial_ms);
        }

        setRssiStreamEnabled(true);
    }
    else if (cmdLower == "p") {
        setSynthPowered(!g_synth_powered);
    }
    else if (cmdLower == "l") {
        bool locked = readLD();
        Serial.printf("\n[Lock Detect] GPIO %d: %s\n"
                      "  channel=%lu MHz  LO=%lu MHz\n\n",
                      MAX2871_LD_PIN,
                      locked ? "HIGH (LOCKED)" : "LOW (UNLOCKED)",
                      current_channel, current_lo);
    }
    else if (cmdLower == "s") {
        Serial.println();
        showStatus();
        Serial.println();
    }
    else if (cmdLower == "h") {
        Serial.println();
        showHelp();
    }
    else {
        Serial.printf("✗ Unknown command: '%s' — type 'h' for help\n\n", cmd.c_str());
    }
}

// ─── Status ──────────────────────────────────────────────────────────────────
void showStatus() {
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("CURRENT STATUS:");
    Serial.println("═══════════════════════════════════════════════════");
    Serial.printf("  Channel:    %lu MHz\n", current_channel);
    Serial.printf("  LO:         %lu MHz  (channel - %d MHz)\n",
                  current_lo, IF_OFFSET_MHZ);
    Serial.printf("  Lock:       %s\n", readLD() ? "LOCKED" : "UNLOCKED");
    Serial.printf("  Synth:      %s  (command `p` — R2 SHDN)\n",
                  g_synth_powered ? "ON (running)" : "OFF (shutdown)");
    Serial.printf("  Auto-cycle: %s\n",
                  auto_cycle_enabled
                      ? "ENABLED  (R" + String(CYCLE_CHAN_A+1) + " ↔ R" + String(CYCLE_CHAN_B+1) + ")"
                      : "DISABLED");
    Serial.printf("  RSSI stream: %s",
                  g_rssi_stream_enabled ? "ON" : "OFF");
    if (g_rssi_stream_enabled) {
        Serial.printf("  (peak-hold, %lu ms)\n",
                      (unsigned long)g_rssi_stream_interval_ms);
    } else {
        Serial.println();
    }
    Serial.printf("  Reference:  %d MHz  (1 MHz steps via frac-N)\n", REF_FREQ_MHZ);
    Serial.printf("  Lock: initial timeout %lu ms  |  hop timeout %lu ms\n",
                  (unsigned long)g_ld_timeout_initial_ms,
                  (unsigned long)g_ld_timeout_hop_ms);
    Serial.printf("  Lock timing:  %s\n",
                  g_lock_timing_mode == LOCK_TIMING_PROGRAM_EDGE
                      ? "setLO end → LD HIGH (m 1)"
                      : "strict LD LOW→HIGH (m 0)");
    {
        uint8_t apwr = (regs[4] >> 3) & 0x3;
        Serial.printf("  LO power:   APWR=%u (%+d dBm)\n", apwr, APWR_DBM[apwr]);
    }
    Serial.printf("  Uptime:     %lu s\n", millis() / 1000);
    Serial.println();
    Serial.println("  Raceband Channels:");
    for (int i = 0; i < NUM_CHANNELS; i++) {
        Serial.printf("    R%d: %d MHz  →  LO %d MHz%s\n",
                      i + 1, RACEBAND_CHANNELS[i],
                      RACEBAND_CHANNELS[i] - IF_OFFSET_MHZ,
                      (RACEBAND_CHANNELS[i] == current_channel) ? "  ← active" : "");
    }
    Serial.println();
    Serial.println("  F-band Channels (scan4):");
    for (int i = 0; i < NUM_F_CHANNELS; i++) {
        Serial.printf("    F%d: %d MHz  →  LO %d MHz%s\n",
                      i + 1, FBAND_CHANNELS[i],
                      FBAND_CHANNELS[i] - IF_OFFSET_MHZ,
                      (FBAND_CHANNELS[i] == current_channel) ? "  ← active" : "");
    }
    Serial.println();
    Serial.println("  Register Bank (as last written):");
    for (int r = 0; r <= 5; r++) {
        Serial.printf("    R%d: 0x%08lX\n", r, regs[r]);
    }
    Serial.println();
    Serial.println("  Pins:");
    Serial.printf("    DATA: GPIO %d\n", MAX2871_DATA_PIN);
    Serial.printf("    CLK:  GPIO %d\n", MAX2871_CLK_PIN);
    Serial.printf("    LE:   GPIO %d\n", MAX2871_LE_PIN);
    Serial.printf("    LD:   GPIO %d\n", MAX2871_LD_PIN);
    Serial.printf("    MUX:  GPIO %d\n", MAX2871_MUX_PIN);
    {
        uint16_t rssi_adc = readRssiAdcClamped();
        uint8_t  r        = rssi_adc >> 3;
        int      mv       = (int)((uint32_t)rssi_adc * 3300 / 4095);
        Serial.printf("    RSSI: GPIO %d (ADC)  %u (0-255)  %.2f V\n",
                      RSSI_ADC_PIN, r, mv / 1000.0f);
    }
    Serial.println("═══════════════════════════════════════════════════");
}

// ─── Help ────────────────────────────────────────────────────────────────────
void showHelp() {
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("COMMANDS:");
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("  f <freq>  Set channel frequency in MHz");
    Serial.println("            LO offset applied automatically");
    Serial.println("            Example: f 5658  →  LO = 5224 MHz");
    Serial.println("            (Disables auto-cycle)");
    Serial.println();
    Serial.println("  c <1-8>   Jump to a Raceband channel");
    Serial.println("            Example: c 1  →  R1 (5658 MHz)");
    Serial.println("            (Disables auto-cycle)");
    Serial.println();
    Serial.printf("  a         Toggle AUTO-CYCLE (R%d ↔ R%d, 5 sec/step)\n",
                  CYCLE_CHAN_A + 1, CYCLE_CHAN_B + 1);
    Serial.println("            Fast hop timeout + lock ms; falls back to extended wait");
    Serial.println();
    Serial.println("  m <0|1>   Lock timing: 0=strict (LOW→HIGH)  1=setLO end→HIGH");
    Serial.println("  t <ms>    Hop / fast re-lock timeout (10–5000), default 150");
    Serial.println("  i <ms>    Initial timeout for boot & manual f/c (200–60000), default 3000");
    Serial.println("  w <0-3>   LO output power (APWR): 0=-4  1=-1  2=+2  3=+5 dBm");
    Serial.println("  b [n]     Benchmark n hops — min/avg/max µs (follows m)");
    Serial.println("  scan4 [duration_s] [settle_us] [f1 f2 f3 f4]");
    Serial.println("            500 Hz/channel F-band rapid-hop POC; forces w 0 first");
    Serial.println("            Typing only `scan4` uses the default below");
    Serial.println("            Default: scan4 10 250 1 2 3 4  (F4 = 5800 MHz)");
    Serial.println("            Compact 2 Hz live status + ON/OFF event lines, summary after run");
    Serial.println("            Example: scan4 60 150 1 3 4 7");
    Serial.println();
    Serial.println("  scan4raw [duration_s] [settle_us] [f1 f2 f3 f4]");
    Serial.println("            Raw sample capture; stores in RAM, dumps CSV after timing run");
    Serial.println("            Use scripts/capture_scan4raw.py to save to scan4.csv");
    Serial.println("            Default: scan4raw 10 250 1 2 3 4");
    Serial.println();
    Serial.println("  sweep [freq] [ms]  Generator-sweep RSSI CSV stream (peak-hold)");
    Serial.println("            sweep              Toggle stream at current channel");
    Serial.println("            sweep off          Stop stream");
    Serial.println("            sweep 5800         Set channel (LO = RF - IF), start stream");
    Serial.printf("            sweep 5800 %d      Peak printed every %d ms (default)\n",
                  RSSI_STREAM_INTERVAL_MS_DEFAULT,
                  RSSI_STREAM_INTERVAL_MS_DEFAULT);
    Serial.printf("            sweep 5800 50      Faster print interval (%d–%d ms)\n",
                  RSSI_STREAM_INTERVAL_MS_MIN,
                  RSSI_STREAM_INTERVAL_MS_MAX);
    Serial.println("            ADC sampled every loop pass; max held until print");
    Serial.println("            Plot time_ms vs adc_peak_raw_0_4095 / adc_mean_raw_0_4095");
    Serial.println("            Sweep external sig gen (coarse 10–25 MHz, fine ±10 MHz @ 5800)");
    Serial.println();
    Serial.println("  l         Read Lock Detect pin");
    Serial.println("  p         Toggle synth on/off (R2 SHDN — software shutdown)");
    Serial.println("  s         Show status, channel table, registers");
    Serial.println("  h         Show this help");
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println();
    Serial.println("RACEBAND → LO MAPPING:");
    for (int i = 0; i < NUM_CHANNELS; i++) {
        Serial.printf("  R%d: %d MHz  →  LO %d MHz\n",
                      i + 1, RACEBAND_CHANNELS[i],
                      RACEBAND_CHANNELS[i] - IF_OFFSET_MHZ);
    }
    Serial.println();
    Serial.println("F-BAND → LO MAPPING:");
    for (int i = 0; i < NUM_F_CHANNELS; i++) {
        Serial.printf("  F%d: %d MHz  →  LO %d MHz\n",
                      i + 1, FBAND_CHANNELS[i],
                      FBAND_CHANNELS[i] - IF_OFFSET_MHZ);
    }
    Serial.println();
    Serial.printf("  IF_OFFSET_MHZ = %d  (set in source)\n", IF_OFFSET_MHZ);
    Serial.printf("  REF_FREQ_MHZ  = %d  (set in source)\n", REF_FREQ_MHZ);
    Serial.println("═══════════════════════════════════════════════════");
}
