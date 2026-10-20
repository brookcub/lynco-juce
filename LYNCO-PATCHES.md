# LynCo JUCE patches

This branch is upstream JUCE plus a short series of LynCo patches, one commit each.

Base tag: `8.0.12` = `29396c22c93392d6738e021b83196283d6e4d850`
Upstream: https://github.com/juce-framework/JUCE

## Rule

Brooklyn (2026-10-19):

> "never mix our changes into juce, keep them visible and separable, and do not build individual product policy/configuration into the juce changes because all lynco apps must share the same juce version with the same patches."

Each patch is one commit, subject `LynCo: <purpose>`, trailer `LynCo-Patch: Pnnn`.

## Upgrade procedure

1. Create a branch from the new upstream tag and cherry-pick the series onto it, in order.
2. Drop a patch if upstream has fixed the problem it addresses.
3. Rebuild and retest every LynCo app against the branch.
4. Tag the result `lynco-<juce>.<n>`. Tags are never moved; a fix gets a new tag.

## Patches

### P001: AudioRecord channel mask follows the opened channel count

- **Files:** `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** the AudioRecord channel mask is chosen from `numDeviceInputChannels` (opened) instead of `numDeviceInputChannelsAvailable` (offered).
- **Why:** opening one channel on a stereo-capable device gave a stereo AudioRecord while the reader assumed mono, so each callback read interleaved L/R as one mono stream at twice the frame count.
- **Origin:** LynTune L3, `d2590aa1`.
- **Default behaviour:** fixed for every app that opens fewer input channels than the device offers; unchanged otherwise.

### P002: AudioDeviceManager device-list change handler

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h`, `modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp`
- **Changes:** adds `AudioDeviceManager::setAudioDeviceListChangeHandler()`, the `AudioDeviceListChangeHandler` type, a `deviceListChangeHandlerLock`, and the capability define `JUCE_AUDIO_DEVICE_MANAGER_HAS_OWNER_CONTROLLED_LIST_CHANGES`. `audioDeviceListChanged()` calls the handler first, synchronously, with "current device still available".
- **Why:** JUCE closes and reopens a vanished device before the app can apply its own device-loss policy.
- **Origin:** LynTune L1, `2844584e`.
- **Default behaviour:** with no handler set, JUCE's own close/reinitialise recovery runs unchanged. Every platform.

### P003: AudioRecord UNPROCESSED source when more than one input channel is opened

- **Files:** `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** the AudioRecord audio source is `UNPROCESSED` (9) when `numDeviceInputChannels > 1`, otherwise `DEFAULT` (0).
- **Why:** on a Pixel 7 a two-channel AudioRecord on the default source returns one mic duplicated; UNPROCESSED returns both physical mics.
- **Origin:** LynTune L4, `232c8570` (D635).
- **Default behaviour:** one-channel opens are unchanged (`DEFAULT`); multi-channel opens change source.

### P004: Oboe input preset

- **Files:** `modules/juce_audio_devices/native/juce_Oboe_android.cpp`
- **Changes:** when `CHRONOS_ANDROID_INPUT_PRESET` is defined, Oboe input streams call `setInputPreset()` with its value.
- **Why:** Oboe's input preset cannot otherwise be set; its default is VoiceRecognition, which processes the signal.
- **Origin:** Chronos C1, `8125921f` (`juce-android-input-preset.patch`), kept as is including the define name.
- **Default behaviour:** inert unless the define is set (Android API 28+ only takes effect).

### P005: Android AudioRecord input callbacks

- **Files:** `modules/juce_audio_devices/audio_io/juce_AudioIODeviceType.h`, `modules/juce_audio_devices/native/juce_Audio_android.cpp`
- **Changes:** adds `AudioIODeviceType::AndroidAudioRecordCallbacks { beforeStart, afterStart, beforeStop }` and `AudioIODeviceType::setAndroidAudioRecordCallbacks()`. Each open copies the registered set and releases it on close.
- **Why:** an app cannot reach the AudioRecord JUCE opens, so it cannot call `setPreferredDevice()` or watch the routed device.
- **Origin:** replaces LynTune L2, `d64da078` and `302c8e86`.
- **Default behaviour:** no callbacks registered, upstream behaviour. Android AudioRecord type only.

### P006: Android software peer survives a failed paint-buffer allocation

- **Files:** `modules/juce_gui_basics/native/juce_Windowing_android.cpp`
- **Changes:** `handlePaintCallback` checks the `NewIntArray` result and pending exception, records `sizeAllocated` only on success, and paints in strips when the full-clip buffer cannot be allocated; a frame is skipped if not even one row fits.
- **Why:** on a constrained Java heap the unchecked allocation left an `OutOfMemoryError` pending and CheckJNI aborted the app.
- **Origin:** replaces LynTune L5, `5f13e5c4`, a tiled-paint overlay that tiled every frame.
- **Default behaviour:** unchanged whenever the allocation succeeds. Android.