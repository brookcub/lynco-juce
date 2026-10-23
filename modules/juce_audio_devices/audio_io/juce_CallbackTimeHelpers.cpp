/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce::CallbackTimeHelpers
{

/*  The time, in nanoseconds, of the sample that is sampleOffset samples into a buffer whose first
    sample is stamped with 'stamp': the stamp's host time converted with hostTimeToNanos, plus the
    offset at sampleRate, rounded to the nearest nanosecond (the arithmetic of CallbackMaxSizeEnforcer).

    Returns nullopt if there is no stamp, or its flags don't say that the host time is valid, or an
    offset needs a sample rate that isn't positive. The stamp is anything with mFlags and mHostTime,
    which is what CoreAudio's AudioTimeStamp has.
*/
template <typename Stamp, typename Flags, typename HostTimeToNanos>
inline std::optional<uint64_t> timeOfSample (const Stamp* stamp,
                                             Flags hostTimeValidFlag,
                                             HostTimeToNanos&& hostTimeToNanos,
                                             size_t sampleOffset,
                                             double sampleRate)
{
    if (stamp == nullptr || (stamp->mFlags & hostTimeValidFlag) == 0)
        return std::nullopt;

    const uint64_t start = hostTimeToNanos (stamp->mHostTime);

    if (sampleOffset == 0)
        return start;

    if (! (sampleRate > 0.0))
        return std::nullopt;

    return start + (uint64_t) std::llround ((double) sampleOffset * 1.0e9 / sampleRate);
}

/*  The context that AudioIODeviceCombiner passes its callback from the callback of its input device,
    whose context is inputDeviceContext. The input time is the input device's. The callback's outputs
    are presented later, on another device, and nothing relates the clocks of the two devices, so with
    outputs there is no output time and hostTimeNs is null; without outputs it is the input time.
*/
inline AudioIODeviceCallbackContext combinedCallbackContext (const AudioIODeviceCallbackContext& inputDeviceContext,
                                                            bool hasOutputs)
{
    if (! hasOutputs)
        return inputDeviceContext;

    return { nullptr, inputDeviceContext.inputHostTimeNs };
}

//==============================================================================
#if JUCE_UNIT_TESTS

class CallbackTimeHelpersTests final : public UnitTest
{
public:
    CallbackTimeHelpersTests() : UnitTest ("Callback time helpers", UnitTestCategories::audio) {}

    void runTest() override
    {
        // The fake stamp has CoreAudio's field names but flag values of its own, and its host time
        // is in ticks of 24 per 1000 ns (so 240,000,000 ticks are 10 s), as on a Mac with a 125/3
        // timebase. Every time expected below was worked out by hand.

        beginTest ("The first sample of a buffer has the stamp's time in nanoseconds");
        expect (timeAt ({ hostTimeValid, 240'000'000 }, 0, 48000.0) == 10'000'000'000u);

        beginTest ("A later chunk is advanced by its offset, and a fraction of a nanosecond is rounded");
        {
            // 512 samples at 48 kHz last 10,666,666.67 ns, and 1024 samples 21,333,333.33 ns
            const FakeStamp stamp { hostTimeValid, 240'000'000 };
            expect (timeAt (stamp,  512, 48000.0) == 10'010'666'667u);
            expect (timeAt (stamp, 1024, 48000.0) == 10'021'333'333u);

            // 147 samples at 44.1 kHz last 3,333,333.33 ns, and 294 samples 6,666,666.67 ns
            expect (timeAt (stamp, 147, 44100.0) == 10'003'333'333u);
            expect (timeAt (stamp, 294, 44100.0) == 10'006'666'667u);
        }

        beginTest ("A stamp whose host time is not flagged valid gives no time at any offset");
        {
            expect (! timeAt ({ 0, 240'000'000 }, 0, 48000.0).has_value());
            expect (! timeAt ({ sampleTimeValid, 240'000'000 }, 0, 48000.0).has_value());
            expect (! timeAt ({ sampleTimeValid, 240'000'000 }, 512, 48000.0).has_value());
            expect (timeAt ({ sampleTimeValid | hostTimeValid, 240'000'000 }, 0, 48000.0).has_value());
        }

        beginTest ("No stamp gives no time at any offset");
        {
            expect (! timeAt (nullptr, 0, 48000.0).has_value());
            expect (! timeAt (nullptr, 512, 48000.0).has_value());
        }

        beginTest ("An offset needs a sample rate, but the first sample does not");
        {
            const FakeStamp stamp { hostTimeValid, 240'000'000 };
            expect (timeAt (stamp, 0, 0.0) == 10'000'000'000u);
            expect (! timeAt (stamp, 512, 0.0).has_value());
        }

        beginTest ("A combined device's callback has its input time and, with outputs, no output time");
        {
            const uint64_t host = 7'000, input = 5'000;
            const auto withOutputs = combinedCallbackContext ({ &host, &input }, true);
            expect (withOutputs.hostTimeNs == nullptr);
            expect (withOutputs.inputHostTimeNs == &input);

            const auto noInputTime = combinedCallbackContext ({ &host, nullptr }, true);
            expect (noInputTime.hostTimeNs == nullptr);
            expect (noInputTime.inputHostTimeNs == nullptr);
        }

        beginTest ("A combined device's callback without outputs keeps the input device's times");
        {
            const uint64_t host = 7'000, input = 5'000;
            const auto noOutputs = combinedCallbackContext ({ &host, &input }, false);
            expect (noOutputs.hostTimeNs == &host);
            expect (noOutputs.inputHostTimeNs == &input);
        }
    }

private:
    struct FakeStamp
    {
        uint32_t mFlags;
        uint64_t mHostTime;
    };

    static constexpr uint32_t sampleTimeValid = 1, hostTimeValid = 2;

    static std::optional<uint64_t> timeAt (const FakeStamp& stamp, size_t offset, double sampleRate)
    {
        return timeAt (&stamp, offset, sampleRate);
    }

    static std::optional<uint64_t> timeAt (const FakeStamp* stamp, size_t offset, double sampleRate)
    {
        return timeOfSample (stamp, hostTimeValid, [] (uint64_t ticks) { return ticks * 125 / 3; }, offset, sampleRate);
    }
};

static CallbackTimeHelpersTests callbackTimeHelpersTests;

#endif

} // namespace juce::CallbackTimeHelpers
