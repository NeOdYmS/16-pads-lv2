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
  not in the graphical pad view. Pads are triggered only through `triggerNN`
  controls mapped with MODEP's native MIDI Learn; no MIDI assignment is built in.

The pad view shows 16 square, rubber-style playable pads with their numbers at
the bottom right, mode selectors, and assigned sample names. Empty pads have no
sample placeholder and there is no help footer. Loop pads blink until the next trigger stops them; Trig pads blink until
all held trigger sources are released (even if the sample has reached its end).
Feedback comes from monitored LV2 output controls, so raw MIDI and MODEP-mapped
triggers update the same indicators. One-shot playback is indicated without
blinking. Reduced-motion browser preferences disable the animation.

MIDI assignment and the host's bypass control stay in MODEP settings rather
than the pad facade. Pedalboard jacks use MOD-UI's standard
`mod-pedal-input`/`mod-pedal-output` markup and native jack artwork: MIDI in on
the left edge and L/R audio out on the right edge, vertically centred on the
pad grid.
A horizontal stereo output meter shows L/R peak levels from -60 to 0 dBFS,
with a red overload border at 0 dBFS or above. The DSP reports up to +36 dBFS
without limiting the audio, with immediate attack and a 300 ms peak decay.
These are peak meters, not calibrated analog VU/RMS meters; monitoring depends
on the host delivering the `level_l` and `level_r` output controls.

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

Since 0.0.10 the plugin has no `noteNN`/`ccNN` settings: MIDI-to-pad mapping
is done exclusively by MODEP on the `triggerNN` ports (70 LV2 ports in total).
Raw MIDI received on the events input does not trigger pads; only CC 120/123
(all-sound-off/all-notes-off, any channel) are honoured as a global panic.
Pedalboards saved with older versions keep their trigger/mode/gain/sample
values (saved by symbol); stale `noteNN`/`ccNN` values are ignored.

### Sample file properties and compatibility

There are sixteen independent writable/readable LV2 Patch properties:

```text
https://github.com/NeOdYmS/16-pads-lv2#sampler:sample01
...
https://github.com/NeOdYmS/16-pads-lv2#sampler:sample16
```

They use `atom:Path` and MOD's `mod:fileTypes "audiotrack"` hint.
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

The skin has been exercised in a **local callback simulator**. It remains
opt-in until interactive playback is tested against the target MOD frontend.
The bundle includes a separate `modgui.ttl`, referenced by `manifest.ttl`, and
MOD catalogue screenshot/thumbnail PNGs captured from the local callback render.
These previews are not screenshots of a Pi and do not prove MODEP compatibility.
Disable it with
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

### Catalogue previews and GUI troubleshooting

MOD's GUI specification requires `modgui:screenshot` and `modgui:thumbnail`.
Without them, MOD-UI's details page can display its "NO INTERFACE AVAILABLE"
fallback image even when a GUI resources directory is discovered. That catalogue
message does not itself establish whether the pedalboard's HTML interface works.
Both asset paths must be declared and resolve to readable files.

The first Pi check, reported from the target session, found Debian 13 with
`modep-mod-ui 1.13.0-4` and `modep-mod-host 1.13.0-3`: the initial binary loaded
and its LV2 descriptor/Worker/State interfaces were present, but the catalogue
returned empty screenshot and thumbnail paths. Version 0.0.2 adds those assets,
author/homepage metadata, and an explicit manifest reference to `modgui.ttl`.
The correction still needs interactive verification on that target.

After copying the **whole** corrected bundle, rescan/reload MOD-UI using the
target's actual service procedure (restarts can interrupt audio), then hard
refresh the browser. Check that `/effect/list` returns non-empty GUI image
paths, and the plugin details endpoint `/effect/get?uri=...` resolves the
icon template, stylesheet, JavaScript, and monitored outputs. In browser
developer tools, ensure the image, CSS, and script requests succeed. Add the
plugin to a pedalboard to test the actual interactive GUI, MIDI feedback,
Loop/Trig blinking, and host configuration file pickers. A catalogue image
alone is not a playback test.

The target subsequently displayed the 0.0.6 GUI but reported a collapsing
46px-wide panel. MOD-UI measures the child content width and repeatedly applies
it to the enclosing icon. A percentage maximum width on our padded,
border-box root fed those reductions back into the next measurement.
Version 0.0.7 fixes the root at 640px, leaving board scaling to MODEP.
The browser regression reproduces repeated host measurements inside a narrow,
zoomed wrapper; this correction still needs a Pi retest. Check that the panel
remains stable after adding it, zooming, and receiving meter updates, without
editing the saved pedalboard.

Sample selectors in MODEP settings are not a filesystem browser. MOD-UI fills
each `custom-select-path` list from `/files/list?types=audiotrack`, which only
scans `$MOD_USER_FILES_DIR/Audio Tracks` recursively (default
`/data/user-files`; MODEP observed `/var/modep/user-files`), so nested folders
such as `Audio Tracks/Samples` are included. Version 0.0.9 switched from
`audiosample,audioloop` (`Audio Samples`/`Audio Loops`) at the user's request.
Already saved sample paths elsewhere still load from state. If the folder is
missing or empty,
the list is empty and clicking appears to do nothing. Copy `.wav` files there,
then reopen the settings. The list may also
show other audio formats; this plugin only decodes WAV and reports an error,
keeping the previous sample, for anything else.

Folder navigation depends on the MOD-UI version, not on plugin metadata.
MOD-UI up to 1.13 (including MODEP's `modep-mod-ui 1.13.0-x`) scans subfolders
recursively but returns a flat list of file basenames with no folder entries,
so files with the same name in different folders are indistinguishable.
MOD-UI 1.14 (upstream commit `d6653b7f`, September 2026) added the hierarchical
picker: `/files/list` gains `dirname`/`basepath` fields and the native selector
shows navigable folders. The same `atom:Path` + `mod:fileTypes` metadata works
with both; on 1.13, use unique, prefixed file names (for example
`Kit1 - Kick.wav`) until MODEP ships MOD-UI 1.14 or later.

To regenerate catalogue assets after changing the skin, optional Playwright
tooling can be installed under the ignored build directory:

```sh
npm install --prefix build/gui-tools --no-audit --no-fund playwright
build/gui-tools/node_modules/.bin/playwright install chromium
PLAYWRIGHT_MODULE="$PWD/build/gui-tools/node_modules/playwright" node tools/capture_gui.js
```

The capture tool needs no server and leaves no running browser. It renders
the MOD callback locally and writes `modgui/screenshot.png` plus a proportional
thumbnail of at most 256x64 pixels. These checked-in assets need no browser
dependency at build time or on MODEP.

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
shared library. The target session has also reported successful library loading
and descriptor discovery on the Pi. These checks do **not** verify audio
playback, GUI integration, or performance on the user's MODEP installation.

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

## Preparing a PatchStorage release

The intended distribution platform is
[PatchStorage LV2 Plugins](https://patchstorage.com/platform/lv2-plugins/).
Nothing has been published. The current project is **not release-ready**:
there is no repository licence yet, and target MODEP integration must be
confirmed before describing it as compatible.

Before distributing:

1. Have the copyright holder choose a licence and add its full text as
   `LICENSE`. Add the corresponding `doap:license` URI to generated LV2
   metadata. Do not assume the public repository grants redistribution rights.
2. Record the tested Pi model, Linux architecture/glibc version, MODEP version,
   and whether file selectors, MIDI Learn, and monitored pad feedback work.
   Run the playback and state-persistence checks described above.
3. Build a fresh Linux aarch64 bundle in a separate output directory. Select
   the web skin only after validating it on that MODEP version. Do not reuse
   the macOS bundle, sanitizer binaries, or an older staging directory.
4. Capture the actual MODEP pad view and configuration page. Show sample
   selection, MIDI assignment, and the gain range without exposing private
   filenames or device information. Label simulator screenshots as simulations
   if used; they are not evidence of target compatibility.
5. Archive the complete `16-pads.lv2/` directory with `README.md` and `LICENSE`.
   Include only `sampler.so`, `manifest.ttl`, `sampler.ttl`, optional `modgui.ttl`, and the referenced
   MOD GUI assets inside the bundle. Exclude build caches, test fixtures,
   preview tools, and samples unless their redistribution rights are explicit.
   Check extraction, architecture, and plugin discovery from the archive.
6. Name the release with an explicit version and architecture, provide its
   SHA-256 checksum, and link the matching source revision and build commands.
   Installation instructions must identify the host's real LV2 search path,
   not assume the SSH account's home directory is used by MODEP.

Suggested listing description, to finalize after target verification:

> 16 Pads is an LV2 WAV sampler with an MPC-inspired optional MOD web interface.
> Sixteen pads can play simultaneously, with one voice per pad and independent
> One-shot, Loop, and held-trigger playback. Pressing a playing pad stops it.
> MIDI assignment, sample selection, and per-pad gain (-60 to +12 dB) are
> configured in the host settings. Loop and Trig pads provide blinking feedback
> through supported MOD frontends. This download targets Linux aarch64; see the
> included instructions for tested hosts, WAV formats, and limitations.

Use the README's format/memory/headroom limitations in the listing and state
the verified compatibility scope. Publication, uploading assets, and choosing
a licence require separate authorization from the owner.
