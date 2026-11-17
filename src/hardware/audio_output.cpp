#include "audio_output.h"
#include "config/config_globals.h"

#if ENABLE_AUDIO

#if defined(BOARD_JC2432W328C)
// SimpleTTS support - includes for future implementation
// #include "SimpleTTS.h"
// #include "AudioTools.h"
// TODO: Implement custom AudioOutput class for DAC26 once audio files are ready
#endif

AudioOutput::AudioOutput() {
    // Constructor
}

void AudioOutput::begin() {
    // Set up GPIO26 as DAC output
    hal_gpio_output(AUDIO_DAC_PIN);
    hal_dac_write(AUDIO_DAC_PIN, 0);  // Start silent
}

void AudioOutput::playLapBeep() {
    // Generate a 1kHz tone for BEEP_DURATION_MS
    const int frequency = 1000;  // 1kHz
    const int samples = (frequency * BEEP_DURATION_MS) / 1000;
    const float period = 1000000.0 / frequency;  // Period in microseconds

    unsigned long start = hal_micros();
    for (int i = 0; i < samples; i++) {
        // Generate square wave (simple but effective)
        int phase = (int)((hal_micros() - start) / (period / 2)) % 2;
        hal_dac_write(AUDIO_DAC_PIN, phase ? 200 : 55);  // DAC range 0-255, keep volume moderate
        hal_delay_us(period / 2);
    }

    hal_dac_write(AUDIO_DAC_PIN, 0);  // Silence
}

void AudioOutput::speakLapAnnouncement(uint16_t lapNumber, uint32_t lapTimeMs) {
    // Calculate all time units from lap time
    uint32_t totalSeconds = lapTimeMs / 1000;
    uint32_t days = totalSeconds / 86400;
    uint32_t hours = (totalSeconds % 86400) / 3600;
    uint32_t minutes = (totalSeconds % 3600) / 60;
    uint32_t seconds = totalSeconds % 60;

    // Determine which units to announce - skip leading zeros
    // Only announce from the first non-zero unit onwards
    bool announceDays = (days > 0);
    bool announceHours = (hours > 0);
    bool announceMinutes = (minutes > 0);
    bool announceSeconds = true;  // Always announce seconds

    // If we're announcing a larger unit, we should also announce smaller units even if they're 0
    // (e.g., "3 minutes 0 seconds" is valid, but "0 hours 3 minutes" should be "3 minutes")
    if (announceDays) {
        // If days > 0, announce all subsequent units
        announceHours = true;
        announceMinutes = true;
    } else if (announceHours) {
        // If hours > 0 (but days = 0), announce hours, minutes, seconds
        announceMinutes = true;
    }
    // If only minutes > 0, announce minutes and seconds
    // If only seconds > 0, announce only seconds

    #if defined(BOARD_JC2432W328C)
    // TODO: Implement SimpleTTS with pre-recorded word fragments
    // For now, use simple beep until audio files are generated
    // Audio files needed: "lap", "1"-"20", "0"-"59", "seconds", "minutes", "hours", "days"
    // Format: Only announce lap number and time (no faster/slower comparisons)
    // Examples:
    //   - "Lap 1, 45 seconds" (if < 1 minute, skip minutes/hours/days)
    //   - "Lap 2, 3 minutes, 5 seconds" (if >= 1 minute, skip hours/days if 0)
    //   - "Lap 2, 3 minutes, 0 seconds" (if minutes > 0 and seconds = 0, still announce seconds)
    //   - "Lap 3, 2 hours, 15 minutes, 30 seconds" (if >= 1 hour, skip days if 0)
    //   - "Lap 4, 1 day, 2 hours, 3 minutes, 5 seconds" (if >= 1 day)
    // DO NOT include: "faster lap", "slower lap", "personal best", etc.
    // DO NOT announce leading zeros: "0 hours" should never be spoken (unless days > 0)

    if (announceDays) {
        // TODO: Play "lap", lap number, days, "days"
        // TODO: Play hours, "hours" (even if 0, since days > 0)
        // TODO: Play minutes, "minutes" (even if 0, since days > 0)
        // TODO: Play seconds, "seconds"
    } else if (announceHours) {
        // TODO: Play "lap", lap number, hours, "hours"
        // TODO: Play minutes, "minutes" (even if 0, since hours > 0)
        // TODO: Play seconds, "seconds"
    } else if (announceMinutes) {
        // TODO: Play "lap", lap number, minutes, "minutes"
        // TODO: Play seconds, "seconds" (even if 0, since minutes > 0)
    } else {
        // Only seconds (less than 1 minute)
        // TODO: Play "lap", lap number, seconds, "seconds"
    }
    playLapBeep();
    #else
    // Fallback to beep for boards without TTS
    playLapBeep();
    #endif
}

#endif // ENABLE_AUDIO
