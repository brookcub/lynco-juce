# LynCo JUCE patches

This branch is upstream JUCE plus a short series of LynCo patches, one commit each.

Base tag: `8.0.12` = `29396c22c93392d6738e021b83196283d6e4d850`
Upstream: https://github.com/juce-framework/JUCE

## Rule

Brooklyn (2026-10-19):

> "never mix our changes into juce, keep them visible and separable, and do not build individual product policy/configuration into the juce changes because all lynco apps must share the same juce version with the same patches."

Each patch is one commit, subject `LynCo: <purpose>`, trailer `LynCo-Patch: Pnnn`.

This file describes the JUCE problem each patch addresses and what it changes. It does not say which app needed a patch; each app records that in its own decision log.

## Upgrade procedure

1. Create a branch from the new upstream tag and cherry-pick the series onto it, in order.
2. Drop a patch if upstream has fixed the problem it addresses.
3. Rebuild and retest every LynCo app against the branch.
4. Tag the result `lynco-<juce>.<n>`. Tags are never moved; a fix gets a new tag.

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
  4. Build without P006: `git checkout <P006>^ -- modules/juce_gui_basics/native/juce_Windowing_android.cpp` (`<P006>` is the commit with trailer `LynCo-Patch: P006`), rebuild as in step 3, copy the APK aside as `without-P006.apk`, then `git checkout HEAD -- modules/juce_gui_basics/native/juce_Windowing_android.cpp`.
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

### P007: Callback context carries the input sample time

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioIODevice.h`
- **Changes:** adds `AudioIODeviceCallbackContext::inputHostTimeNs` (`const uint64_t*`, default `nullptr`) and documents what both time fields mean. `hostTimeNs` is the time of the first output sample of the callback, or of the first input sample when there is no output, and null when that cannot be established even if there are outputs. `inputHostTimeNs` is the time of the first input sample actually captured into the callback, and null when the device gives none or the first sample was not captured (for example padding). Both are nanoseconds on the timeline of `Time::getHighResolutionTicks()`, are the times the OS or driver reports with no latency or bias correction, and converting a device's position and time pair to the callback's first sample is part of the contract while guessing a latency is not.
- **Why:** the context had one time with a vague comment, and a device with inputs and outputs has two different first samples: the input was captured before the output will be heard.
- **Default behaviour:** nothing fills the new field yet, so there is no behaviour change; every existing backend leaves it null. Declaration and documentation only, every platform.
- **Test evidence:** the aggregate initialisation in `juce_CoreAudio_mac.cpp` names only the first member and is unaffected; every other user of the struct only receives or forwards it. `UnitTestRunner` builds on Windows with the change (Release, MSVC), and `UnitTestRunner --category=Audio` passes.

### P008: Split callbacks get advanced times

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp`
- **Changes:** `CallbackMaxSizeEnforcer`, which splits a device callback longer than the device's buffer size into blocks, keeps the device's sample rate from `audioDeviceAboutToStart` and, for every block after the first, passes a context whose non-null `hostTimeNs` and `inputHostTimeNs` are the original value plus `position * 1e9 / sampleRate` rounded to the nearest nanosecond. A null time stays null; the first block gets the original pointers. The values live in locals of the callback, so nothing is allocated. The `AudioDeviceManager` unit tests gain four cases and the mock device offers a buffer size of 512 and can make a callback.
- **Why:** the same context was passed to every block, so the second and later blocks carried the time of the first block's first sample.
- **Default behaviour:** unchanged whenever a callback is not split (one block, the original pointers). Every platform that delivers `hostTimeNs`, or `inputHostTimeNs` from P007.
- **Test evidence:** `UnitTestRunner` built out of tree with `cmake -S <JUCE> -B <build> -DJUCE_BUILD_EXTRAS=ON`, then `cmake --build <build> --target UnitTestRunner --config Release`, run as `UnitTestRunner --category=Audio`. Tested 2026-10-23, Windows 11, MSVC (Visual Studio 18).
  1. With this patch: exit code 0, `All tests completed successfully`, 205 `Completed tests` lines, no `!!!` lines.
  2. With only the `blockContext` argument in `CallbackMaxSizeEnforcer` changed back to `context` (the tests kept): exit code 1; the two-block times case fails 2 checks, the one-time-null case 2 checks and the rounding case 4 checks. The case with both times null passes on the old code too, because it guards that the fix does not invent a time.

### P009: WASAPI callbacks carry device times

- **Files:** `modules/juce_audio_devices/native/juce_WASAPI_windows.cpp`
- **Changes:** fills `hostTimeNs` and `inputHostTimeNs` (P007) in the WASAPI backend, in ns on the QPC timeline.
  - **Input:** `GetBuffer` is asked for the QPC time of each packet's first frame. A small helper, `InputTimeline`, keeps the end time of the newest queued packet and how many of the newest queued frames are timed with no gap between them. A callback's input time is that end time minus the frames queued, which is the time of the oldest queued frame. It is null when the callback starts with padding, or when any queued frame is from a packet flagged `AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR` or queued before one flagged `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`, until those frames have been taken.
  - **Output:** the device gets `IAudioClock` from the client, counts every frame it releases to the render client (start-up silence included), and before each callback reads `GetPosition` and converts it with `GetFrequency` (not necessarily frames). The time of the callback's first output frame is the clock's QPC time plus (frames released minus position in frames) at the sample rate. It is null if there is no clock or `GetPosition` fails or returns `S_FALSE`.
  - **Run loop:** `hostTimeNs` is the output time when the callback has output channels and the input time otherwise; `inputHostTimeNs` is the input time. Both live in locals of the loop; nothing is allocated or locked.
- **Why:** the callback context had no times on Windows. Callback entry on WASAPI has 0.4 to 4.7 ms of jitter, against 0.25 to 0.3 ms for the device's packet time.
- **Default behaviour:** an app that ignores the two fields is unaffected. The only other change in the callback path is one `GetPosition` call per callback with an output and a count of released frames.
- **Test evidence:** `UnitTestRunner` built out of tree with `cmake -S <JUCE> -B <build> -DJUCE_BUILD_EXTRAS=ON`, then `cmake --build <build> --target UnitTestRunner --config Release`, run as `UnitTestRunner --category=Audio`. Tested 2026-10-23, Windows 11, MSVC (Visual Studio 18).
  1. With this patch: exit code 0, `All tests completed successfully`, 219 `Completed tests` lines (205 before, plus 14 `WASAPI timestamps` cases), no `!!!` lines.
  2. Each of these changes to the helper, with the tests kept, gives exit code 1:
     - the newest packet's first-frame time instead of the oldest queued frame's: 6 checks fail in 5 cases (left-over frames, two packets, three packets, queue wraparound, discontinuity);
     - the discontinuity flag ignored: 2 checks fail in the discontinuity case;
     - the timestamp-error flag ignored: 4 checks fail in 2 cases;
     - a callback that starts with padding given a time anyway: 2 checks fail in the padding case;
     - the clock position treated as frames instead of converted with the frequency: 5 checks fail in the 3 output cases with a clock position.
  3. Hardware: Focusrite Scarlett on this PC with output 1 cabled to input 1, 48 kHz, a -30 dBFS click about every 0.3 s, 40 s per run, against the lab app built on this tree (`_review/arrival-timestamps`, `tslab` with `-DLAB_P009=ON`). Six clean runs of WASAPI shared 480 and 192 and low-latency 480, with 1 to 9 xruns each, one of them with 0 to 4 ms holds in 9 % of callbacks:
     - `hostTimeNs` was present in 100.00 % of callbacks in every run, and `inputHostTimeNs` in 99.3 to 99.95 % (100.00 % after the first 5 s in four of the six), null only at start-up and for 1 to 4 callbacks after each xrun while the callbacks before the discontinuity drained;
     - input jitter (std) 218 to 349 µs, output jitter 255 to 449 µs, native loop L 184.3 to 185.2 ms with robust σ 0.39 to 0.58 ms and no click more than 2 ms off, against 251 to 300 µs, 0.38 to 0.54 ms and 180 to 191 ms for the lab's WASAPI rows (same session, lab fork: 258 and 341 µs input jitter, L 184.5 and 184.3 ms, σ 0.43 and 0.30 ms);
     - in the run with the holds, callback entry minus input time and output time minus callback entry were the same for held and unheld callbacks (51.56 against 51.46 ms, and 60.60 against 60.60 ms), and input jitter was 247 µs against 218 and 335 µs undelayed;
     - two runs (shared 480 and low-latency 480) had 382 and 569 xruns over the run, cause not established (the machine was shared with other builds); there `inputHostTimeNs` was present in 71.6 % and 57.5 % of callbacks, `hostTimeNs` still in 100 %, and the loop was still 184.1 to 184.4 ms. The same low-latency run repeated, with the lab fork and this one alternating, gave 2 and 1 xruns for the lab and 9 and 2 for this patch.
  4. Not checked: WASAPI exclusive input (the Scarlett refuses to open it). Exclusive output alone opened and gave `hostTimeNs` in 100 % of 1501 callbacks at 16.9 ms ahead of callback entry, but its accuracy was not measured.

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

### P013: Oboe callbacks carry device times

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`, `modules/juce_audio_devices/juce_audio_devices.cpp`, `modules/juce_audio_devices/audio_io/juce_StreamFrameTimes.h` (new)
- **Changes:** fills `hostTimeNs` and `inputHostTimeNs` (P007) in the Oboe device, in ns on `CLOCK_MONOTONIC`, on Android 11 (API 30) and later. The level is read once per session with `android_get_device_api_level()`; below 30 both stay null and the new code makes no call into Oboe (Oboe advises against `getTimestamp()` from a data callback before R).
  - **Both times:** the stream's `getTimestamp (CLOCK_MONOTONIC)` says that frame `framePosition` passed the device at `timeNs`. The time of another frame is `timeNs` plus (frame index - `framePosition`) at the stream's sample rate, rounded to the nearest ns. That arithmetic, and the rules for when there is no time, are two small templates in `juce_StreamFrameTimes.h` (built and unit tested on every platform, the stream is a lambda).
  - **Input:** after the `read()` in the output stream's callback, the first frame of this callback's input is `getFramesRead() - framesJustRead`. A short read is timed from its own first frame. A read of nothing or an error, or a failed `getTimestamp()` (which happens for the first few callbacks of a stream), gives null.
  - **Output:** the first output frame of the callback is `getFramesWritten()` read in `onAudioReady`. That count does not yet include this callback's frames: AAudio writes the callback's buffer after the data callback returns, and Oboe's OpenSL ES stream adds `mFramesPerCallback` after `fireDataCallback()`. The time is a prediction of when the frames being produced will be presented. Null if `getTimestamp()` fails.
  - **Context:** `hostTimeNs` is the output time when the callback has output channels and the input time otherwise; `inputHostTimeNs` is the input time. JUCE's session always opens an output stream and runs the callbacks from it, so an input-only configuration still gets callbacks, with `hostTimeNs` equal to `inputHostTimeNs`; an output-only configuration has no input stream and a null `inputHostTimeNs`.
  - **One timestamp per stream:** on API 30 and later the callback reads each stream's `getTimestamp()` once and uses it for both the device time and the latency (`getInputLatencyInSamples()`, `getOutputLatencyInSamples()`). The latency is found from it with JUCE's own arithmetic in `getLatencyFor()` (the fallback it used when `calculateLatencyMillis()` failed), and is 0 when there is no timestamp, as before. So the callback makes no more calls into Oboe than upstream: upstream's `calculateLatencyMillis()` also reads a timestamp and a frame count per stream. Oboe takes a shared lock (`mAAudioStreamLock`) in each of these calls, exclusive only in `close()`, as it does for `read()`. Below API 30 the latency is found as upstream finds it. Nothing is allocated.
- **Why:** the callback context was empty on Android. On a Pixel 7, callback entry on Oboe had a jitter std of 233 to 249 µs and a maximum of 2.4 to 3.0 ms, against 144 to 187 µs and 0.78 to 0.88 ms for the stream's own input timestamps.
- **Default behaviour:** Android Oboe device only, and only on API 30 and later; an app that ignores the two fields is unaffected. On API 30 and later the latency comes from JUCE's arithmetic instead of Oboe's `calculateLatencyMillis()`: the same formula, in integer ns instead of double ms, from a timestamp read earlier in the same callback. Below API 30 nothing changes.
- **Test evidence:** Tested 2026-10-23.
  1. Unit tests: `UnitTestRunner` built out of tree as for P008, run as `UnitTestRunner --category=Audio` (Windows 11, MSVC, Release). With this patch: exit code 0, `All tests completed successfully`, 229 `Completed tests` lines (220 before, plus 9 `Stream frame times` cases), no `!!!` lines. Each of these changes to the helper, with the tests kept, gives exit code 1:
     - the offset subtracted instead of added: 10 checks fail in 5 cases;
     - the input block's first frame taken as `getFramesRead()` without subtracting the frames just read: 4 checks in 2 cases;
     - the support flag ignored: 3 checks in 1 case (the stream is asked, and a time is returned);
     - a failed timestamp ignored: 3 checks in 1 case;
     - the offset truncated instead of rounded: 1 check in 1 case.
  2. Compiles: the arrival-timestamps lab app (outside this repository), a variant that records `hostTimeNs` and `inputHostTimeNs`, builds against this tree for `arm64-v8a` and `x86_64` with NDK 28.1.13356709 and CMake 3.31.6; no warnings from `juce_Oboe_android.cpp` or the new header.
  3. Android 15 emulator (`system-images;android-35;google_apis;x86_64`, `-no-audio`), Oboe duplex 48 kHz, buffer 192, 15 s: both times were present in 1181 of 1182 callbacks (null only in the first, where `getTimestamp()` fails). Input-only: 1352 of 1353, and `hostTimeNs` equal to `inputHostTimeNs` in every one. Output-only: `inputHostTimeNs` null in all 1131 callbacks, `hostTimeNs` present in 1130. A scratch build with a log line per callback (not in this repository) showed the output counter at 0 in the first callback and growing by exactly the previous callback's `numFrames` in each of the 892 later ones, 7 short reads and 2 reads of nothing (null input time) among them, and, whenever the stream's pair was unchanged between two callbacks, the output time advancing by exactly the previous `numFrames` (449 of 449) and the input time by the frames read (277 of 281; the other 4 follow a change of the pair between the two `getTimestamp()` calls of the scratch build). The emulator's timestamps are coarse (they move in steps of about 4 ms), so its jitter says nothing about a phone.
  4. Android 10 emulator (`system-images;android-29;google_apis;x86_64`), same run: 725 callbacks, both times null in all of them. A build whose P013 `getTimestamp()` helper logs each call showed 0 calls on API 29 and 1 per stream per callback on API 35.
  5. Pixel 7, Android 16 (`CP1A.260405.005`), 2026-10-23, 40 s acoustic loop (phone speaker to phone microphone), Oboe 48 kHz, run with `tools/run_android_p013.sh` of the arrival-timestamps harness. Both times were present in every callback after the first 5 s (17489 at buffer 192, 17505 at 960); input-only, present in all 17500 with `hostTimeNs` equal to `inputHostTimeNs` in every one. Standard deviation of each time against its frame count: input 135 and 108 µs, output 134 and 128 µs (the lab's own stream timestamps: 144 to 187 and 103 to 115; callback entry: 209 and 259). Loop on the device times: 10.81 ms at 192 and 10.75 ms at 960 (sd 184 and 215 µs, 114 clicks each, none more than 2 ms off), against the lab's 11.03 and 10.98. The frame-count loop, which uses no times, moved too (21.67 against 21.92 ms at 192, 37.62 against 38.89 at 960), and at 192 the device-time loop minus the frame-count loop is -10.88 ms against the lab's -10.89, so the 0.2 ms shift is in the acoustic path between sessions, not in the times.
  6. One timestamp per stream (the latency read from the same timestamp as the device time), same phone and harness, 2026-10-23, compared with item 5. Both times present in every callback after the first 5 s (17493 at 192, 17507 at 960; input-only 17500, `hostTimeNs` equal to `inputHostTimeNs` in every one). Standard deviation against the frame count: input 107 and 125 µs, output 104 and 129 µs (item 5: 135 and 108, 134 and 128). Device-time loop minus frame-count loop: -10.87 ms at 192 and -26.88 ms at 960 (item 5: -10.88 and -26.88). Latency reported at the end of each run, input/output in samples: 100/330, 110/1081, input-only 133/341 (item 5: 81/341, 85/1112, 116/340; upstream JUCE in the lab: 100/340, 157/1087, 85/349). Callback entry std was 312 and 303 µs (item 5: 209 and 259); this patch makes fewer calls than item 5's, and the frame-count loop, which does not depend on entry times, is unchanged.

### P014: CoreAudio callbacks carry device times for every chunk and for their input

- **Files:** `modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp`, `modules/juce_audio_devices/audio_io/juce_CallbackTimeHelpers.cpp` (new), `modules/juce_audio_devices/juce_audio_devices.cpp` (one include)
- **Changes:** fills `hostTimeNs` and `inputHostTimeNs` (P007) in the CoreAudio backend, in ns on the mach host time line converted with `timeConversions.hostTimeToNanos`, for every chunk of a buffer.
  - **Chunks:** CoreAudio gives the IOProc one stamp per direction for the whole buffer and `processBufferChunk()` splits it into chunks of the device's buffer size; it passed a time to the first chunk only. A chunk's time is the stamp's host time plus the chunk's offset at the sample rate, rounded to the nearest ns (P008's arithmetic). A null stamp, or one without `kAudioTimeStampHostTimeValid` in `mFlags`, gives a null time, for the output stamp as well as the input one (the `jassert` in `checkTimestampsForDiscontinuity` stays).
  - **Input:** `inputHostTimeNs` is `inInputTime` advanced as above, when the callback has input channels. `hostTimeNs` is the output stamp's time when the callback has outputs and the input stamp's otherwise (as before).
  - **Combined devices:** `AudioIODeviceCombiner` calls the app's callback from its input device's callback, with an output scratch buffer. With outputs it now passes `inputHostTimeNs` = the input device's input time and `hostTimeNs` = null (the output is presented later on another device and no relation between the two clocks is established); without outputs it passes the input device's context unchanged. `updateSampleTimeFromContext` still reads the original context. No cross-device synchronisation is added.
  - **Helpers:** `CallbackTimeHelpers::timeOfSample()` (offset arithmetic, flag and null checks) and `combinedCallbackContext()` are in a new file that compiles on every platform, with their unit tests.
- **Why:** the CoreAudio context had a time for the first chunk only, none for the input, and a combined device's callback with outputs held an input time in `hostTimeNs`.
- **Default behaviour:** an app that ignores the two fields is unaffected. `hostTimeNs` is now present on every chunk (it was null after the first) and null when the stamp's host time is not flagged valid (it was passed unchecked). A combined device's `hostTimeNs` is null where it held the input time. macOS only. The iOS backend is unchanged.
- **Test evidence:** Tested 2026-10-23. There is no hardware qualification: nothing here was run against a CoreAudio device.
  1. Windows 11, MSVC (Visual Studio 18): `UnitTestRunner` built out of tree with `cmake -S <JUCE> -B <build> -DJUCE_BUILD_EXTRAS=ON`, then `cmake --build <build> --target UnitTestRunner --config Release`, run as `UnitTestRunner --category=Audio`. With this patch: exit code 0, `All tests completed successfully`, 226 `Completed tests` lines (219 before, plus 7 `Callback time helpers` cases), no `!!!` lines. Three changes to the helpers, with the tests kept, give exit code 1: the flag check removed fails 3 checks in the flag case; the rounding removed (truncation) fails 2 checks in the chunk case; the combined context keeping `hostTimeNs` with outputs fails 2 checks in the combined case.
  2. `juce_CoreAudio_mac.cpp` itself was not compiled. The changed block of `processBufferChunk()` was compiled as extracted text against stand-in `AudioTimeStamp`, flag and conversion types with the Android NDK 28.1.13356709 clang (`-std=c++17 -fsyntax-only -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow`): no diagnostics.
  3. Mac: compiled, unit-tested and run on a GitHub `macos-latest` runner from a branch outside this series (`.github/workflows/macos-check.yml`), `UnitTestRunner --category=Audio`. Result (run 39950801825, 2026-10-23, on the branch `ci/macos-check` at 0f0ec0a6ed, which is this patch on P011): built with no warnings in the changed files; 214 Audio tests completed, all passing, including the 7 new ones. Run 39954189927 on the whole series, P001 to P014: 224 Audio tests completed, all passing. No Mac audio hardware was used.

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
