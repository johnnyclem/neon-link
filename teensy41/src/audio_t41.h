#pragma once

#include <cstdint>

// Audio out (docs/AUDIOLINK.md, Teensy leg): the portable audio engine —
// SampleClock, metronome ClickSynth, PulseRender pulse-as-audio taps,
// and the mixer routing matrix — rendered in the Teensy Audio Library's
// update ISR and played through an SGTL5000 audio shield (or any I2S
// DAC on the standard pins). AudioInputI2S feeds the onset detector so
// AUDIO > FOLLOW can run with the engine muted. Link Audio streaming is
// not ported yet; kLinkIn/kLineIn roles render silent.
//
// State crosses into the ISR through an interrupt-masked staging copy,
// not the seqlock buses: on a single core, an ISR spinning on a
// mid-publish seqlock would never see the writer finish. push_state()
// is called from the main loop; the ISR copies staging at block start.
namespace audioeng {

void init();
void poll(int64_t now_us);  // stage bus state + publish meters/counters

}  // namespace audioeng
