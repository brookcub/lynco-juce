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
