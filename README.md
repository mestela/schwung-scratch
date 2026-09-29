# Schwung Scratch

Digital vinyl and hands-on sample scratching for Ableton Move and Schwung.
Scratch decodes Serato 2nd Edition side A control vinyl from Move's stereo line
input. It can also use Move's first encoder or jog wheel when no turntable is
available. Pads act as momentary crossfader cuts.

## Requirements

- Ableton Move running Schwung 1.5.0 or later.
- A 44.1 kHz, 16-bit, stereo PCM WAV sample.
- For DVS: Serato 2nd Edition side A control vinyl and a stereo line-level
  turntable output connected to Move's 3.5 mm input.

Convert an MP3 with:

```sh
ffmpeg -i input.mp3 -ar 44100 -ac 2 -c:a pcm_s16le output.wav
```

## Main page

| Knob | Control |
|---|---|
| 1 | Sample browser |
| 2 | DVS, Knob, or Jog control |
| 3 | Open Scratch View |
| 4 | Hamster mode |
| 5 | Loop |
| 6 | Low-cut filter |
| 7 | Fader cut-in threshold |
| 8 | Pad retrigger gap |

The second page is DVS Monitor, showing input level, decoder lock, direction,
speed, signal quality, and the timecode scope.

## Scratch View

Knob and Jog modes expose Feel, Smooth, Inertia, Touch, playback Speed, waveform
Zoom, and Motor Play/Stop. In Knob mode, touching Knob 1 stops the virtual
record, turning scratches it, and releasing resumes the motor. Jog mode uses the
jog wheel. Touch behavior can be disabled, currently detecting a touch is instantaneous, but detecting a release lags enough to annoying.

DVS mode follows the decoded vinyl position and speed. Knob 6 scales playback
speed and Knob 7 changes waveform zoom. Press any pad to open the cut; overlapping
pad presses retrigger it for crab and transformer techniques. Press the jog wheel
or Back to leave Scratch View.

Sample selection and all controls persist with the chain. New instances use the
bundled `ahh-fresh.wav`.

## Build and test

```sh
./tests/run.sh
./scripts/build.sh
```

The release archive is written to `dist/scratch-module.tar.gz`.

## License

GPL-3.0-or-later. The timecode decoder is derived from xwax by Mark Hills and
retains its upstream notices. See `THIRD_PARTY_LICENSES.md` and
`SAMPLE_LICENSE.md`.
