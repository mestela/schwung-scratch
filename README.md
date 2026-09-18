# Schwung Scratch

Experimental digital-vinyl sound generator for Ableton Move and Schwung.

The module decodes Serato 2nd Edition side A control vinyl from Move's stereo
line input and uses the decoded direction, speed, and optional absolute
position to play a digital sample. A class-compliant USB MIDI fader can control
the output cut.

## Current MVP

Choose **Sample** in the module parameter page to open Schwung's built-in
sample browser. The current decoder accepts 44.1 kHz, 16-bit, stereo PCM WAV
files. The selected absolute path is stored with the chain state and reloaded
by a background worker. New instances start with the bundled **Ahh Fresh**
sample, created and contributed by Matt Estela; choosing another sample or
restoring a saved chain overrides it.

Connect stereo line-level timecode audio to Move's 3.5 mm input and the MIDI
device to Move's USB-A host port. The default fader mapping is CC 1 on any MIDI
channel; channel 16 in the module UI means omni. For the Headache Sound OMNI,
set **Learn CC** to **Armed**, then sweep the selected main crossfader once. The
first incoming CC is stored with its MIDI channel. Use the OMNI's FADER switch
to choose the physical left or right fader, and use either the OMNI REV switch
or the module's Hamster option—not both.

Without a turntable, **Knob Scratch** provides a simple bounded test control.
For a more playable version, select **Jog Scratch** and press the jog wheel to
open its full-screen mode. Turning the jog wheel then moves the sample instead
of navigating the UI; pads remain momentary cut switches. Press the jog wheel
or Back to leave the mode, and adjust **Jog Feel** to change its response. The
virtual platter resumes forward playback after release; **Motor** starts or
stops that playback and **Motor Speed** sets its forward rate.

Run host tests with `./tests/run.sh`. Build the ARM64 module with
`./scripts/build.sh`.

## License

GPL-3.0-or-later. The timecode decoder is derived from xwax by Mark Hills and
retains its upstream copyright notices.
