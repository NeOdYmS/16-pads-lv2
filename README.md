# 16-pads-lv2

An initial 16-pad WAV sampler for Linux aarch64 / MODEP. The DSP is portable C11;
the optional MPC-inspired interface is a MOD HTML/CSS/JavaScript skin, **not a
native LV2 UI binary**. The default bundle uses the host's generic controls.

## Agreed behavior

- **Global polyphony, mono per pad:** all 16 pads can play together. Triggering
  an already-playing pad stops it immediately, without restarting it. Triggering
  an idle pad starts its sample from the beginning.
- **One-shot:** play once, ignoring trigger release.
- **Loop:** repeat until the next trigger on that pad stops playback. Trigger
  release does not stop a loop. There is no tempo synchronization.
- **Trig:** play once while a trigger is held; release stops playback. Holding
  beyond the sample's end does not loop it.
- **Gain:** -60 to +12 dB per pad, default 0 dB, visible **only in MODEP's
  configuration page**, not in the pad view. -60 dB is attenuation, not mute.
- **MIDI mapping and sample assignments:** configured in MODEP's settings page,
  not in the graphical pad view. No final MIDI note or CC assignments are built in.

The pad view shows 16 playable pad buttons, mode selectors, and assigned sample
names. Loop pads blink until the next trigger stops them; Trig pads blink until
all held trigger sources are released (even if the sample has reached its end).
Feedback comes from monitored LV2 output controls, so raw MIDI and MODEP-mapped
triggers update the same indicators. One-shot playback is indicated without
blinking. Reduced-motion browser preferences disable the animation.

## Build and tests

On a Debian-family **development machine**, install the build dependencies:

```sh
sudo apt-get install build-essential pkg-config python3 lv2-dev
make
make test
```

The bundle is `build/16-pads.lv2`. C tests cover the WAV decoder, all playback
modes, stop-on-retrigger, resampling, stereo output, and gain boundaries. A small
LV2 host harness covers all sixteen simultaneous pads, MIDI timestamps, MODEP
gate controls, asynchronous and synchronous workers, failed/superseded loads,
state save/restore, and malformed/undersized atom buffers. Python tests check
the generated port and sample-property metadata.

`make test-core` runs the core and metadata tests without LV2 headers. For
address/undefined-behavior checks with a compiler that supports them:

```sh
make -B test CFLAGS="-O1 -g -fsanitize=address,undefined"
```

`make test-ui` optionally uses Node.js (no npm packages) to exercise the MOD
callback, all 16 pads, press/release controls, blinking feedback, sample labels,
and errors. These headless tests supplement the browser simulator, not MODEP
integration. Address-sanitized executables stalled on the local macOS host and
were interrupted; no successful local ASan result is claimed. Native tests
without ASan pass, as does the local core test with UBSan only; the Linux CI
job also attempts combined sanitizer tests.

On macOS, `brew install lv2` supplies headers. If the active `pkg-config` does
not search Homebrew, set `PKG_CONFIG_PATH` to Homebrew's `lib/pkgconfig` and
`share/pkgconfig` directories. A macOS build is **Mach-O**, even on an ARM Mac,
and cannot be installed into MODEP on Linux.

### Linux aarch64

Build natively on a Linux aarch64 development machine with the same commands,
or use a Linux cross-toolchain on a development machine:

```sh
make BUNDLE=build/aarch64/16-pads.lv2 CC=aarch64-linux-gnu-gcc
file build/aarch64/16-pads.lv2/sampler.so
```

LV2 is header-only; `pkg-config --cflags lv2` must point to available LV2 1.18+
headers. The cross-compiler also needs its Linux libc/libm sysroot. An alternative
tested cross-build uses Zig:

```sh
make BUNDLE=build/aarch64/16-pads.lv2 \
  CC="zig cc -target aarch64-linux-gnu.2.28"
```

Keep native and cross-built bundles in separate directories. Use the complete
Linux aarch64 `.lv2` directory, not just the shared library. `make install`
defaults to `~/.lv2`; override `LV2DIR` to the **actual host's configured plugin
search path**. To stage a cross-built bundle without touching a target system:

```sh
make install BUNDLE=build/aarch64/16-pads.lv2 \
  CC="zig cc -target aarch64-linux-gnu.2.28" \
  DESTDIR="$PWD/build/stage" LV2DIR=/usr/local/lib/lv2
```

These are instructions only: this project does not assume access to a Pi,
its files, its plugin directories, or its running MODEP installation.

## MODEP configuration

1. Connect the stereo outputs. In MODEP settings, assign a sample to each
   `Pad NN sample` file property. Upload/manage WAVs through whatever file
   manager your MODEP version provides; the plugin never reads browser-local
   files or uploads files itself.
2. Assign each `Pad NN Trigger` control using MODEP's MIDI mapping. Use a
   **momentary** assignment that sends both pressed and released values for
   Trig, rather than a latched/toggle assignment. A rising control edge starts
   an idle pad or stops a playing one; a falling edge only stops Trig.
3. Set each pad's mode and gain. Gains are reported to MODEP in dB.

The `trigger01` ... `trigger16` ports are the normal host-mapping workflow;
the mapping itself is stored by MODEP. Control-port gates are observed at
audio-block boundaries. Which MIDI sources (notes versus CCs) MODEP can learn
depends on its version.

For hosts/controllers that route raw MIDI to the plugin, optional `noteNN` and
`ccNN` settings provide direct assignments in the same generic settings panel.
Both default to **-1 (disabled)**. Raw MIDI is omni-channel and velocity-sensitive
for notes; velocity-zero note-on is note-off. A mapped CC rising across 64
triggers at full velocity; falling below 64 releases. Repeated high CC values
do not trigger repeatedly. MIDI CC 120/123 are reserved as global all-sound-off.
Multiple pads can share an assignment. Reassigning a held note/CC clears its
held state so Trig cannot get stuck.

### Sample file properties and compatibility

There are sixteen independent writable/readable LV2 Patch properties:

```text
https://github.com/NeOdYmS/16-pads-lv2#sampler:sample01
...
https://github.com/NeOdYmS/16-pads-lv2#sampler:sample16
```

They use `atom:Path` and MOD's `mod:fileTypes "audiosample,audioloop"` hint.
Modern MOD-derived hosts can expose file selectors for these parameters in
their settings. **The exact MODEP installation has not been inspected; file
picker support is not guaranteed.** The hint can list formats the host knows
but this initial decoder accepts **WAV only**. Unsupported selections fail
explicitly rather than replacing a working sample.

If the host lacks file-property support, the generic mode/gain/trigger controls
still work, but do not by themselves provide a file browser. Use a host that
can send `patch:Set` with `patch:property` equal to a sample URI and
`patch:value` an `atom:Path`, or restore an LV2 state containing those paths.
Do not expect a GUI library to add this missing host capability.

An empty path clears a pad. Paths are limited to 1023 bytes plus NUL and refer
to files accessible by the **audio host**. `patch:Get` returns effective paths;
successful loads emit `patch:Set` path notifications. Failures retain the
previous sample and emit a readable `:error` string (also host-logged when
the LV2 Log feature is available).

## Optional MOD web skin

```sh
make ENABLE_MOD_GUI=1
# For a Linux cross-bundle, also pass the BUNDLE and CC options above.
```

The skin uses MOD's documented callback/control API. MIDI mappings, sample
selection, and gains stay in the host's settings. A regular GTK/Qt/X11 LV2 UI
would not run inside MODEP's web frontend; a browser skin needs no separate
aarch64 GUI binary.

The skin has been exercised in a **local callback simulator**, not in MODEP.
It remains opt-in until tested against the target MOD frontend. It does not
include MOD catalogue screenshot/thumbnail assets. Disable it with
`make ENABLE_MOD_GUI=0` if the host cannot render it; all controls and sample
properties remain available. Depending on the host, a rescan/reload may be
needed after changing bundle metadata.

To inspect the layout without an audio host:

```sh
python3 -m http.server 8765 --bind 127.0.0.1
# Open http://127.0.0.1:8765/tools/preview.html
```

This preview simulates callbacks only: it produces no audio and proves neither
MODEP integration nor target-device performance.

## Implementation limits

Samples are decoded in an LV2 Worker, not in `run()`. Old sample buffers are
also released through the worker; stale requests cannot overwrite newer
selections. Invalid loads retain the old sample. A successful replacement stops
that pad. Instantiation requires URID Map and Worker Schedule.

Supported files: little-endian RIFF WAV, mono/stereo, PCM 16/24/32-bit or
IEEE float32; no compressed audio or WAVE_FORMAT_EXTENSIBLE. Non-finite float
samples are rejected; float samples outside [-1, 1] are normalized by clamping.
Each decoded sample is capped at 64 MiB. Sixteen maximum-size samples can use
1 GiB, with additional transient memory during replacement/state restoration;
use short samples appropriate to the host's available RAM.

Sample rates are adapted using linear interpolation. Outputs are summed
without a limiter: simultaneous pads and positive gain require output headroom.
There are no envelopes, fades, time-stretching, or disk streaming; abrupt stops
can click. Pi CPU load, underruns, and latency have **not** been measured.

Sample paths are saved through LV2 State and `state:mapPath` when provided.
Without path mapping, saved paths remain host-local/non-portable. Audio bytes
are not embedded in state; relocating/copying referenced files is the host's
responsibility. Control values and MIDI mappings are saved by the host.
Restore is transactional on load failure and must happen with audio suspended
(the plugin does not advertise `state:threadSafeRestore`).

The implementation has passed native tests and generated a Linux aarch64 ELF
shared library. That confirms build/ABI feasibility, **not execution on the
user's Pi or compatibility with an uninspected MODEP release**.

## Conventions consulted

- [LV2 programming examples](https://lv2plug.in/book/) and the
  [Worker](https://lv2plug.in/ns/ext/worker) /
  [State](https://lv2plug.in/ns/ext/state) /
  [Patch](https://lv2plug.in/ns/ext/patch) specifications.
- [MOD GUI specification](https://github.com/mod-audio/mod-sdk/blob/master/modgui.lv2/modgui.ttl).
- [MOD file type hints](https://github.com/mod-audio/mod-sdk/blob/master/mod.lv2/mod.ttl).
- [MOD frontend callback and parameter handling](https://github.com/mod-audio/mod-ui/blob/master/html/js/modgui.js).

For full RDF validation, install `lv2_validate` / `sord_validate` and include
the MOD `mod.ttl` and `modgui.ttl` vocabularies along with generated bundle files.
Without those vocabularies, a general LV2 validator reports MOD properties as
undefined even though they are defined by the MOD extension.
