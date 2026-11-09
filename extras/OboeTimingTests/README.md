# Oboe converted-frame timing regression

This Android command-line test runs the actual conversion graph with deterministic
child streams. It opens no audio device, records no microphone data and needs no
microphone permission. The PCM probe is a finite smooth pulse: the fastest
unfiltered downsampler may legitimately skip a single-sample impulse.

Configure this directory with the Android NDK CMake toolchain, `ANDROID_ABI=arm64-v8a`,
`ANDROID_PLATFORM=android-29`, `ANDROID_STL=c++_static` and a Release build. Build
`oboe-timing-tests`, push it to `/data/local/tmp`, set its executable bit, and run it
with `adb shell`. A successful run reports nonzero checks, zero failures and exits
zero. A 15-second external timeout guards regressions in nonblocking reads.
To reproduce against another checkout, set `OBOE_SOURCE_DIR` to its Oboe directory.

Coverage includes callback-output, callback-input and read-input counters across 96/48, 48/96,
44.1/48 and 48/48 kHz; short and empty reads; restarting the same stream; failed
timestamp queries; nonzero native timestamp positions and fractional conversion;
and pulse timing through every converter quality. Expected pulse time comes from
the source signal and deterministic child clock, independently of wrapper counters.

## Separate existing error-path failure

The test also prints `read_error_contract=FAIL` on the pinned baseline and this
patch: a native `ErrorDisconnected` read emerges as a successful zero-frame read
through the conversion graph. This is not fixed by the timing correction. Run with
`--require-read-errors` to make that separate reproduction fail the process.
The normal timing run checks that an unsuccessful read supplies no new counted
frames; it does not claim correct error propagation or recovery.

These checks do not qualify physical ADC/DAC timing, arbitrary driver timestamps,
blocking output writes, or flush/discard behavior. Real Pixel capture evidence is
recorded in the patch ledger; an application build is a separate qualification.
