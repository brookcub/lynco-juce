# Oboe lifecycle regression tests (P016)

These Android hardware tests open real system-default input/output streams. They
write silence and discard microphone samples. Run on a development phone with no
other audio test running. They require a 48 kHz route. With microphone permission
denied, the runner instead checks permission rejection and output-only opening.

Use a JUCE Android GUI application target with this directory's `Main.cpp` as its
entry point, linking `juce_audio_devices` and `juce_gui_basics` from this checkout.
Define `JUCE_UNIT_TESTS=1` for the entire target and enable RECORD_AUDIO in the
Android manifest. Use a separate application ID, not an installed product's ID.
Build and install the Debug ARM64 APK, grant microphone permission, and launch it.
Read `adb logcat` for `OBOE TESTS: passes=... failures=0`; zero passes is a failure.
The application exits after the tests. Then revoke microphone permission with
`adb shell pm revoke <test-package> android.permission.RECORD_AUDIO` and launch
again; this run must also report nonzero passes and zero failures. The runner does
not request permission itself. Restore the permission before the next full run.

The backend includes `juce_Oboe_android_test.cpp` only with `JUCE_UNIT_TESTS`.
Use the supplied runner for both phases: it runs synchronous lifecycle checks on
the message thread, then input notification checks on a worker while Android
delivers recovery messages. Running only the message-thread phase omits the input
notification tests.

Coverage:

- `isPlaying()` remains true inside a real audio callback and becomes false on stop.
- `isOpen()` completes inside a real audio callback while the lifecycle lock is held;
  stopping preserves open state, and closing clears it.
- Full, partial, zero and failed input reads preserve new samples and silence the
  unread tail. These deterministic buffer tests feed synthetic read results through
  the production preparation function, for mono/stereo float and int16 data. They
  check full block size, direct-reference identity, conversion and actual read count.
- Scratch covers both stream capacities and the requested block size. An oversized
  callback silences its output without writing beyond the supplied block.
- Denied microphone permission returns its error without creating a session;
  output-only opening still works without permission.
- One successful reconnect after a controlled disconnected error.
- Input read disconnection signalled from a real audio callback: duplicate signals
  coalesce, other read errors do not recover, deletion cancels queued recovery, and
  a failed reopen reports one terminal error. These read results are injected.
- Stop, close, or destruction from the stopped callback cancels recovery.
- A client replacement opened/started during stopped notification survives both
  interrupted recovery and failed startup.
- Stop, close, or destruction during preparation cancels startup.
- Real native input/output streams deliberately closed before `start()` report
  failure from requestStart, close partial streams, and pair preparation/stopped notifications.
- Close or destruction during failed-start notification suppresses stale error
  delivery and does not access the deleted device.

The disconnected error is injected directly at the backend recovery entry point;
it is not evidence of a natural OS disconnection or of native callback timing.
These tests do not reproduce or fix the separate Pixel vendor MMAP process-death
crash, and do not establish thread safety for unsynchronised public getters.
