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
  3. The same hook on the code before this patch (`ab8558ecd0`): a disconnect gives no lifecycle callback at all (callbacks dip briefly and continue); another error gives none either, audio callbacks stop, and the device still reports `deviceOpen=yes`.
  4. Not yet run on a phone. The procedure and the expected log lines are in the harness's `LIFECYCLE-LAB.md`: install, grant `RECORD_AUDIO`, capture `logcat -s LynCoLifecycle`, connect and disconnect a Bluetooth speaker.
