/*
 * MAX2871+ Test - Minimal Synthesizer Diagnostic
 *
 * Tests MAX2871/MAX2871+ synthesizer with a downstream mixer.
 * The chip is programmed to LO = channel - IF_OFFSET_MHZ so that the
 * mixer output lands at a fixed IF (default 200 MHz).
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
 *   s         Show status + current register values
 *   h         Show help
 *   t <ms>    Hop / fast re-lock timeout (auto-cycle, benchmark); default 150
 *   i <ms>    Initial / conservative timeout (boot, manual f/c); default 3000
 *   b [n]     Benchmark re-lock (see m); default 10 hops
 *   m <0|1>   Lock timing: 0=LOW→HIGH (strict), 1=setLO end→HIGH (no unlock required)
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

// ─── Configuration ──────────────────────────────────────────────────────────
// REF_IN frequency in MHz (into MAX2871). With R=1 in R2, f_PFD equals this.
// Frac-N: MOD = REF_FREQ_MHZ → step = f_PFD/MOD = 1 MHz (any integer REF MHz).
#define REF_FREQ_MHZ        50    // This hardware: 50 MHz reference, R divider 1

// Mixer IF offset in MHz.
// MAX2871 is programmed to LO = channel - IF_OFFSET_MHZ (low-side injection).
// Mixer output: IF = RF - LO = channel - (channel - offset) = IF_OFFSET_MHZ.
// Flip sign or swap formula here if your mixer uses high-side injection.
#define IF_OFFSET_MHZ       200

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

// ─── MAX2871 Default Register Bank ──────────────────────────────────────────
// Register address is embedded in bits[2:0] of each 32-bit word.
// Write order is ALWAYS R5 → R4 → R3 → R2 → R1 → R0 (per datasheet).
//
//   R5 0x00400005  LD[23:22]=01  digital lock detect, HIGH when locked
//   R4 0x6180B23C  DIVA[22:20]=0 ÷1 (VCO direct, 3–6 GHz range)
//                  FB[23]=1      fundamental VCO feedback to N divider
//                  RFA_EN[5]=1   RF output A enabled
//                  APWR[4:3]=11  +5 dBm output power
//   R3 0x0000000B  CDM[16:15]=0  clock divider off; VAS autocalibration on
//   R2 0x00004042  R[23:14]=1    reference divider = 1 (no division)
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
    0x00004042,  // R2 — R=1, LDF=frac-N, PDP=positive
    0x0000000B,  // R3 — CDM off, VAS on
    0x6180B23C,  // R4 — DIVA=0, RFA enabled, +5 dBm
    0x00400005,  // R5 — LD = digital lock detect
};

// ─── State ──────────────────────────────────────────────────────────────────
uint32_t current_channel = RACEBAND_CHANNELS[0]; // actual RF channel freq (MHz)
uint32_t current_lo      = 0;                     // what MAX2871 is tuned to (MHz)
bool     auto_cycle_enabled = true;
bool     on_cycle_a         = true;   // if true, next auto hop → CYCLE_CHAN_B; else → CYCLE_CHAN_A
uint32_t last_cycle_time    = 0;

// micros() timestamp after last successful setLO() register burst (retune timing)
static uint32_t g_last_lo_program_us = 0;

// ─── Prototypes ─────────────────────────────────────────────────────────────
void    spiWriteWord(uint32_t word);
bool    checkChip();
void    runHealthCheck();
void    initSynth();
void    setChannel(uint32_t channel_mhz);
void    setLO(uint32_t lo_mhz);
bool    waitForLock(uint32_t timeout_ms, uint32_t *out_lock_ms);
bool    waitForLockAfterRetune(uint32_t timeout_ms, uint32_t *out_relock_us);
bool    waitForLockProgramEdgeToHigh(uint32_t timeout_ms, uint32_t *out_us);
bool    waitForLockTimed(uint32_t timeout_ms, uint32_t *out_us);
void    runLockBenchmark(uint16_t num_hops);
bool    readLD();
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

    analogReadResolution(12);
    analogSetPinAttenuation(RSSI_ADC_PIN, ADC_11db);  // ~0–3.3 V full scale

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

    if (auto_cycle_enabled && (millis() - last_cycle_time >= CYCLE_INTERVAL_MS)) {
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

    // Periodic status line
    static uint32_t last_print = 0;
    if (millis() - last_print >= 1000) {
        int rssi_mv = readRssiMilliVolts();
        Serial.printf("[LD: %s] ch=%lu MHz  LO=%lu MHz | AUTO-CYCLE: %s | RSSI: %.2f V\n",
                      readLD() ? "LOCKED  " : "UNLOCKED",
                      current_channel, current_lo,
                      auto_cycle_enabled ? "ON" : "OFF",
                      rssi_mv / 1000.0f);
        last_print = millis();
    }

    delay(10);
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
    for (int r = 5; r >= 0; r--) {
        spiWriteWord(regs[r]);
        delay(5);
    }
    Serial.println("  Default registers written (R5→R0)");
}

// ─── Channel → LO Mapping ────────────────────────────────────────────────────
// Takes the actual RF channel frequency, subtracts IF_OFFSET_MHZ, and programs
// the MAX2871. Mixer output: IF = RF_in - LO = IF_OFFSET_MHZ.
void setChannel(uint32_t channel_mhz) {
    if (channel_mhz <= (uint32_t)IF_OFFSET_MHZ) {
        Serial.printf("ERROR: channel %lu MHz is <= IF offset (%d MHz)\n",
                      channel_mhz, IF_OFFSET_MHZ);
        return;
    }
    uint32_t lo = channel_mhz - IF_OFFSET_MHZ;
    Serial.printf("  channel=%lu MHz  LO=%lu MHz  IF=%d MHz\n",
                  channel_mhz, lo, IF_OFFSET_MHZ);
    current_channel = channel_mhz;
    setLO(lo);
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
void setLO(uint32_t lo_mhz) {
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

    Serial.printf("  → LO=%lu MHz: VCO=%lu, N=%lu, FRAC=%u, MOD=%u, DIVA=%u (%s-N)\n",
                  lo_mhz, vco, N, FRAC, MOD, diva, int_mode ? "integer" : "frac");

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
    current_lo = lo_mhz;
}

// ─── Lock Detect ─────────────────────────────────────────────────────────────
bool readLD() {
    return digitalRead(MAX2871_LD_PIN) == HIGH;
}

int readRssiMilliVolts() {
    return analogReadMilliVolts(RSSI_ADC_PIN);
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

// ─── Commands ────────────────────────────────────────────────────────────────
void processCommand(String cmd) {
    cmd.trim();

    // Lowercase only the command letter, not the value
    String cmdLower = cmd;
    cmdLower.toLowerCase();

    if (cmdLower.startsWith("f ")) {
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
    else if (cmdLower == "b" || cmdLower.startsWith("b ")) {
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
    Serial.printf("  Auto-cycle: %s\n",
                  auto_cycle_enabled
                      ? "ENABLED  (R" + String(CYCLE_CHAN_A+1) + " ↔ R" + String(CYCLE_CHAN_B+1) + ")"
                      : "DISABLED");
    Serial.printf("  Reference:  %d MHz  (1 MHz steps via frac-N)\n", REF_FREQ_MHZ);
    Serial.printf("  Lock: initial timeout %lu ms  |  hop timeout %lu ms\n",
                  (unsigned long)g_ld_timeout_initial_ms,
                  (unsigned long)g_ld_timeout_hop_ms);
    Serial.printf("  Lock timing:  %s\n",
                  g_lock_timing_mode == LOCK_TIMING_PROGRAM_EDGE
                      ? "setLO end → LD HIGH (m 1)"
                      : "strict LD LOW→HIGH (m 0)");
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
    Serial.printf("    RSSI: GPIO %d (ADC)  %.2f V\n",
                  RSSI_ADC_PIN, readRssiMilliVolts() / 1000.0f);
    Serial.println("═══════════════════════════════════════════════════");
}

// ─── Help ────────────────────────────────────────────────────────────────────
void showHelp() {
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("COMMANDS:");
    Serial.println("═══════════════════════════════════════════════════");
    Serial.println("  f <freq>  Set channel frequency in MHz");
    Serial.println("            LO offset applied automatically");
    Serial.println("            Example: f 5658  →  LO = 5458 MHz");
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
    Serial.println("  b [n]     Benchmark n hops — min/avg/max µs (follows m)");
    Serial.println();
    Serial.println("  l         Read Lock Detect pin");
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
    Serial.printf("  IF_OFFSET_MHZ = %d  (set in source)\n", IF_OFFSET_MHZ);
    Serial.printf("  REF_FREQ_MHZ  = %d  (set in source)\n", REF_FREQ_MHZ);
    Serial.println("═══════════════════════════════════════════════════");
}
