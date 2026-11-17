# StarForgeOS Test Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        StarForgeOS Project                               │
│                                                                          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐           │
│  │   main.cpp     │  │  timing_core   │  │ standalone_mode│           │
│  │                │  │  - RSSI read   │  │  - WiFi AP     │           │
│  │  - Setup       │  │  - Lap detect  │  │  - Web server  │           │
│  │  - Wi-Fi start │  │  - RX5808      │  │  - mDNS        │           │
│  └────────────────┘  └────────────────┘  └────────────────┘           │
│                                                                          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐           │
│  │   lcd_ui       │  │    config.h    │  │                │           │
│  │  - Display     │  │  - Pin defs    │  │                │           │
│  │  - Touch       │  │  - Board cfg   │  │                │           │
│  │  - Battery     │  │  - Constants   │  │                │           │
│  └────────────────┘  └────────────────┘  └────────────────┘           │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ Tests validate
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          Test Suite                                      │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ test/platformio.ini - Test Environment Configuration             │  │
│  │  - 7 board environments (test-esp32-c3, test-esp32-c6, ...)     │  │
│  │  - Unity test framework                                          │  │
│  │  - Board-specific build flags                                    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐        │
│  │  test_hardware  │  │  test_timing    │  │   test_wifi     │        │
│  │                 │  │                 │  │                 │        │
│  │  • Pin config   │  │  • RSSI reading │  │  • AP init      │        │
│  │  • ADC setup    │  │  • Lap detect   │  │  • IP config    │        │
│  │  • SPI pins     │  │  • Frequency    │  │  • SSID gen     │        │
│  │  • Power button │  │  • Threshold    │  │  • Stability    │        │
│  │  • UART config  │  │  • Thread safe  │  │  • AP/STA/off   │        │
│  │                 │  │                 │  │                 │        │
│  │  15+ tests      │  │  18+ tests      │  │  15+ tests      │        │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘        │
│                                                                          │
│  ┌─────────────────┐                                                    │
│  │   test_lcd      │                                                    │
│  │                 │                                                    │
│  │  • I2C bus      │                                                    │
│  │  • Touch ctrl   │                                                    │
│  │  • Backlight    │                                                    │
│  │  • Battery      │                                                    │
│  │  • Audio        │                                                    │
│  │                 │                                                    │
│  │  13+ tests      │                                                    │
│  └─────────────────┘                                                    │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ Runs on
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      7 ESP32 Board Types                                 │
│                                                                          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐       │
│  │  ESP32-C3  │  │  ESP32-C6  │  │   ESP32    │  │  ESP32-S2  │       │
│  │            │  │            │  │            │  │            │       │
│  │ Single-    │  │ WiFi 6     │  │ Dual-core  │  │ Single-    │       │
│  │ core RISC  │  │ RISC-V     │  │ Xtensa     │  │ core       │       │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘       │
│                                                                          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐                        │
│  │  ESP32-S3  │  │ S3-Touch   │  │JC2432W328C │                        │
│  │            │  │            │  │            │                        │
│  │ Dual-core  │  │ Waveshare  │  │ ESP32 LCD  │                        │
│  │ + PSRAM    │  │ LCD board  │  │ board      │                        │
│  └────────────┘  └────────────┘  └────────────┘                        │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ Results feed into
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         CI/CD Pipeline                                   │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ .github/workflows/multi-board-tests.yml                          │  │
│  │                                                                   │  │
│  │  Triggers:                                                        │  │
│  │  • Push to main/develop                                          │  │
│  │  • Pull requests                                                  │  │
│  │  • Manual dispatch                                               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────┐    │
│  │ Jobs (Parallel Execution)                                       │    │
│  │                                                                  │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐│    │
│  │  │ ESP32-C3 │ │ ESP32-C6 │ │  ESP32   │ │ ESP32-S2 │ │ ...  ││    │
│  │  │          │ │          │ │          │ │          │ │      ││    │
│  │  │ • Build  │ │ • Build  │ │ • Build  │ │ • Build  │ │ More ││    │
│  │  │ • Test   │ │ • Test   │ │ • Test   │ │ • Test   │ │      ││    │
│  │  │ • Upload │ │ • Upload │ │ • Upload │ │ • Upload │ │      ││    │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘│    │
│  └────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Outputs                                                           │  │
│  │  ✅ Build status (pass/fail)                                     │  │
│  │  📦 Firmware binaries (.bin files)                               │  │
│  │  📊 Test summary report                                          │  │
│  │  💾 Artifacts (30-day retention)                                 │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ Provides
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       Developer Tools                                    │
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐     │
│  │ run_tests.sh     │  │ run_tests.bat    │  │ Documentation    │     │
│  │                  │  │                  │  │                  │     │
│  │ • Build-only     │  │ • Windows        │  │ • README.md      │     │
│  │ • Hardware       │  │   compatible     │  │ • QUICKSTART.md  │     │
│  │ • Specific board │  │ • Same features  │  │ • EXAMPLES.md    │     │
│  │ • Color output   │  │                  │  │ • CHECKLIST.md   │     │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘     │
└─────────────────────────────────────────────────────────────────────────┘
```

## Test Flow Diagram

```
┌─────────────┐
│ Developer   │
│ Changes     │
│ Code        │
└──────┬──────┘
       │
       ├─────────────────────┐
       │                     │
       ▼                     ▼
┌─────────────┐      ┌─────────────┐
│ Local Tests │      │ Git Push    │
│             │      │             │
│ • Quick     │      │ • Commit    │
│ • Build-only│      │ • Push PR   │
│ • Specific  │      │             │
└──────┬──────┘      └──────┬──────┘
       │                     │
       │ Pass                │ Triggers
       ▼                     ▼
┌─────────────┐      ┌─────────────┐
│ Commit      │      │ GitHub      │
│ Changes     │      │ Actions     │
└─────────────┘      │             │
                     │ • All boards│
                     │ • Parallel  │
                     │ • Artifacts │
                     └──────┬──────┘
                            │
                            ├─── Pass ──────┐
                            │                │
                            ▼                ▼
                     ┌─────────────┐  ┌─────────────┐
                     │ ✅ Green    │  │ Download    │
                     │ Checkmark   │  │ Firmware    │
                     │             │  │ Artifacts   │
                     │ Safe to     │  │             │
                     │ Merge       │  │ Flash &     │
                     └─────────────┘  │ Test        │
                                      └─────────────┘
```

## Board Coverage Matrix

```
┌────────────────┬─────────┬─────────┬─────────┬─────────┐
│ Board Type     │ HW Test │ Timing  │ WiFi    │ LCD     │
├────────────────┼─────────┼─────────┼─────────┼─────────┤
│ ESP32-C3       │    ✅   │    ✅   │    ✅   │    ❌   │
│ ESP32-C6       │    ✅   │    ✅   │    ✅   │    ❌   │
│ ESP32          │    ✅   │    ✅   │    ✅   │    ❌   │
│ ESP32-S2       │    ✅   │    ✅   │    ✅   │    ❌   │
│ ESP32-S3       │    ✅   │    ✅   │    ✅   │    ❌   │
│ ESP32-S3-Touch │    ✅   │    ✅   │    ✅   │    ✅   │
│ JC2432W328C    │    ✅   │    ✅   │    ✅   │    ✅   │
└────────────────┴─────────┴─────────┴─────────┴─────────┘
```

## Test Execution Timeline

```
┌─────────────────────────────────────────────────────────────┐
│ Build-Only Tests (No Hardware)          ~5-10 minutes       │
│                                                              │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     │
│ │ C3   │ │ C6   │ │ ESP32│ │ S2   │ │ S3   │ │ LCD  │     │
│ │ 1min │ │ 1min │ │ 1min │ │ 1min │ │ 1min │ │ 2min │     │
│ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘     │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Hardware Tests (Board Connected)         ~2-3 min per board │
│                                                              │
│ ┌────────────┐                                              │
│ │ Your Board │                                              │
│ │  • Upload  │  30s                                         │
│ │  • Test    │  90s                                         │
│ │  • Report  │  10s                                         │
│ └────────────┘                                              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ CI Pipeline (All Boards Parallel)       ~3-5 minutes        │
│                                                              │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     │
│ │ C3   │ │ C6   │ │ ESP32│ │ S2   │ │ S3   │ │ LCD  │     │
│ │      │ │      │ │      │ │      │ │      │ │      │     │
│ │ All  │ │ Run  │ │ In   │ │ Para││ │ llel │ │      │     │
│ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘     │
│                                                              │
│ ┌────────────────────────────────────┐                      │
│ │ Generate Summary & Artifacts       │                      │
│ └────────────────────────────────────┘                      │
└─────────────────────────────────────────────────────────────┘
```

## Component Relationships

```
┌────────────────────────────────────────────────────────────┐
│                      config.h                              │
│              (Board-specific configuration)                 │
│                                                            │
│  • Pin definitions for each board                          │
│  • Feature flags (LCD, Battery, Audio)                     │
│  • Task priorities (single vs dual core)                   │
│  • DMA ADC configuration                                   │
└──────────────┬─────────────────────────────────────────────┘
               │ Included by
               │
    ┌──────────┼──────────┬──────────┬──────────┐
    │          │          │          │          │
    ▼          ▼          ▼          ▼          ▼
┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│ main   │ │ timing │ │ stand- │ │  lcd   │ │  node  │
│        │ │  core  │ │ alone  │ │   ui   │ │  mode  │
└────────┘ └────────┘ └────────┘ └────────┘ └────────┘
    │          │          │          │          │
    └──────────┴──────────┴──────────┴──────────┘
               │ Tested by
               ▼
         ┌────────────┐
         │ Test Suite │
         └────────────┘
```

## Success Indicators

```
┌─────────────────────────────────────────────────────────────┐
│ All Green! ✅                                               │
│                                                             │
│ ✅ All 7 boards compile                                    │
│ ✅ All 60+ test cases pass                                 │
│ ✅ No linter errors                                        │
│ ✅ CI pipeline successful                                  │
│ ✅ Firmware artifacts generated                            │
│ ✅ Documentation up to date                                │
│                                                             │
│                    Safe to Deploy! 🚀                       │
└─────────────────────────────────────────────────────────────┘
```

This architecture ensures that every code change is validated across all supported hardware platforms before deployment!

