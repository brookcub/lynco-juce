# LynCo JUCE patches

This branch is upstream JUCE plus a short series of LynCo patches, one commit each.

Base tag: `8.0.12` = `29396c22c93392d6738e021b83196283d6e4d850`
Upstream: https://github.com/juce-framework/JUCE

## Rule

Brooklyn (2026-10-19):

> "never mix our changes into juce, keep them visible and separable, and do not build individual product policy/configuration into the juce changes because all lynco apps must share the same juce version with the same patches."

Each patch is one commit, subject `LynCo: <purpose>`, trailer `LynCo-Patch: Pnnn` (`Pnnn.n` for a revision, see "Revising a patch").

This file describes the JUCE problem each patch addresses and what it changes. It does not say which app needed a patch; each app records that in its own decision log.

## Upgrade procedure

1. Create a branch from the new upstream tag and cherry-pick the series onto it, in order.
2. Drop a patch if upstream has fixed the problem it addresses.
3. Rebuild and retest every LynCo app against the branch.
4. Tag the result `lynco-<juce>.<n>`. Tags are never moved; a fix gets a new tag.

### Revising a patch

A patch number names the patch's purpose. When a published patch's purpose stays but its implementation is replaced, the replacement takes the next revision number: P009 becomes P009.1, then P009.2. It takes the original's place in the series, with trailer `LynCo-Patch: P009.1`; the earlier revision stays only in the tags that shipped it. The ledger keeps one section per purpose, describing the current revision, followed by one history line per earlier revision: `P009 (lynco-8.0.12.2 to .5): <what it did>. Replaced by P009.1 in .6: <why>.` A patch whose purpose is dropped is marked retired, with no successor. Numbers are never reused. A patch that has not been published is revised in its PR and keeps its number.

### Moving an app's pin

An app pins the fork as a submodule gitlink at a tagged commit.

1. **Approval:** moving the pin needs Brooklyn's approval in the app's repository. The approval is a commit message line `JUCE-Upgrade: approved <sha>`, where `<sha>` is the full 40-character commit the gitlink points to at the head of the change. A tag name or a short SHA does not count. Only Brooklyn gives it, and it is never self-served.
2. **Gate:** if the gitlink differs between the change's base and head, some commit in base..head must carry that line with the head gitlink's SHA. A bare `JUCE-Upgrade: approved`, or a line naming another SHA, does not count, so moving the pin again needs a new line. A change that moves the pin and back needs none.
3. **Fetching a tag** into a submodule whose clone borrows objects through alternates: `git -c fetch.negotiationAlgorithm=noop fetch origin tag <tag>`. The default negotiation can fail on the thin pack.

## Macros the series adds

An app that defines a macro does so at its own build boundary (for example `target_compile_definitions`), for every translation unit that includes the module.

| Macro | Defined by | Default | Patch |
|---|---|---|---|
| `JUCE_AUDIO_DEVICE_MANAGER_HAS_OWNER_CONTROLLED_LIST_CHANGES` | `juce_AudioDeviceManager.h` (capability marker, always `1`) | `1` | P002 |
| `JUCE_ANDROID_AUDIORECORD_MULTICHANNEL_SOURCE` | the app, optional | `0` (`MediaRecorder.AudioSource.DEFAULT`) | P003 |
| `JUCE_ANDROID_OBOE_INPUT_PRESET` | the app, optional | not defined; no input preset is set | P004 |

## Patches

### P001: AudioRecord channel mask follows the opened channel count

- **Files:** `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** the AudioRecord channel mask is chosen from `numDeviceInputChannels` (opened) instead of `numDeviceInputChannelsAvailable` (offered).
- **Why:** opening one channel on a stereo-capable device gave a stereo AudioRecord while the reader assumed mono, so each callback read interleaved L/R as one mono stream at twice the frame count.
- **Default behaviour:** fixed for every app that opens fewer input channels than the device offers; unchanged otherwise.

### P002: AudioDeviceManager device-list change handler

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h`, `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp`
- **Changes:** adds `AudioDeviceManager::setAudioDeviceListChangeHandler()`, the `AudioDeviceListChangeHandler` type, a `deviceListChangeHandlerLock`, and the capability define `JUCE_AUDIO_DEVICE_MANAGER_HAS_OWNER_CONTROLLED_LIST_CHANGES`. `audioDeviceListChanged()` calls the handler first, synchronously, with "current device still available".
- **Why:** JUCE closes and reopens a vanished device before the app can apply its own device-loss policy.
- **Default behaviour:** with no handler set, JUCE's own close/reinitialise recovery runs unchanged. Every platform.

### P003: AudioRecord source when more than one input channel is opened

- **Files:** `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** the AudioRecord audio source for a multi-channel open (`numDeviceInputChannels > 1`) is the macro `JUCE_ANDROID_AUDIORECORD_MULTICHANNEL_SOURCE`, which defaults to `0` (`MediaRecorder.AudioSource.DEFAULT`). A one-channel open always uses `DEFAULT`. An app that wants raw multi-channel capture defines the macro as `9` (`UNPROCESSED`).
- **Why:** on a Pixel 7 a two-channel AudioRecord on the default source returns one microphone duplicated; `UNPROCESSED` returns both physical microphones. Which source is right is the app's decision, so JUCE only exposes the choice.
- **Default behaviour:** upstream's, unless the app defines the macro.

### P004: Oboe input preset

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`
- **Changes:** when `JUCE_ANDROID_OBOE_INPUT_PRESET` is defined, Oboe input streams call `setInputPreset()` with its value (an `oboe::InputPreset`) before they open. Output streams are untouched.
- **Why:** JUCE's Oboe device cannot otherwise set the input preset, and Oboe's default, VoiceRecognition, processes the microphone signal.
- **Default behaviour:** inert unless the macro is defined; it takes effect on Android API 28+ (AAudio) only.

### P005: Android AudioRecord input callbacks

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioIODeviceType.h`, `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** adds `AudioIODeviceType::AndroidAudioRecordCallbacks { beforeStart, afterStart, beforeStop }` and `AudioIODeviceType::setAndroidAudioRecordCallbacks()`. Each open copies the registered set and releases it on close.
- **Why:** an app cannot reach the AudioRecord JUCE opens, so it cannot call `setPreferredDevice()` or watch the routed device.
- **Default behaviour:** no callbacks registered, upstream behaviour. Android AudioRecord device type only.

### P006: Android software peer survives a failed paint-buffer allocation

- **Files:** `modules/juce_gui_basics/native/juce_Windowing_android.cpp`
- **Changes:** `handlePaintCallback` checks the `NewIntArray` result and pending exception, records `sizeAllocated` only on success, and paints in strips when the full-clip buffer cannot be allocated; a frame is skipped if not even one row fits. A size that failed is not tried again by that peer.
- **Why:** when the Java heap cannot hold the paint buffer, the unchecked allocation left an `OutOfMemoryError` pending and CheckJNI aborted the process in `GetIntArrayElements`.
- **Default behaviour:** unchanged whenever the allocation succeeds. Android software component peer only.
- **Test evidence:** repeatable with this fork and the Android SDK alone, using JUCE's own `examples/DemoRunner`. Tested 2026-10-22 on SDK platform 35, NDK 28.1.13356709, CMake 3.22.1, build-tools 35.0.0, JDK 17, emulator image `system-images;android-29;google_apis;x86_64`.
  1. Create and start a 16 MiB-heap Android 29 emulator (`ANDROID_HOME` is the SDK; use the `-port`/serial you own, never another device's):
     ```
     avdmanager create avd -n juce_p006_api29 -k "system-images;android-29;google_apis;x86_64" -d pixel_2
     emulator -avd juce_p006_api29 -port 5720 -no-window -no-snapshot -no-audio -gpu swiftshader_indirect
     adb connect 127.0.0.1:5721            # then wait for: adb -s 127.0.0.1:5721 shell getprop sys.boot_completed  ->  1
     ```
  2. Cap the Java heap after boot (`-prop dalvik.vm.heapsize=16m` at boot is ignored by this image), repeating this before every run because it does not survive an emulator restart. Wait about 30 s after it before installing:
     ```
     adb -s 127.0.0.1:5721 root
     adb -s 127.0.0.1:5721 shell "setprop dalvik.vm.heapsize 16m; setprop dalvik.vm.heapgrowthlimit 16m; stop; start"
     adb -s 127.0.0.1:5721 shell getprop dalvik.vm.heapgrowthlimit      # 16m
     ```
     The limit the app really gets is printed by ART in the failure line of the logs below: `growth limit 16777216`. Verified in all six runs.
  3. Build DemoRunner for x86_64 (about 12 min cold):
     ```
     cd examples/DemoRunner/Builds/Android
     ./gradlew --no-daemon -Pandroid.injected.build.abi=x86_64 assembleDebug_Debug
     ```
     APK: `app/build/intermediates/apk/debug_/debug/app-debug_-debug.apk`. Copy it aside as `with-P006.apk`.
  4. Build without P006: `git show <P006> | git apply -R` (`<P006>` is the commit with trailer `LynCo-Patch: P006`; reverting only its diff keeps later patches to the same file), rebuild as in step 3, copy the APK aside as `without-P006.apk`, then `git checkout HEAD -- modules/juce_gui_basics/native/juce_Windowing_android.cpp`.
  5. Run each APK, repeating step 2 first:
     ```
     adb -s 127.0.0.1:5721 shell wm size 1600x2560; adb -s 127.0.0.1:5721 shell wm density 320
     adb -s 127.0.0.1:5721 install -r -t <apk>
     adb -s 127.0.0.1:5721 logcat -c
     adb -s 127.0.0.1:5721 shell am start -W -n com.rmsl.jucedemorunner/com.rmsl.juce.JuceActivity
     sleep 15; adb -s 127.0.0.1:5721 shell pidof com.rmsl.jucedemorunner
     adb -s 127.0.0.1:5721 logcat -d | grep -E "OutOfMemoryError|JNI DETECTED|Fatal signal"
     adb -s 127.0.0.1:5721 exec-out screencap -p > shot.png
     ```
  6. Expected, 3 of 3 runs each: without P006 the process is gone (`pidof` prints nothing) and the log has two `Throwing OutOfMemoryError "Failed to allocate a 16384012 byte allocation ... growth limit 16777216"` lines, then `JNI DETECTED ERROR IN APPLICATION: JNI GetIntArrayElements called with pending exception java.lang.OutOfMemoryError` in `AndroidComponentPeer::handlePaintCallback`, then `Fatal signal 6 (SIGABRT)`. With P006 the same two OutOfMemoryError warnings appear, there is no JNI error or abort, `pidof` prints a pid, and the screenshot shows the painted DemoRunner start screen.
  7. The same run with a LynCo app's Debug x86_64 build at `lynco-8.0.12.4`, 2026-10-24, 3 runs each: without P006 the app aborts the same way in 3 of 3; with it the app stays alive in 3 of 3 and paints its full screen. Not exercised: the skipped frame when not even one row fits.

### P007.1: Callback context carries native device timestamps by role

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioIODevice.h`, `modules/juce_audio_devices/audio_io/juce_CallbackTimeHelpers.cpp` and `juce_CallbackTimeHelpers_test.cpp` (new), `modules/juce_audio_devices/juce_audio_devices.cpp` (their includes).
- **Changes:** adds `AudioIODeviceTimestamp { timeNs, sampleOffset, nativeFlags }` and three pointers to it in `AudioIODeviceCallbackContext`, one per role: `inputStream` (a time at the source's capture or input pipeline reference point), `outputStream` (at its playback or presentation reference point, an observed position or a native prediction per source) and `processingCycle` (the native time of the processing cycle, with no capture or presentation claim). P007's `inputHostTimeNs` is removed; `hostTimeNs` keeps upstream's comment and meaning.
  - **Absence:** `nullptr` is the only absence marker; zero is a valid time. A native value that the source marks invalid without a payload is absent; one delivered with a native error or degraded flag is delivered with `nativeFlags` set (`nativeDiscontinuity`, `nativeTimestampError`, `nativeReducedAccuracy`).
  - **Clock:** `timeNs` is in ns on the clock and origin of `Time::getHighResolutionTicks()` (QPC on Windows, `mach_absolute_time` on Apple, `CLOCK_MONOTONIC` on Android and Linux), converted from the native value by an exact unit conversion. The header names the only exceptions: ASIO's clock-origin offset, measured once per run (P021); JACK's per-callback `CLOCK_MONOTONIC - CLOCK_MONOTONIC_RAW` difference (P023); and WASAPI's 100 ns units, which carry up to 100 ns of the OS's own rounding when QPC is not 10 MHz (P009.1). No latency, smoothing or rate correction.
  - **Offset:** `sampleOffset` is the timestamped sample's position minus that of the buffer's first sample, in frames: negative before the buffer, positive after its start (for example a real sample after leading padding). Padding is never timestamped.
  - **One adjustment:** when sample-rate conversion puts the native position x between two application samples, the position becomes the nearest sample, n = floor (x + 1/2) (a half-sample residual rounds up), and the time moves by (n - x) samples at the application rate, at most half a period, applied once.
  - **Run:** `audioDeviceAboutToStart` and `audioDeviceStopped` delimit the run a role's sample positions belong to; no observation crosses a stop.
  - **Source table:** the context's comment has a table of each backend's native source per role, with observation or prediction. Each backend's patch adds its own row (P009.1, P013.1, P014.1, P020 to P023); until then a backend is under "all others", every role absent.
  - **Helpers:** `CallbackTimeHelpers::alignToApplicationSample` implements the one adjustment in exact integer arithmetic (the position is split into whole multiples of the denominator and a remainder, so no intermediate reaches 2^63, and no 128-bit type is needed), and `forChunkAt` gives an observation as seen from a chunk of its buffer (time and flags unchanged, offset minus the chunk's start). Each backend's patch adds its own helpers and tests to the same two files, one section per patch.
- **Why:** P007's single time per direction was defined at the callback's first sample, so every backend had to project a native (position, time) pair to that sample with a nominal rate, and a duplex callback's input, output and cycle times were mixed in two fields. A consumer that relates the device clock to its buffers needs the native observation itself and the sample it belongs to.
- **Default behaviour:** every platform. Code that reads `hostTimeNs`, or initialises the context with it alone, is unaffected; code that used `inputHostTimeNs` moves to `inputStream`. Every role is absent until a backend's patch fills it.
- **Rails:** the acceptance tests of P007.1 to P023 were written from the contract by a rails author before the code, blind to it, and the builders did not edit them. Every expected value is a hand-worked integer or exact fraction (checked with Python's `fractions.Fraction`); no test calls production code to compute an expectation, and a sweep checks the defining relation on exact integers instead of a copy of the formula. Each rule was checked against a throwaway reference and mutants of it. For P007.1's alignment, red cases across the suites: a floor instead of the nearest sample 5, ties rounded down 3, truncation toward zero with a 64-bit product 8, double arithmetic 1 (large counters). The half-nanosecond tie of the final ns rounding is not pinned. Integration harnesses (macOS combined device, Linux ALSA and JACK) ran on CI branches outside this series. "SPEC", "REQUIREMENTS" and data paths such as `hw-windows/` in the sections below refer to the contract, decisions and measurements kept in the LynCo SDK review workspace (`_review/timestamps-v2`); the fork has no decision log of its own.
- **Test evidence:** `UnitTestRunner` built out of tree with `cmake -S <JUCE> -B <build> -DJUCE_BUILD_EXTRAS=ON`, then `cmake --build <build> --target UnitTestRunner --config Release`, run as `UnitTestRunner --category=Audio`, Windows 11, MSVC (Visual Studio 18), on the whole .6 series: `Callback timestamp alignment` 9 of 9, whole run 288 of 288, exit 0.

P007 (lynco-8.0.12.2 to .5): Added `inputHostTimeNs` and defined it and `hostTimeNs` as the times of the callback's first input and output samples, projected from the device's position and time pairs. Replaced by P007.1 in .6: a projection to the first sample needs a nominal rate and mixes roles; the context now carries each role's native observation with its sample offset, and `hostTimeNs` is upstream's again.

### P008.1: Split callbacks stay correctly associated

**Files:** `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp` (`CallbackMaxSizeEnforcer` and the `AudioDeviceManager` tests). Uses `CallbackTimeHelpers::forChunkAt` from `audio_io/juce_CallbackTimeHelpers.cpp` (P007.1).

**Why:** `CallbackMaxSizeEnforcer` splits a device callback into blocks no longer than the device's buffer size. A native timestamp names a moment and a sample position relative to the callback's buffer; for a block that starts k samples into that buffer, the same moment is k samples earlier relative to the block. The enforcer therefore keeps each timestamp's time and moves only its offset, which needs no sample rate and no arithmetic on times.

**Contract:** For a block starting at sample k, each present role (`inputStream`, `outputStream`, `processingCycle`) is passed with the same `timeNs`, the same `nativeFlags` and `sampleOffset - k`. An absent role stays `nullptr`. `hostTimeNs` is passed to every block unchanged, exactly as stock JUCE 8.0.12's enforcer does. Per-block copies live on the audio thread's stack: no allocation, no locks. The enforcer no longer stores the device's sample rate.

**Verification:** Windows `UnitTestRunner --category=Audio` (Release), whole .6 series: `AudioDeviceManager` (16 cases, 5 of them the enforcer's, driven through the mock device) and `Callback timestamp alignment` (9 cases) pass; whole run 288 of 288. Upstream's enforcer (the context passed unchanged) fails 4 of the 5 enforcer cases, on the role offsets only; P008's fails on `hostTimeNs` as well.

P008 (lynco-8.0.12.2 to .5): Advanced the callback context's host time and the input host time by each block's duration when `CallbackMaxSizeEnforcer` split a callback. Replaced by P008.1 in .6: the callback context now carries native device timestamps whose time stays fixed and whose sample offset moves with the block, and `hostTimeNs` returns to stock behaviour, so no time is advanced by a computed duration.

### P009.1: WASAPI forwards device timestamps

**Files:** `modules/juce_audio_devices/native/juce_WASAPI_windows.cpp` (`CapturePacketLedger`, `getRenderTimestamp`, `WASAPIInputDevice`, `WASAPIOutputDevice`, the run loop). `InputTimeline` and `getOutputFrameTimeNs` are gone. Uses `CallbackTimeHelpers::alignToApplicationSample` (P007.1).

**Why:** WASAPI stamps every capture packet with the QPC time of its first frame, and `IAudioClock` says which frame of the render stream is playing at a QPC time. Neither is a time for the callback's buffer: capture packets are queued and cut into callbacks of another size, and the render clock reading is not at the buffer's first frame. The context therefore carries each native observation unchanged, with the offset from the callback's first frame to the observed frame. Nothing is projected from a nominal rate or a queue length.

**Contract:**

| | `inputStream` | `outputStream` | `processingCycle` | `hostTimeNs` |
|---|---|---|---|---|
| Native source | `IAudioCaptureClient::GetBuffer` QPC position of a packet | `IAudioClock::GetPosition` position and QPC | absent | absent (stock: WASAPI never set it) |
| Reference point | the capture time of the packet's first frame | the frame being presented at that QPC time | - | - |
| Observation or prediction | observation | observation (a position that has been presented) | - | - |

- **Clock conversion:** GetBuffer's and GetPosition's QPC values are in units of 100 ns on the `Time::getHighResolutionTicks()` clock and origin, so `timeNs = qpc * 100`, including for 0. The 100 ns unit is the OS's own conversion of the QPC count (count x 1e7 / frequency): it is exact only for a 10 MHz QPC (this PC's), and otherwise (HPET, ACPI-PM) the value carries up to 100 ns of the OS's rounding; nothing in the SDK adds to it. The input time is never altered.
- **Input offset:** a ledger of packets (first frame in a running count of frames appended, size, time, flags) sits beside the queue. A callback takes the oldest queued frames, after leading padding when fewer than wanted are queued. The observation is that of the packet holding the first real sample of the buffer; its offset is `packet first frame - first real sample + padding`: zero for a packet taken whole, negative once part of it was taken by earlier callbacks, positive after padding. If the first real sample's packet is not in the ledger (frames queued with no record), or the buffer has no real sample, the role is absent; nothing is back-projected. Flags travel with the packet: `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY` gives `nativeDiscontinuity` and `AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR` gives `nativeTimestampError` (the time is still delivered); `SILENT` is ignored. The ledger is reset in `start()`, sized by the queue's capacity, and allocates nowhere else.
- **Output position:** the stream position is `position * rate / frequency` frames, `frequency` being `IAudioClock::GetFrequency` (it is not necessarily a frame count). It is aligned once to the nearest frame by `alignToApplicationSample` (half-frame residual rounds up; the time moves by the residual at the application rate, never more than half a period). The offset is the aligned position minus the stream position of the callback's first output frame, which is `RenderStreamCorrespondence::callbackStart()`, the count of frames released to the render client since `open`, including the initial silence. `S_FALSE` from `GetPosition` gives `nativeReducedAccuracy`; a failed call, a zero or oversized frequency, or a missing clock gives absence.
- **Render after an underrun (SPEC ruling 9):** measured on the Focusrite endpoint. The position counts only submitted frames (it stands still during an underrun and continues from the same place), but after a 600 ms underrun its (position, QPC) pair no longer matches presentation: against an ASIO reference the output role was 8 to 16 ms off for the rest of the run while the input role was unchanged. So the output role is absent from the first detected underrun until the device is opened again. Detection is the render loop's own bookkeeping (`RenderStreamCorrespondence`): every successful `GetCurrentPadding` taken before a write goes to `paddingBeforeWrite`, in shared and exclusive mode alike and for the first application write after `start()`'s silence as for any other; a padding of 0 once any frame has been released latches the loss. A padding that cannot be read before a write is a lost correspondence, and a clock reading ahead of the frames released latches it too. There is no exemption for the first block.
- **Start-up dry, measured consequence (Focusrite, shared, 48 kHz / 480, this PC):** the engine reports a padding of 0 about 0.7 ms after `start()` has released 1056 frames of silence, before the application's first write, so the latch fires at the first write of every run and the output role is absent from callback 0 to the end of every run (20 s run, 1999 of 1999 callbacks; the 60 s run with a 600 ms stall at 30 s, 5944 of 5944; input role present throughout). The engine takes the initial frames into its own buffer as soon as the client starts, so a padding of 0 right after start is how the stream begins, not something the application can avoid by ordering. Two orderings were measured and not adopted. Starting the client only after the first application block was written (lab-only experiment, the 480-frame block released first) still read a padding of 0 at the second write 0.9 ms later. The silence already fills the whole 1056-frame buffer, so there is no longer pre-fill to try. With the latch disabled in the lab so that the loop L can be compared, five sessions each of 30 s gave per-session L medians (after 5 s, ms): current ordering 192.16, 186.48 (96 xruns), 184.38, 184.46, 186.32; start-after-first-block 184.43, 176.45, 192.36, 192.33, 184.23. The spans are 7.8 ms and 16.1 ms, against the 1.0 ms required, and the first-block ordering has more xruns, so it removes neither the dry nor the session-to-session steps and is not adopted. On this PC the WASAPI output role is therefore absent for every shared-mode run; the input role is unaffected. Data: `hw-windows/data-r3/` (`dry0`, `arm0`, `arm*_s*`, `L480stall_final`).
- **Exclusive mode (measured, Focusrite output, 48 kHz / 480, exclusive output only: the endpoint's input refuses exclusive duplex with "Couldn't open the input device!"):** the padding is read after the event and before `GetBuffer`, for the latch only; the write stays the whole buffer. At the event it is 480, the whole buffer, at every pass of a clean 15 s run (1501 reads of 480; the output role present in 1500 of 1500 callbacks), and it is 0 at every pass after an underrun: stalls of 600, 40 and 25 ms each left it at 0 from the stall to the end (535, 582 and 584 reads) and the output role absent from the stall to the end (48 %, 46 % and 46 % of callbacks present, all before the stall), while a 15 ms stall (about one and a half periods) caused no underrun, kept the padding at 480 and left the role present throughout. So with this driver the padding is a usable underrun signal in exclusive mode and the latch works there; no exclusive-mode click loop was measured, and other drivers may differ (Microsoft documents the padding as of no use there). Where the padding cannot be read, the role is absent from that write.
- **Start-up settling:** in the first 4 to 5 s the output time settles by about 14 ms. That is the native timing of a correctly associated observation and is forwarded unchanged (REQUIREMENTS section 5); nothing is smoothed or dropped.
- **Roles in a callback:** `{ nullptr, inputStream, outputStream, nullptr }`.

**Verification:** Windows `UnitTestRunner --category=Audio` (Release, `_build/juce-r6-utr`, commit `661880267f`): `WASAPI device timestamps` 23 of 23, whole run 288 of 288 cases, exit 0; the same on the whole .6 series. Hardware as described under the start-up dry and exclusive-mode items above (lab built from `661880267f`; the per-write padding log and the ordering experiment were temporary lab-only patches, not in the commit).

**Limits:** Measured on the Focusrite interface with a loopback cable (output 1 to input 1), shared mode at 48 kHz / 480 and 192 frames, and exclusive output only at 480; not measured: low-latency mode (`IAudioClient3`; if its buffer equals one engine period a steady-state padding of 0 would falsely latch the loss), shared mode with sample-rate conversion, exclusive duplex (the input refused), and any other exclusive driver. On this PC the shared-mode output role is absent for the whole of every run (start-up dry above); the loop L measured in earlier sessions (185 to 192 ms, session-dependent in 8 ms steps) is therefore only available with the latch disabled. The input offset is zero in a steady state only when callbacks and packets are aligned; otherwise it is the (negative) distance back to the packet's first frame.

P009 (lynco-8.0.12.2 to .5): Forwarded WASAPI input and output times as `inputHostTimeNs` and `hostTimeNs`, projecting the capture time of the first queued frame back from the newest packet and the output frame time forward from `IAudioClock` with a nominal rate. Replaced by P009.1 in .6: the callback context now carries per-role native observations (`inputStream`, `outputStream`) with the sample offset that goes with each, taken from the packets and clock readings themselves, and `hostTimeNs` returns to stock (empty).

### P010: Oboe stream recovery is reported through the device lifecycle

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`
- **Changes:** `OboeAudioIODevice` derives privately from `AsyncUpdater`. When an Oboe output stream fails, the session's `onErrorAfterClose` (Oboe's error thread) only calls `OboeAudioIODevice::streamFailed()`, which records the error in an atomic (the first error wins) and triggers an update. `handleAsyncUpdate()` runs on the message thread: it stops the device with `start (nullptr)` (so the callback gets `audioDeviceStopped()`), and after `ErrorDisconnected` opens the device again with the channels, sample rate and buffer size it had and starts it with the same callback (so the callback gets `audioDeviceAboutToStart()`). If the reopen fails, or the error was anything else, it sets `deviceOpen = false` and `lastError` and calls `audioDeviceError()` on the callback. `stop()` clears the recorded error and cancels the pending update, so nothing runs for an error that predates a stop, a reopen by the app, or the device's deletion. `streamRestartMutex` is removed (it only kept two error callbacks from restarting at once on the error thread; recovery is now serial on the message thread); `audioCallbackMutex` stays. The latency, bit depth and xrun getters no longer dereference a null session after a failed reopen.
- **Why:** `onErrorAfterClose` destroyed and reopened the streams on Oboe's error thread after `ErrorDisconnected` (for example a Bluetooth speaker connecting or disconnecting) and told the app nothing: no `audioDeviceStopped`, `audioDeviceAboutToStart` or `audioDeviceError`. The new stream could have a different latency, buffer size or device. After any other error the streams were destroyed and the device silently stopped.
- **Default behaviour:** Android Oboe device only; no new API and no macro. The callback now hears the lifecycle callbacks above, on the message thread. Known cost: audio is silent a little longer after a disconnect, because the reopen waits for the message thread. `audioDeviceError` reaches an `AudioDeviceManager`'s callbacks only if its `CallbackMaxSizeEnforcer` forwards it, and upstream's does not; that is a separate gap in `juce_AudioDeviceManager.cpp`.
- **Test evidence:** Tested 2026-10-23. The harness is a small JUCE Android app (outside this repository) that registers an `AudioIODeviceCallback` with an `AudioDeviceManager` on the UI thread and logs every `audioDeviceAboutToStart`, `audioDeviceStopped` and `audioDeviceError` with the thread, the device's buffer size and sample rate, and `getXRunCount()` once a second.
  1. Compiles: the unity sources of `juce_core`, `juce_events`, `juce_audio_basics` and `juce_audio_devices` build for `arm64-v8a` and `x86_64` with NDK 28.1.13356709 and CMake 3.31.6; no warnings from `juce_Oboe_android.cpp`.
  2. Android 15 emulator (`system-images;android-35;google_apis;x86_64`, `-no-audio`), with a test-only hook (not in this repository) that fails the output stream the way Oboe's error thread does: it stops and closes the stream, then calls `onErrorAfterClose` from a new thread. With this patch: disconnect gives `audioDeviceStopped` then `audioDeviceAboutToStart`, both on the message thread, then audio callbacks again at about 100 per second (27 ms and 271 ms from stopped to aboutToStart); another error (`ErrorInternal`) gives `audioDeviceStopped` then `audioDeviceError "Audio stream error: ErrorInternal"` and `getXRunCount()` is -1; a disconnect whose reopen fails gives `audioDeviceStopped` then `audioDeviceError "Unable to reopen the audio device after it was disconnected: ..."`. After either error `isOpen()` is false and no further audio callbacks run. No `DetachCurrentThread` or JNI warnings in the log. The `audioDeviceError` lines needed `AudioDeviceManager`'s enforcer to forward the error, which it does not upstream; the runs above had that one method added in the test tree.
  3. The same hook on the code before this patch: a disconnect gives no lifecycle callback at all (callbacks dip briefly and continue); another error gives none either, audio callbacks stop, and the device still reports `deviceOpen=yes`.
  4. Pixel 7, Android 16 (`CP1A.260405.005`), this series up to P012, Oboe duplex 48 kHz, 2026-10-23. A Bluetooth speaker was switched on and off three times while the harness played a tone. Six route changes, each giving `audioDeviceStopped` then `audioDeviceAboutToStart`, both on the message thread with nothing in between, 16 to 29 ms apart, and the first audio callback 70 to 205 ms after `audioDeviceStopped`; the tone moved to the speaker and back each time. No `audioDeviceError`, `getXRunCount()` 0 throughout, and no `DetachCurrentThread`, JNI or fatal lines from the harness. When the harness was force-stopped afterwards, the phone's audio HAL aborted and restarted (`audio.primary.gs201.so`, `in_mmap_stop` locking a destroyed input stream); the phone holds 22 identical tombstones from 2026-10-20 on, before this patch existed, so that is the vendor HAL closing an MMAP input stream, not this patch.

### P011: Oboe `getXRunCount()` returns -1 when it has no count

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`
- **Changes:** `OboeStream::getXRunCount()` returns -1 (was 0) when the stream is missing or the query fails. The session returns -1 when neither the input nor the output stream gave a count, and otherwise the sum of the counts that were available (it was `jmax (0, ...)` of each, with a missing stream counted as 0).
- **Why:** `AudioIODevice::getXRunCount()` is documented to return -1 if the count is not supported or not available, and `AudioDeviceManager::getXRunCount()` already handles it, but the Oboe device returned 0, so "no underruns" and "no count" were indistinguishable.
- **Default behaviour:** the Oboe device returns -1 where it used to return 0 when no stream gave a count (a closed or failed device, or Android SDK 23 and lower). Unchanged while a stream reports a count.
- **Test evidence:** same harness and hook as P010, Android 15 emulator, 2026-10-23. While running, `getXRunCount()` is 0 with and without the patch. After the hook fails the output stream and the device stops (the streams are destroyed), the code before this patch reports `deviceXRuns=0` and the patched code `deviceXRuns=-1`. The query-failure branch (SDK 23 and lower) is not reachable on the emulator's SDK 35, and the harness's minSdk is 26.

### P012: `AudioDeviceManager` forwards `audioDeviceError` to its callbacks

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp`
- **Changes:** `CallbackMaxSizeEnforcer` overrides `audioDeviceError (const String&)` and forwards it to the inner handler, as it already does for `audioDeviceAboutToStart`, the IO callback and `audioDeviceStopped`. The `AudioDeviceManager` unit tests gain one case; the mock device gains `reportError()` and `MockCallback`'s `error` function now receives the message.
- **Why:** the callback registered with the device is the enforcer (`getAudioIODeviceCallback()` returns it), not `CallbackHandler`. Without the override, `AudioIODeviceCallback::audioDeviceError`'s empty default swallowed every error, so no backend's error reached the callbacks added with `addAudioCallback()` and `CallbackHandler::audioDeviceError` (which calls `audioDeviceErrorInt`) never ran. This is the gap P010 refers to.
- **Default behaviour:** all backends. An app whose callback overrides `audioDeviceError` now receives the errors its devices report, on the thread the device reports them from (`AudioDeviceManager` passes them on unchanged). Apps that do not override it see no change.
- **Test evidence:** Tested 2026-10-23, Windows, `UnitTestRunner` Release, `--category=Audio`. A mock device reports `"Audio stream error: ErrorInternal"` to the callback it was started with; a callback added to the manager must receive exactly that string once. With the patch: 220 tests completed, 0 failures (219 before the new case). With the override removed and the test kept: the new case fails 2 checks (expected 1 received error, got 0; expected the message, got an empty string) and 219 tests completed.

### P013.1: Oboe forwards device timestamps

**Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`, `modules/juce_audio_devices/audio_io/juce_CallbackTimeHelpers.cpp` (`getOboeInputTimestamp`, `getOboeOutputTimestamp`).

**Why:** a consumer that wants the device clock relation needs the stream's own timestamp associated with the callback's buffer, not a projection.

**Contract:**
- Roles: `inputStream` and `outputStream`; `processingCycle` and `hostTimeNs` absent (stock Oboe `hostTimeNs` is empty).
- Native source: `AudioStream::getTimestamp (CLOCK_MONOTONIC)`, read once per callback per stream, only on Android 11+ (API 30, the existing guard); otherwise absent. Reference point: the stream's frame position passing the device (observation, not prediction). The same reading also gives the latency (`getLatencyFor()`, JUCE's own arithmetic), so the callback makes no more calls into Oboe than upstream.
- Clock: CLOCK_MONOTONIC ns, the `Time::getHighResolutionTicks()` timeline; no conversion, so no rounding. Streams at the application's rate; a converted stream's timestamp is aligned to application frames inside bundled Oboe (P018).
- Input offset = native position minus the first frame of the buffer (`getFramesRead()` after the read minus frames just read). A short read has the frames at the buffer start; no successfully read frame means no input observation. The offset can be positive: the frame Android timestamps can be one the device has captured but the application has not read yet.
- Output offset = native position minus `getFramesWritten()` at callback start.
- Native timestamps repeat: Android refreshes a stream's (frame, time) pair about every 50 ms (about 20 Hz), so most callbacks carry the same pair as the one before, at an offset that has moved with the buffer. A repeated pair is forwarded unchanged; nothing is extrapolated between refreshes.
- Time and flags unchanged (`nativeFlags` 0: Oboe reports no qualifications).

**Verification:** `Oboe stream timestamps` (6 cases) in `UnitTestRunner --category=Audio`, Windows, and on the whole .6 series (whole run 288 of 288); NDK 28.1 syntax compile of `juce_audio_devices.cpp` for arm64 Android with `JUCE_UNIT_TESTS`. Pixel 7, Android 16, a Release lab app built on a tree whose Oboe code is identical to this series, 2026-11-11: at 48 kHz on the built-in devices, 31,043 callbacks, both roles present in every one, flags 0, 29,814 of them a repeated pair, no position or time going backwards, input offsets -2448 to +240 frames, output offsets -3264 to -528, and the rate between distinct pairs 47,980 to 48,020 Hz over 62 spans; at 44.1 kHz (converted, P018), 28,553 callbacks, the same, input -2410 to +244, output -3043 to -505, 44,087 to 44,115 Hz.

**Limits:** devices before Android 11 forward no roles; streams that fail `getTimestamp` give absence for that callback. No absolute (acoustic loop) check of the roles was made.

P013 (lynco-8.0.12.2 to .5): Filled `hostTimeNs` and `inputHostTimeNs` from each stream's `getTimestamp` pair, projected to the callback's first input and output frames at the stream's nominal rate. Replaced by P013.1 in .6: the projections were estimates; the native observation is forwarded with its offset.

### P014.1: CoreAudio forwards device timestamps

**Files:** `modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp`; shared helpers in `audio_io/juce_CallbackTimeHelpers.cpp` (`getBufferStartTimestamp`, `getCoreAudioChunkTimes`, `getCombinedCallbackContext`, `CombinedOutputCorrespondence`; P020 shares `getBufferStartTimestamp`).

**Why:** a CoreAudio IOProc is handed `inInputTime` and `inOutputTime`: the host time at which the first frame of the input buffer was acquired and at which the first frame of the output buffer will be presented. These are device observations of the buffer the callback is given. JUCE splits an IOProc buffer into chunks no longer than the application's buffer size, so a chunk that starts k frames in sees the same moment k frames earlier. When one input and one output device are combined by `AudioIODeviceCombiner`, the application's output goes through a FIFO to the output device, whose stamp names a frame of what that device read; the application can be given that stamp only with the proven correspondence between the FIFO frame and its own buffer.

**Contract:**
- Ordinary device. `inputStream` = `inInputTime` and `outputStream` = `inOutputTime`, each only when the callback has channels in that direction and the stamp has `kAudioTimeStampHostTimeValid` (otherwise absent). Native source: the AudioTimeStamp passed to the IOProc. Reference: the first frame of the IOProc's buffer. Observation, not prediction. A chunk starting at k frames has `sampleOffset` = -k, the same `timeNs` and `nativeFlags` 0. `processingCycle` absent.
- Clock: `mHostTime` (mach_absolute_time ticks) to ns by the same `HostTimeToNanos` conversion stock JUCE uses (`mach_timebase_info` numerator/denominator on the 64-bit tick count); no further rounding is introduced by this patch.
- `hostTimeNs` is stock 8.0.12: first chunk only, the output stamp's host time when the callback has outputs and the input stamp's otherwise, converted without consulting the flags, none without that stamp.
- Combined device. The application callback runs from the input device's callback. `inputStream` is the input device's role (offset relative to the application buffer, which is the input chunk); `hostTimeNs` is the input device's. `outputStream` is the output device's `inOutputTime` for the FIFO frame it names, at offset (FIFO address of that frame) - (FIFO address of the application buffer's first frame), a negative number while the output device is behind the application. `CombinedOutputCorrespondence` records the FIFO address range of each run of consecutive application writes and the output device's last stamp together with the address of the frame it names; the stamp is accepted only if the named frame was read in that same output callback (not as padding) and lies in the current run of written addresses. A write that does not continue the previous one, an xrun, or a reset starts a new run and loses the observation until a frame of the new run is read and stamped. The latest observation is repeated, its offset falling with the application's progress, until a new one replaces it; its flags travel with it. FIFO times and rounded addresses derived from times are never used for correspondence. `processingCycle` absent.
- Threads. The application callback (input device thread) calls `observationFor` and `wrote`; the output device thread calls `read`; `reset` runs on either. Each side publishes its own state through a single-writer sequence lock built from `std::atomic` words, and `reset` only bumps an atomic epoch. No locks, no allocation; a reader that collides with a write repeatedly sees no value, which only means "absent".

**Verification:**
- Windows `UnitTestRunner --category=Audio` (Release) on the whole .6 series: `CoreAudio chunk timestamps` 6 of 6, `CoreAudio combined output timestamps` 8 of 8, whole run 288 of 288. The entries below were taken while the other backends' patches were still in progress.
- Windows `UnitTestRunner --category=Audio` (Release, `_build/juce-r7-utr`, other lanes' backend wiring replaced by a throwaway uncommitted fix so the tree compiles): `Callback timestamp alignment` 9/9, `AudioDeviceManager` enforcer cases, `CoreAudio chunk timestamps` 6/6 and `CoreAudio combined output timestamps` 8/8 pass. Failures remaining are the suites of other lanes' stubs (Oboe, ASIO, ALSA, JACK, WASAPI).
- GitHub CI `ci/timestamps-apple`, macos-latest (macOS 26.7.2, Xcode 26.6): `UnitTestRunner --category=Audio` builds (compiling `juce_CoreAudio_mac.cpp`) with no warnings from the changed files; the same suites pass; only the other lanes' suites fail (run 42995599691). iOS simulator compile of `juce_audio_devices` (arm64, iPhoneSimulator26.5 SDK, Xcode generator) succeeds.
- Combined-device integration (run 42997767678): BlackHole 2ch as input and BlackHole 16ch as output, both loaded on the runner after `sudo killall coreaudiod`; 48 kHz, 256 frames, 20 s through `AudioDeviceManager`: 3720 callbacks. After the first second `inputStream` was present in 3532 of 3532 callbacks and `outputStream` in 3392 (96.0%); `processingCycle` never; while an `outputStream` repeated (88 repetitions) its offset fell by exactly the previous `numSamples` every time; 703 spans of at least one second between distinct observations implied 48 kHz within 1% in every case; after a 200 ms block of the application callback the output role was absent in 44 callbacks starting with the next one and returned at the fifth. The runner's virtual devices produced 41 xruns in the 20 s (reported by the combiner, the forced one included), which is why the output role is absent in about 4% of callbacks.
- Combined-device absolute association (macOS CI, BlackHole 2ch in, BlackHole 16ch out, 48 kHz / 256): a one-sample marker written at a known application output frame, captured through BlackHole 16ch's loopback opened as an ordinary input. Over 9 runs, 430 judged markers were captured where the forwarded `outputStream` predicted, within 4.8e-5 frames (the two ns conversions' rounding); with one buffer added to the combiner's output offset (a mutant), every one of 472 markers was off by exactly +256 frames. The tolerance is 0.5 frame, so any whole-frame mis-association fails. BlackHole reports zero device, safety-offset and stream latency, so the rail does not test their handling.

**Limits:** the combiner's name is the output device's name, so the integration check identifies it by two distinct opened devices and by the combiner's xrun counter, not by a "in + out" name. The wiring of the ordinary-device path has no unit test; it is checked by review and by the compile. A real hardware device pair was not run; BlackHole is a virtual driver with no latency of its own beyond its buffering.

P014 (lynco-8.0.12.2 to .5): Projected the CoreAudio output host time across split chunks by adding each chunk's duration at the device sample rate, and gave the combined device only the input host time. Replaced by P014.1 in .6: each chunk now sees the IOProc's own stamps unchanged with an offset of minus its start, and the combined device's output role is forwarded through real FIFO bookkeeping, so no time is advanced by a computed duration.

### P015: Android display area follows the Activity's window, and is refreshed on resume

- **Files:** `modules/juce_gui_basics/native/juce_Windowing_android.cpp`
- **Changes:**
  - **userArea (API 30 and later):** `Displays::findDisplays` takes `getCurrentWindowMetrics()` from the Activity's `WindowManager` instead of the application context's. Before an Activity exists it uses the application's, as before. `totalArea` (the display's real metrics) and the insets (the decor view's) are unchanged. Below API 30 nothing changes.
  - **Resume:** `handleAppResumedCallback` (the existing `onActivityResumed` route of `ComponentPeerView`) calls `forceDisplayUpdate()`. `Displays::refresh` notifies peers only when the displays differ, so a resume that changes nothing resizes nothing.
- **Why:** with a landscape Activity behind a portrait-only lock screen, the display turns portrait while the Activity's window and configuration stay landscape. A layout pass under the lock screen refreshed the cache from the application context and recorded a portrait `userArea` (412x915 against a 914x411 window, Pixel 7, Android 16). On unlock Android sometimes turned the display back with no layout pass and no insets dispatch, which were the only refresh triggers after startup. The Activity's configuration does not change during the lock and unlock, so a configuration-change callback would not fire either. A full-screen peer's bounds are the primary display's `userArea`, so the app was laid out portrait in its landscape window until the next resize.
- **Default behaviour:** Android only. On API 30 and later `userArea` is the Activity's window bounds; these differ from the application context's only when the two disagree (the case above, or in multi-window). Every resume runs one display query.
- **Test evidence:** Tested 2026-10-23 on a Pixel 7, Android 16, with a LynCo app's Android Release build (arm64) built with JUCE at this patch. The app lays out a full-screen peer from `userArea` and `safeAreaInsets`. Its own workaround for this bug (a display listener that requested a layout) was removed. Logged: the cached `userArea`, `totalArea` and insets at every layout, `parentSizeChanged`, `onResume`, the display rotation, content-view layout changes and insets dispatches.
  1. Compiles with no diagnostics from `juce_Windowing_android.cpp` (NDK clang, Release).
  2. Locked in landscape and unlocked, 8 times: every unlock ended on the correct landscape layout. In each screenshot the layout's reference row matched the one taken before the lock pixel for pixel.
     - Under the lock screen the display turned portrait and the hidden window was laid out once: `userArea` stayed 915x412 (landscape, the Activity's window) while `totalArea` went 412x915.
     - In the 5 unlocks where Android kept the app landscape, the resume refresh put `totalArea` back to landscape before any layout change, and no insets dispatch was logged. The display's rotation back was logged about 0.4 s after `onResume`, so the layout was already right because `userArea` never went portrait.
     - In the 3 unlocks where Android resized the window to portrait at resume, the layout followed the portrait window, and then the landscape one when it was rotated back.
  3. Locked and unlocked in portrait: no screen-size notification; the layout stayed the same.
  4. Rotated while unlocked, portrait to landscape and back: each rotation laid out correctly, as before.
  5. Not tested: multi-window or freeform, API 29 and below (unchanged code path), a recreated Activity.

### P016: Report Oboe startup failures and retain error-callback lifetime

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`, its adjacent
  `_test.cpp`, and the runner/instructions in `extras/OboeLifecycleTests`.
- **Startup:** propagate Oboe's `requestStart()` result without imposing a start deadline,
  propagate input/output failure, close partial streams, and pair preparation with a
  stopped notification before reporting the error. The device reports closed/stopped
  state; `AudioDeviceManager` already reads `getLastError()` after starting.
  A slow transition to Started is not a failure. The reported AAudio -899 is
  ErrorDisconnected and requestStart returns it directly. A successful request can
  still fail asynchronously; the output error callback and input read path cover that.
  Latency-support flags are prepared before starting either stream, because callbacks
  can arrive before requestStart returns. No callback/error from a stalled native
  stream cannot be diagnosed here; no timeout, polling or stall detector is added.
- **Lifetime:** shared error callbacks outlive native streams and queued message-thread
  dispatch. A shared control object serializes lifecycle operations and survives device
  deletion. Stopping detaches the session callback; destruction clears its owner. Late
  messages cannot reopen a stopped device or access a deleted owner. Client notifications
  are lifecycle boundaries: the retained control checks ownership and a lifecycle revision
  before continuing after preparation or stopped notifications. Explicit stop/close,
  replacement startup, and destruction cancel the interrupted operation. Terminal callbacks
  may delete the device while the retained control object keeps synchronization valid.
- **Query contract:** `isOpen()` and `isPlaying()` read existing state flags atomically.
  These are independent snapshots, not publication of session or configuration state.
  Lifecycle operations and `getLastError()` are serialized. Never call getLastError()
  from the audio callback: stop holds the
  control lock while waiting for audioCallbackMutex, which the callback holds.
  Other session/configuration getters still require caller serialization with lifecycle
  changes; P016 does not make them safe for arbitrary concurrent queries.
- **Input validity:** missing microphone permission returns before session creation.
  Short, zero and failed reads silence unavailable input frames before referencing or
  converting the full callback block. Timestamp calculations retain the actual read count.
  Scratch buffers cover the requested and both native stream capacities; callbacks do
  not resize them. A callback beyond that capacity emits silence without accessing scratch.
- **Recovery:** preserves JUCE's existing automatic reopen policy for ErrorDisconnected,
  including the lifecycle notifications provided by P010. Input disconnection now uses
  that same path. P016 adds no retry budget, cooldown or delayed retry. A failed reopen
  reports an error; a later disconnection can trigger recovery again.
  Blocking Oboe input has no native error
  callback. A session-owned worker waits on a POSIX semaphore; the serialized audio
  callback posts it at most once, without allocation, locks, logging or message posting.
  The worker forwards to the same retained error callback as output disconnection.
  Stop detaches and quiesces audio before joining the worker; the worker never takes
  the control lock. Semaphore/thread setup failure is reported as startup failure.
  Other read errors (including transient startup errors) only produce zero-filled input.
  ErrorDisconnected remains terminal even before the first successful input read;
  requiring earlier input would hide a disconnected stream that never became live.
  Replaces P010's raw session callback and AsyncUpdater; P012's error forwarding remains.
  Only explicit disconnection uses this recovery; other read errors only zero-fill.
- **Verification:** ARM64 Android Debug builds and Pixel 7 Android 16 execution.
  Natural disconnected-start failures established the reporting defect. Controlled
  real-stream startup tests covered both directions, partial cleanup, notification order,
  repeated close, reopen, and worker-thread callback-triggered deletion. A delayed native
  Bluetooth-disconnect callback was safely delivered after device deletion. Controlled
  disconnect tests verified successful recovery, stopped-device cancellation, and queued
  cancellation after worker deletion. The retained Android hardware suite reproduces the
  previous patch's unwanted reopen after client closure (three failed assertions). The
  b1a60fc0 suite passed 409 assertions with microphone permission and 298 without it:
  recovery cancellation/deletion, client replacement, preparation cancellation, real
  input/output ErrorClosed startup failures, playback state during an actual callback,
  permission rejection and output-only opening, and scratch-buffer bounds. Synthetic
  read results test full/partial/zero/error input for mono/stereo float and int16 buffers.
  Restoring the old buffer, playback-query and permission behaviors fails 133 assertions
  with permission and 134 without; restoring the fixes passes again.
  Recovery errors in this suite are injected at the backend entry point; this
  does not requalify native error timing. Unit-test code is excluded from normal builds.
  The retained suite also covers the input semaphore path from a real audio callback:
  duplicate notification coalescing, non-disconnection errors, cancellation on deletion,
  successful reopen and a failed reopen that reports one terminal error.
  The requestStart/input-disconnection revision at 5833ae0 passed 453 assertions with
  microphone permission and 298 without. The additional open-state regression holds
  the lifecycle lock while a real audio callback calls isOpen(), then checks stop/close.
  Restoring the locking getter fails exactly that contention assertion (457 pass, one
  fail); the runner still exits normally and the Pixel audio-service PID stays unchanged.
  With the atomic getter, the expanded suite passes 458 assertions with microphone
  permission and 298 without, with zero failures.
  A natural Release comparison used Tribit A2DP playback and the Pixel microphone:
  30 launches each at baseline c2a16c6 and 5833ae0. Both hit native -899 on four launches;
  baseline recovered one, P016 all four. Baseline had three silent dead-audio outcomes;
  P016 had none and one separate, visible session-creation failure. Most launches followed
  an audio-service restart caused by force-stop; this is not a clean-launch comparison.
  A separate native recorder naturally disconnected the input while the audio service
  stayed alive. Baseline input froze without an error. P016 attempted one reopen, reported
  its failure once while the recorder still held the microphone, and manual Retry restored
  live input after the recorder exited. Natural automatic-reopen success after a running
  input disconnect remains unverified; injected tests cover that path.
  Application recovery and clean smoke checks are recorded by the consuming application's PR.
  Other Android versions/devices and desktop audio were not qualified. This patch does
  not fix the separately reproduced vendor MMAP process-death cleanup crash.
- **Natural runs at `5ab7720`:** Release on Pixel 7, Bluetooth A2DP output and the
  built-in microphone, 30 force-stop relaunches: 27 live input, 3 visible errors with
  Retry, no silent failure (c2a16c6 had 3). 13 launches hit -899 at start; 12 recovered
  and 1 reported its error. With P017 (`7178a57`), 30 more: 30 live input, no silent
  failure or error; no -899 occurred.
- **Not fixed:**
  - A competing recorder that takes the microphone while the device runs on the
    system default devices with a 48-sample buffer leaves input callbacks delivering
    zeros with no error, also after the recorder exits; c2a16c6 behaves the same. With
    explicitly selected devices and a 1920-sample buffer the same takeover reports an
    error, and one Retry restores input.
  - Device IDs are read when JUCE scans devices. An audio-server restart renumbers
    Android's devices, so a later open or recovery reopen can request an ID that no
    longer exists (Android then picks the route) or that now names another device. In
    one of 30 launches the output opened on the speaker's Bluetooth SCO profile instead
    of A2DP. Upstream behaviour, not changed by P016 or P017.

### P017: Notify clients when an Oboe device is explicitly stopped

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`, its adjacent
  `_test.cpp`, and the instructions in `extras/OboeLifecycleTests`.
- **Why:** WASAPI and CoreAudio notify `audioDeviceStopped()` from `stop()`; Oboe did
  not, so a client never learned that it had been stopped.
- **Contract:** explicit `stop()`, `close()`, and reopening a running device notify
  the detached client exactly once, after native audio is quiescent. Stopped/closed
  state is visible before the notification. Repeated closure sends no duplicate.
  `start(nullptr)` uses the same stop path, including during an active audio callback.
  Restart after stop recreates native streams and their error handler through the
  existing open path before preparing the client. Starting a closed device reports
  an error without preparing it.
  The callback pointer stays stable during processing. Replacement uses the existing
  audio mutex; the audio thread only tries that lock and emits silence on contention.
- **Lifetime:** reuse P016's retained control and lifecycle revision to allow client
  stop, close, destruction, or replacement during notification. A superseded outer
  open returns a cancellation error without overwriting the replacement or its error.
  Internal teardown remains silent so
  startup failure and disconnection recovery retain their existing single stopped
  notifications. Destructor teardown remains silent, as before.
- **Scope:** a separate callback-contract correction on top of P016. No changes to
  its startup/error reporting or JUCE's recovery policy; no retry limit or timeout.
- **Verification:** ARM64 Debug on Pixel 7, Android 16. The explicit-stop tests on
  unchanged P016 (`5ab7720`) reproduced 41 failed assertions (577 passed), all in
  the added notification/replacement cases. With this patch and the additional
  restart, callback-identity and cancelled-open regressions, 759 assertions passed
  with microphone permission and 562 with permission denied, with zero failures.
  The held-callback tests check completion of the client invocation, not its release
  signal. Both phases exited normally;
  the Pixel audio-service PID stayed unchanged throughout the comparison.
  The suite covers direct stop/close, repeated closure, callback-visible state,
  recursive lifecycle calls, destruction, replacement, and existing P016 failure
  and recovery cases. Other Android devices and versions were not qualified.
  A Release app on the same phone: selecting no input and no output stops callbacks
  and the client is notified; selecting the defaults again restores live input.

### P018: Associate converted Oboe timestamps with application frames

- **Files:** bundled Oboe `FilterAudioStream.{h,cpp}` and
  `DataConversionFlowGraph.h`; standalone regression in `extras/OboeTimingTests`.
- **Why:** scaled child counters are not application delivery cursors. A 96 kHz
  application can receive two blocks during one 48 kHz child callback; both used
  the same timestamp target. Buffered input reads have the corresponding defect.
  Fractional conversion also discarded part of the timestamp pair, and the pair
  omitted the causal sample-rate converter delay.
- **Contract:** count callback-output frames when supplied by the application and
  input frames when consumed by it. Preserve the existing child counters for the
  opposite sides and for blocking output. Project native timestamp pairs through
  the converter's source-frame delay, then align the position once to the nearest
  application frame, n = floor (x + 1/2) (a half-frame residual rounds up), with
  the time moved by (n - x) / application rate rounded to the nearest ns, in
  `FilterAudioStream::getTimestamp`. x = (child position * app rate - delay *
  child rate) / child rate for output and (child position + delay) * app rate /
  child rate for input, in exact integer arithmetic for the converter's
  whole-frame delay (taps/2); a fractional delay would fall back to doubles. This
  is the rule of `CallbackTimeHelpers::alignToApplicationSample` (P007.1); bundled
  Oboe cannot include JUCE, so each copy's comment names the other and both are
  pinned by their tests. Failed native timestamp queries remain failures. No
  clock estimator, recovery policy, retry or application lifecycle changes.
- **Verification:** Android arm64 Release with NDK 28.1.13356709, Pixel 7.
  `extras/OboeTimingTests` (test source and commands there): checks=158
  failures=0, with and without `--require-read-errors` (the latter with P019).
  The checks cover integer/fractional/equal rates, buffered/short/empty reads,
  counter continuity, failed queries, smooth pulse timing through all five
  conversion qualities (expected pulse timing independent of the wrapper
  counters; largest measured discrepancy 0.006 native frame, tolerance 0.02) and
  the nearest-frame alignment of every frame, ties included. The unaligned
  (floor) revision fails 30 of the 158 checks, and one with ties rounded down
  fails 20, exactly the tie frames. A separate Release recorder using the first
  revision's production changes captured 23,461 callbacks across 44.1/48/96 kHz
  on Pixel speaker/microphone, with no adjacent repeated/backward available
  context timestamps and no recorder loss/errors. A lab app on a tree with this
  revision, 44.1 kHz: see P013.1.
- **Limits:** counter restart coverage uses deterministic streams. Physical
  restart/flush behavior, blocking output writes, acoustic ADC/DAC timing, other
  devices and an application consuming these times are not qualified by these tests.
  This does not validate arbitrary native timestamps. The delay model (taps/2) is
  bounded by the pulse test at 0.02 native frames; if the true group delay were
  fractional, x and so the nearest frame could differ. The conversion graph's
  swallowing of a native `ErrorDisconnected` read, reproduced by
  `--require-read-errors`, is repaired by P019, not here.

New in .6. Revised before publication: the first revision floored the converted
position and kept the fraction in the time; it now aligns to the nearest frame
once, as above.

### P019: Surface native read errors through the Oboe conversion graph

- **Files:** bundled Oboe `AudioSourceCaller.{h,cpp}` and
  `DataConversionFlowGraph.cpp`.
- **Why:** an input stream that needs rate or format conversion is wrapped in
  `FilterAudioStream`. A blocking native read that failed (for example
  `ErrorDisconnected`) became a negative byte count inside the flowgraph, the
  sink treated it as an empty pull, and the caller saw a successful zero-frame
  read. A client that detects disconnection only from a failed read (P016) then
  receives silence forever without an error. Upstream Oboe has the same code.
- **Contract:** `AudioSourceCaller` records the Result of a failed native read.
  `DataConversionFlowGraph::read` clears it on entry and, when the pull delivered
  no frames and a read failed, returns that Result as the negative value that
  `FilterAudioStream::read` already maps to an error. Frames delivered earlier in
  the same call are returned as a normal partial read; the failure is not
  latched, so it is reported by the next read, which hits the same native
  stream. This matches `AudioStream::read`, which returns a frame count for a
  partial transfer and an error otherwise. No retry, timeout or lifecycle change;
  the callback and output paths are unchanged.
- **Verification:** `extras/OboeTimingTests`, Android arm64 Release, NDK
  28.1.13356709, Pixel 7. On the P018 base, `--require-read-errors` fails
  (`read_error_contract=FAIL`, converted result 0 for native -899). With this
  patch it passes (converted result -899) and the normal run still passes
  108/108 checks. With P018's nearest-frame revision the suite has 158 checks and
  passes with and without `--require-read-errors`.
- **Limits:** verified with deterministic child streams, not a physical device
  disconnect. A block-aligned read that fails after part of an application block
  was copied discards that partial block (it was never counted as delivered).
  Application-level recovery is a separate concern.

New in .6.

### P020: iOS forwards its render cycle time

**Files:** `modules/juce_audio_devices/native/juce_Audio_ios.cpp`. Uses `CallbackTimeHelpers::getBufferStartTimestamp` from `audio_io/juce_CallbackTimeHelpers.cpp` (P014.1).

**Why:** the iOS render callback receives an `AudioTimeStamp` for the buffer it is asked to render. It says when that cycle's buffer starts on the host clock, and nothing about when the input was captured or when the output will be heard (which depends on hardware and session latency that the callback does not carry). Forwarding it as the processing cycle states exactly that and no more.

**Contract:** `processingCycle` is present when the render stamp has `kAudioTimeStampHostTimeValid`, at `sampleOffset` 0 with `nativeFlags` 0. Role: processingCycle. Native source: `AudioTimeStamp.mHostTime` of the AURenderCallback's stamp. Reference point: the first frame of the buffer being rendered. Observation, not prediction. Clock: `mach_absolute_time` ticks converted to ns by the same `HostTimeToNanos` conversion stock JUCE uses for `hostTimeNs` (`mach_timebase_info` numerator/denominator applied to the 64-bit tick count), so it is the `Time::getHighResolutionTicks()` timeline with that conversion's own integer rounding. `inputStream` and `outputStream` stay absent. `hostTimeNs` is stock: the same host time, present under the same condition, as before this patch.

**Verification:** the seam `getBufferStartTimestamp` is covered by the suite `CoreAudio chunk timestamps` (Windows and macOS `UnitTestRunner --category=Audio`). The iOS wiring compiled for the iOS simulator (arm64, iPhoneSimulator SDK, Xcode generator, CMake `CMAKE_SYSTEM_NAME=iOS`, target including `juce_audio_devices`): see the CI run recorded in the P014.1 ledger. No device run.

**Limits:** no iOS device or simulator run of the callback; the wiring has no unit test.

New in .6.

### P021: ASIO forwards each buffer switch's own time

**Files:** `modules/juce_audio_devices/native/juce_ASIO_windows.cpp` (`callback`, `processBuffer`, `readSwitchTime`, `toSwitchTime`, `identifyClockOffset`, `collectSystemTime`, `getInterruptTimeNs`, `asioMessagesCallback`, the two switch callbacks, `open`, `close`), and the bodies of `CallbackTimeHelpers::AsioBlockTimes` and `identifyAsioClockOffset` in `audio_io/juce_CallbackTimeHelpers.cpp`. `getAsioProcessingCycle` and the backend's old `getProcessingCycle` / `measureClockOffset` are gone.

**Why:** An ASIO driver stamps each buffer switch with a (samplePosition, systemTime) pair. It is the driver's own time for the block, not a capture or presentation time, so it travels as `processingCycle` and makes no claim about when samples crossed a converter.

**Contract:**
- **Role:** `processingCycle` only; `inputStream`, `outputStream` and `hostTimeNs` are absent (stock: ASIO never set `hostTimeNs`).
- **Native source:** JUCE now answers `kAsioSupportsTimeInfo` with 1, and each switch's `ASIOTime` from `bufferSwitchTimeInfo` supplies `systemTime`, `samplePosition` and flags for that switch. A driver that calls the plain `bufferSwitch` is asked with `ASIOGetSamplePosition` instead (success stands for both valid flags; no change flags exist). `kSystemTimeValid` or `kSamplePositionValid` cleared, or a failed call, is absent for that callback. `kSampleRateChanged` or `kClockSourceChanged` sets `nativeDiscontinuity` on that switch only.
- **Reference point / observation:** the driver's time of the buffer switch (observation). The offset is not always 0: it is the pair's `samplePosition` minus the tracked start of the current block (`AsioBlockTimes`). The first valid position of a run anchors the tracking; every later switch advances the tracked start by the block size (an invalid position still counts). A valid position is reconciled when it is not before the newest position seen, not after the tracked start, and a whole number of blocks before it; then the offset is position minus tracked start: 0 for the switch's own pair, minus one block for a stale fallback pair (the previous switch's pair repeated), and so on. A position that is not reconciled (a rewind, a pair ahead of the tracking, whether the next switch's pair read early or missed switches, or one not a whole number of blocks away) ends the association: that switch is absent, the next switch with a valid position anchors the tracking again (offset 0), and the first observation delivered after the loss carries `nativeDiscontinuity`. Absent before a valid position in the run. Known limit: an anchoring pair is taken to be its own switch's, so a stale anchor gives offsets whole blocks too high until a pair ahead of the tracking re-anchors it.
- **Clock rule (once per run):** the driver's first eight system times are compared with three clocks read back to back at the instant `timeGetTime()` changes: QPC, interrupt time (`QueryInterruptTimePrecise`, x100 ns, looked up at run time) and `timeGetTime()` x 1e6. If all eight times are whole milliseconds the clock is `timeGetTime`; otherwise interrupt time is checked first, then QPC. A candidate matches if the newest driver time is within `maxAge` before it (`maxAge` = 2 blocks + 32 ms: a block's age, the wait below, a timer tick of spinning, and the interrupt clock's lag of up to a tick). The offset is QPC minus the matching candidate. No match: the role is absent for the run. If QPC and interrupt time both match (the two are closer than `maxAge`, e.g. a PC that has not slept since boot), the clock is taken to be interrupt time (SPEC "ASIO ambiguous clock"); a driver that really uses QPC is then off by the measured |interrupt time - QPC| gap, constant for the run and at most `maxAge`. The measurement waits one block plus 1 ms after the last collected time, because a switch that follows another at once (a driver catching up) can be given a time up to a block ahead of its arrival (measured: +4 to +5 ms at 256 frames), and the identification needs a driver time not later than the clocks. The identification runs in `open()` after the driver has started and before `isStarted` is set, so before the first callback is delivered; it never runs on the callback thread.
- **`timeGetTime` wrap:** a `timeGetTime` clock is a 32-bit millisecond count, so a driver's time on it falls back to 0 every 2^32 ms (49.7 days of uptime). Each switch's time is unwrapped exactly (`unwrapTimeGetTimeNs`: the whole number of 2^32 ms periods that puts it within half a period of the previous switch's unwrapped time; for the first switch after an identification, of the `timeGetTime` value the offset was measured against), so the count continues across any number of wraps and the offset stays the constant it was measured as. Only for a clock identified as `timeGetTime`; QPC and interrupt time are 64-bit.
- **Clock source change and identification failure:** the first identification stays in `open()`, after the driver has started and before `isStarted`. Every later identification (after a clock-source change) and every retry (after any failed identification, including that first one: fewer than eight times in 2 s, no tick edge, no matching clock) runs on a background thread the device owns, never on the message thread and never on the callback thread; there is no limit on attempts, with a pause of 250 ms between them. On the transition of `kClockSourceChanged` (set on this switch, not on the previous one; a flag held set over consecutive switches is one request) the callback thread does one atomic increment of a clock generation count, which makes the held offset stale (role absent) at once and is the whole request: no system call, no `AsyncUpdater`. The background thread polls the count every 10 ms. An identification publishes its offset, then the generation it started under; a newer transition makes a result in flight stale and it is discarded. `close()` stops and joins the thread before the driver is released, and `open()` resets the generations, so nothing crosses a run. The flagged switch is delivered with `nativeDiscontinuity`, only if an offset was still held.
- **Rounding:** none beyond the QPC-to-ns unit conversion (exact for the 10 MHz QPC); interrupt time x 100 and timeGetTime x 1e6 are exact. No per-callback fitting, smoothing or rate correction; no reading is compared with callback arrival once identified.
- **Not done:** no re-measurement within a run except after a clock-source change or a failed identification.

**Verification:** Windows `UnitTestRunner --category=Audio` (Release, `_build/juce-r6-utr`, commit `661880267f`): `ASIO processing cycle timestamps` 17 of 17, whole run 288 of 288 cases, exit 0; the same on the whole .6 series. Hardware (Focusrite USB ASIO, loopback cable out 1 to in 1, lab built from `661880267f`, no `-dirty`), on a PC whose interrupt time is about 118 s behind QPC: `processingCycle` in 100 % of callbacks in every run, offset 0, flags 0; the interrupt-time offset was applied (converted time within milliseconds of callback entry, not 118 s). 592.9 s run at 48 kHz / 256 (a foreground run is capped at 10 minutes, so 592 s): least-squares slope -1.53 ppm over all callbacks (-1.26 ppm from 120 s), but the tight edges are flat: 90th, 99th and 99.9th percentile per 20 s bin -0.16, -0.46 and -0.08 ppm. The driver was far jitterier in this session than on the earlier runs (repeated times in 32.6 % of callbacks against 1.6 %, median 4.4 ms before entry against 0.6 ms, spread 5 ms against 0.5 ms), which is why the least-squares slope is above 1 ppm; an A/B on the same driver state with the pre-change code (`950f871a35`) gave the same figures (repeats 32.6 % vs 33.2 %, median -4.40 vs -4.49 ms, spread 5.29 vs 5.37 ms over 60 s), so it is the driver's state today and not this change. ASIO4ALL (48 kHz / 256, 120 s): identified as `timeGetTime`, present 100 %, least-squares slope -0.79 ppm (also noisier than the earlier -0.04 ppm run: residual sd 1.8 ms against 0.5 ms). The clock-source switch check was not run: it needs a clock source to change in the driver's panel, and I did not open the panel or change any driver setting. The unwrap and the re-identification thread's retry and switch paths are covered by the seam cases and by review only; no clock-source change, wrap or failed identification occurred on the device. Data: `hw-windows/data-r3/` (`asio_drift256`, `a4a120`, `ab_*`).

**Limits:** The Focusrite driver's time info is jittery: in the earlier sessions about 1.6 % of switches repeated the previous time and 1.9 % went backwards (spacing sd 2.3 ms) while the sample positions advance normally, and in the latest session a third repeated, so the offset is 0 in every callback. That is the driver's own timing, forwarded unchanged, so `processingCycle` is not a low-jitter time on that driver (click arrival against it has a robust sigma of 3 to 4.4 ms over 60 s). A `timeGetTime` clock is quantised to the timer period, so its times are up to a period earlier than the true switch; the conversion does not add to that. If the driver actually uses QPC and QPC is within `maxAge` of interrupt time, the identified interrupt-time offset leaves a constant error of that gap (at most `maxAge`) for the run. The block tracking cannot tell a stale anchoring pair from a good one (see the contract), and a real missed switch costs one absent switch and a flagged re-anchor. An identification whose driver times straddle a `timeGetTime` wrap matches no candidate and is retried. Between a clock-source change and the next identification (tens of milliseconds to seconds, or longer if the driver pauses its callbacks) the role is absent.

New in .6. Revised before publication: the first draft forwarded `ASIOGetSamplePosition`'s `systemTime` at offset 0, treating the driver's clock as QPC unless every time was a whole millisecond. On the Focusrite driver `systemTime` is on the Windows interrupt-time clock (118.19 s from QPC on the test PC), and about 12.8 % of back-to-back switches were handed the previous switch's pair, so the time was both on the wrong clock and a block early; the time info the driver delivers with each switch is now used, and the clock is identified against QPC, interrupt time and `timeGetTime`.

### P022: ALSA forwards device timestamps

**Files:** `modules/juce_audio_devices/native/juce_ALSA_linux.cpp` (`ALSADevice`, `ALSAThread::run`); `audio_io/juce_CallbackTimeHelpers.cpp` (`getAlsaTimestamp`).

**Why:** A direct ALSA PCM reports, in one `snd_pcm_status` call, how many frames are available and the time of the hardware pointer update they belong to. That is a native observation of the device ring that no other JUCE path exposes. Forwarding it lets a consumer relate the device's own timeline to the callback's buffer without estimating latency.

**Contract:**
- Roles: `inputStream` and `outputStream`, each from its own PCM. `processingCycle` and `hostTimeNs` stay absent/stock (`hostTimeNs` is `nullptr`).
- Native source: `snd_pcm_status` -> `avail` and `htstamp`, in one call (one coherent observation). Reference point: the frame at the hardware pointer, expressed against the application cursor (frames the application has read from, or written to, that PCM since it was opened). Observation, not prediction.
- Capture: the reading is taken right after the buffer is read, so the pointer is at `framesRead + avail` and the buffer began at `framesRead - numSamples`; the offset is `avail + numSamples` (>= 0). Playback: the reading is taken right before the buffer is written, so the pointer is at `framesWritten - (ring - avail)` and the buffer begins at `framesWritten`; the offset is `-(ring - avail)` (in (-ring, 0]). `ring` is the hardware buffer size.
- Clock: `tstamp_mode` is set to ENABLE and `tstamp_type` to `SND_PCM_TSTAMP_TYPE_MONOTONIC` when the PCM is configured, then read back from the PCM's software parameters. Only if it reads back MONOTONIC are readings forwarded (SPEC ruling 7: one configured type; otherwise the role is absent). CLOCK_MONOTONIC is JUCE's clock on Linux, so seconds and nanoseconds convert to `timeNs` exactly (`s * 1e9 + ns`, no rounding).
- Absent when: the PCM is not a direct hardware PCM (`snd_pcm_type != SND_PCM_TYPE_HW`: `default`, `plughw`, `dmix`, `plug`, ... may convert rates or share the ring, so no application-frame association exists); the state is not RUNNING (before start, after an xrun, after recovery until the stream runs again); the status time is zero (not stamped); `avail` is outside `[0, ring]`; for capture, the latest read returned fewer than `numSamples` frames or an error.
- `nativeFlags` is always 0: ALSA's status carries no qualification that maps to the interface flags.
- Lifecycle: the application cursor starts at 0 when the PCM is opened (each `open` of the device creates new `ALSADevice` objects), so nothing crosses a stop/start.
- Real-time cost: one `snd_pcm_status` ioctl per role per callback, on the stack.

**Verification:** Windows `UnitTestRunner --category=Audio`: suite `ALSA device timestamps` 5 of 5 cases pass. WSL Ubuntu (ALSA and JACK enabled): same suite 5 of 5. On the whole .6 series (Windows, Release): the same suite passes, whole run 288 of 288. `snd-aloop` is not available in the WSL kernel (6.18.33.2-microsoft-standard-WSL2 ships no module), so no loopback run was made here.

**Limits:** Only `hw:` devices; a plugin device never reports, even where its slave is a hardware PCM. Short reads/writes after an xrun make the affected callbacks absent. The status `tstamp` is the time of the last hardware pointer update (interrupt-driven for most drivers); its precision is the driver's.

New in .6.

### P023: JACK forwards its processing-cycle time

**Files:** `modules/juce_audio_devices/native/juce_JackAudio.cpp` (`jack_get_cycle_times` and `jack_get_time` resolved with `juce_loadJackFunction` into `JackAudioIODevice` members when the client is opened; `JackAudioIODevice::process`, `readClocks`); `audio_io/juce_CallbackTimeHelpers.cpp` (`JackClockReadings`, `getJackProcessingCycle`).

**Why:** `jack_get_cycle_times` gives the start of the cycle the process callback is running in, as the server timed it. Forwarding it as `processingCycle` at offset 0 lets a consumer tie the callback's buffer to the server's own cycle time.

**Contract:**
- Role: `processingCycle` only (JACK has no per-stream device timestamps); `inputStream`/`outputStream` absent; `hostTimeNs` stock (`nullptr`).
- Native source: `jack_get_cycle_times` -> `current_usecs`. In jackd2 this is `fCurrentWakeup`, the server's DLL prediction of the cycle start, not a measurement. It is a native prediction, forwarded as the server gives it; it can run slightly ahead of the callback, or a period or two behind it when a timed driver runs a late cycle back to back with the next.
- Reference point: the first frame of the process buffer, so `sampleOffset` is 0. `nativeFlags` is 0.
- Clock rule, applied on every callback, nothing carried between callbacks: CLOCK_MONOTONIC, `jack_get_time()` and CLOCK_MONOTONIC_RAW are read back to back, in that order (`readClocks`). JACK's clock is whichever of MONOTONIC and RAW `jack_get_time() x 1000` is closer to (a tie is MONOTONIC: no conversion when the readings cannot tell). `timeNs = current_usecs x 1000 + (MONOTONIC - that clock's reading)`: the microsecond count converts exactly (x 1000), and the added term is zero for a MONOTONIC server and (MONOTONIC - RAW) for a RAW server, in integer ns. If `jack_get_time()` is more than 1 ms from both clocks the role is absent for that callback. No once-per-run offset, no smoothing. On platforms other than Linux and BSD the readings are never close and the role is absent.
- Error bound: the conversion adds the difference between two clocks read a few tens of ns apart, so its error is the read spacing (three reads inside one process callback, normally under a microsecond, longer if the thread is preempted between them) plus the slew of the two clocks relative to each other over one cycle (the difference changes by microseconds per second, so well under a microsecond over a 5 ms cycle). On top of that sits the prediction's own error, which is the server's.
- Absent when: this libjack lacks `jack_get_cycle_times` or `jack_get_time` (the pointers, resolved at open and off the audio thread, are null; nothing is called and no time 0 is made up), `jack_get_cycle_times` returns nonzero, or `jack_get_time()` matches neither clock within 1 ms.
- Real-time cost: one `jack_get_cycle_times`, one `jack_get_time` and two `clock_gettime` calls per process callback (vDSO, no syscall).

**Finding (jackd2):** jackdmp 1.9.22 (Ubuntu) stamps cycles with CLOCK_MONOTONIC_RAW (jack2 `JackLinuxTime.c`). MONOTONIC minus RAW is the accumulated NTP slew since boot (68 ms and 338 ms seen on WSL at different times, 425 to 451 ms on the GitHub Ubuntu images), so no fixed offset works and a 1 ms check against MONOTONIC alone never passes on a jackd2.

**Verification:** Windows `UnitTestRunner --category=Audio`: suite `JACK processing cycle timestamps` 7 of 7 cases pass, and 7 of 7 on the whole .6 series (Windows, Release, whole run 288 of 288). WSL Ubuntu (ALSA and JACK enabled, jackd2 1.9.22): same suite 7 of 7 after the missing-symbol change (F4); harness `ts-harness jack 20 require-present` against `jackd -d dummy -r 48000 -p 256`: ALL PASS, processingCycle present in 100% of 3750 callbacks, offset 0, flags 0, increasing, cycle time minus callback entry within -0.68 to +0.13 ms, spans within 1% of 48 kHz, `hostTimeNs` null, MONOTONIC - RAW = 68.2 ms printed and applied. Missing symbol: the harness run against a copy of libjack.so.0 with the name `jack_get_cycle_times` byte-edited to `jack_get_cycle_timez` (so `dlsym` fails; copy on `LD_LIBRARY_PATH`, not committed) reports processingCycle present in 0.0% of callbacks.

**Limits:** A libjack predating `jack_get_cycle_times` gets no role. The value is JACK's prediction, not a device or kernel timestamp. A server whose clock matches neither MONOTONIC nor RAW within 1 ms gets no role. Under heavy host overload JACK's prediction itself can step; that is the server's value and is forwarded unchanged.

New in .6. Revised before publication: the first draft checked once per activation that `jack_get_time()` agreed with CLOCK_MONOTONIC within 1 ms, and so left the role absent on every jackd2. Replaced in place by the per-callback rule below (SPEC "JACK clock: ruling 8 replaced").

### P024: Android file flushes no longer leak JNI global references

- **Files:** `modules/juce_core/native/juce_Files_android.cpp`.
- **Why:** `FileOutputStream::flushInternal()` created a `SingleMediaScanner` on every flush and never deleted it. The object's own `invocationHandler` global reference kept its Java handler reachable, so the finalizer that would delete it never ran. Each flush leaked three JNI global references (`javaSubClass`, `invocationHandler`, `msc`). A process that flushes often overflows ART's global reference table (about 51,200 entries) and aborts: a stream flushed every 2 s exhausts it in about 9.5 h, and several such streams proportionally sooner.
- **Change:** the flush still runs `fsync` and still asks the media scanner to rescan the file, as upstream does, but through the static `MediaScannerConnection.scanFile (Context, String[], String[], OnScanCompletedListener)` with no listener. That call creates no native proxy and holds no global reference. A Java exception from the call is cleared. `SingleMediaScanner`, `MediaScannerConnectionClient` and the unused instance members of the `MediaScannerConnection` JNI class are removed; they had no other users.
- **Default behaviour:** unchanged apart from the leak: the same file is scanned after every flush. Apps that do not flush are unaffected.
- **Verification:** Android arm64 compile of `juce_core.cpp` (NDK 28.1.13356709, API 29, `-std=c++20 -Wall -Wshadow-all -Wunused-function`): no diagnostics. Device: on an Android 15 x86_64 emulator, a release app recording to two files read JNI global references (`adb shell debuggerd -j <pid> | grep -i globals=`) at start and about 31, 93 and 155 s in. Without the patch: 1439, 1553, 1757, 1944, about 194 per minute. With it: 1433 at every reading. The media scanner still ran after every flush (logcat `MediaScannerConnection: Scanned`).

### P025: Android leaves kiosk mode without leaving full screen

- **Files:** `modules/juce_gui_basics/native/juce_Windowing_android.cpp`.
- **Why:** leaving kiosk mode always took the peer out of full screen, even when it had been full screen before kiosk mode, as an Android app's main window normally is. That removed the view from the Activity and re-added it through the window manager. `Desktop` then restored the bounds saved on entering kiosk mode, which are stale after a rotation. Getting back to full screen re-added the view a second time. An app that shows the system bars only in some orientations had a visible jump on every rotation: a cut-off frame, several black frames, then the right layout.
- **Change:** the Android `Desktop::setKioskComponent` records whether the peer was full screen when it enters kiosk mode. If it was, leaving kiosk mode keeps it full screen and only shows the system bars; the view is not re-added, and `Desktop`'s restore of the pre-kiosk bounds becomes a no-op. A peer that was not full screen before kiosk mode leaves it exactly as before.
- **Default behaviour:** a window's state after kiosk mode now matches its state before it. An app that called `setFullScreen (true)` after leaving kiosk mode to get back to full screen still works; the call is no longer needed. Leaving kiosk mode no longer resizes the window, so the system bars reappearing changes only the display's `safeAreaInsets`. JUCE reports that, as it always has for an inset-only change, through `parentSizeChanged()` on the top-level component, not `resized()`. An app that lays out from the insets only in `resized()` must also lay out again in `parentSizeChanged()`. Stock JUCE's extra resizes on leaving kiosk mode used to hide that gap: after a rotation the app kept the previous orientation's insets.
- **Verification:** Android arm64 syntax compile of `juce_gui_basics.cpp` (NDK 28.1.13356709, API 29, `-std=c++20 -Wall -Wshadow-all -Wunused-function`): no new diagnostics. Device: Pixel 7, an app that leaves kiosk mode on turning to portrait and enters it on turning to landscape. Rotated by hand both ways, it showed a visible two-step jump without the patch and a single step with it (owner's by-eye check, Debug build).

### P026: Android views covered by OpenGL no longer paint in software

- **Files:** `modules/juce_opengl/native/juce_OpenGL_android.h`, `modules/juce_core/native/juce_JNIHelpers_android.h` (adds `View.setWillNotDraw` to the `AndroidView` JNI class).
- **Why:** an `OpenGLContext` attached to a window's top-level component puts a `SurfaceView` over the whole peer view. The GL context's cached image keeps JUCE's own repaints away from the peer, but Android still calls the peer view's `onDraw` whenever it redraws the view itself: at launch, on layout, and when the view first takes focus, which a touched control causes. Each call painted the whole component tree in software, under a surface that hides it. On a Pixel 7 (Debug build) that was 270-400 ms on the main thread, a visible freeze on the first touch after launch.
- **Change:** when the GL context's component is the peer's own component, the context marks the peer view `setWillNotDraw (true)` on attaching and clears it on detaching, so Android skips the view's `onDraw`. Child views, including the GL surface, still draw. A context on a child component, which leaves part of the peer uncovered, changes nothing.
- **Default behaviour:** unchanged on screen, apart from the moment before the GL surface's first frame, when the window background shows instead of the software paint.
- **Verification:** Android arm64 syntax compile of `juce_opengl.cpp` and `juce_gui_basics.cpp` (NDK 28.1.13356709, API 29, `-std=c++20 -Wall -Wshadow-all -Wunused-function`): no new diagnostics. Device: Android 15 x86_64 emulator, an app whose top-level component carries a GL context, with a log line in the paint callback. Without the patch it painted the full window in software at launch (1124 ms, Debug) and on the first touch (190 ms). With it, no software paint at launch, on the first touch, or through rotation and HOME/relaunch, and the screens were unchanged. Pixel 7 (Debug arm64, `dumpsys gfxinfo`, three cold launches each, one tap on an empty part of the panel after 7 s): slowest launch frame 500 ms without the patch, 150 ms with it; first-touch frame 250/300/250 ms without, 24/16/81 ms with. The owner, three cold launches on the Pixel: no lag felt on the first touch. Still owed: her look at launch for a background flash.
