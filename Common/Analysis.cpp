#include "Analysis.h"

#include <complex>

namespace RoundTrip
{
namespace
{
using Complex = std::complex<double>;

constexpr int alignmentWindow = 65536;
constexpr int slipWindow = 4096;
constexpr int slipSearchRadius = 8192;
constexpr int eventMergeGap = 32;
constexpr size_t maxEventsPerChannel = 1000;
constexpr double floorDb = -300.0;

void fft (std::vector<Complex>& a, bool inverse)
{
    const auto n = a.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
            std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = juce::MathConstants<double>::twoPi / (double) len * (inverse ? -1.0 : 1.0);
        const Complex step (std::cos (angle), std::sin (angle));

        for (size_t i = 0; i < n; i += len)
        {
            Complex w (1.0);

            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }

    if (inverse)
        for (auto& x : a)
            x /= (double) n;
}

struct LagEstimate
{
    juce::int64 lag = -1;
    double correlation = 0.0;  // signed, normalised
    double ambiguity = 1.0;    // second-highest peak relative to the highest
};

// Finds k in [0, maxLag] maximising |sum x[n] * y[n + k]|. Requires yLen >= xLen + maxLag.
LagEstimate estimateLag (const float* x, int xLen, const float* y, int yLen, int maxLag)
{
    jassert (yLen >= xLen + maxLag);

    size_t n = 1;

    while (n < (size_t) juce::jmax (xLen + maxLag, yLen))
        n <<= 1;

    std::vector<Complex> fx (n), fy (n);
    std::copy_n (x, xLen, fx.begin());
    std::copy_n (y, yLen, fy.begin());

    fft (fx, false);
    fft (fy, false);

    for (size_t i = 0; i < n; ++i)
        fy[i] *= std::conj (fx[i]);

    fft (fy, true);

    size_t best = 0;

    for (size_t k = 1; k <= (size_t) maxLag; ++k)
        if (std::abs (fy[k].real()) > std::abs (fy[best].real()))
            best = k;

    auto second = 0.0;

    for (size_t k = 0; k <= (size_t) maxLag; ++k)
        if (k + 4 < best || k > best + 4)
            second = juce::jmax (second, std::abs (fy[k].real()));

    const auto peak = std::abs (fy[best].real());

    LagEstimate result;
    result.lag = (juce::int64) best;
    result.correlation = normalisedCorrelation (x, y + best, xLen);
    result.ambiguity = peak > 0.0 ? second / peak : 1.0;
    return result;
}

bool hasSignal (const float* x, int numSamples, int bits) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        if (quantise (x[i], bits) != 0)
            return true;

    return false;
}

double toDb (double ratio, double scale)
{
    return ratio > 0.0 ? scale * std::log10 (ratio) : floorDb;
}

juce::String channelName (int ch)
{
    return "Channel " + juce::String (ch);
}

} // namespace

double normalisedCorrelation (const float* x, const float* y, int numSamples) noexcept
{
    double xy = 0.0, xx = 0.0, yy = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        xy += (double) x[i] * y[i];
        xx += (double) x[i] * x[i];
        yy += (double) y[i] * y[i];
    }

    return (xx > 0.0 && yy > 0.0) ? xy / std::sqrt (xx * yy) : 0.0;
}

bool Analysis::isBitExact() const noexcept
{
    if (! isValid() || ! slips.empty())
        return false;

    return std::all_of (channels.begin(), channels.end(), [] (const ChannelResult& c)
    {
        return c.mismatches == 0 && ! c.inverted;
    });
}

juce::String Analysis::getVerdict() const
{
    if (! isValid())    return "ERROR";
    if (isBitExact())   return "BIT-EXACT";
    if (! slips.empty()) return "SLIPPED";
    return "NOT BIT-EXACT";
}

Analysis analyse (const AudioClip& stimulus, const juce::AudioBuffer<float>& capture, juce::int64 maxLagRequested)
{
    Analysis result;
    result.compareBits = stimulus.getCompareBits();
    result.sampleRate = stimulus.sampleRate;
    result.stimulusLength = stimulus.getNumSamples();

    const auto bits = result.compareBits;
    const auto numChannels = stimulus.getNumChannels();
    const auto length = stimulus.getNumSamples();
    const auto captureLength = capture.getNumSamples();

    if (capture.getNumChannels() < numChannels)
    {
        result.error = "Recording has " + juce::String (capture.getNumChannels()) + " channels, stimulus has "
                     + juce::String (numChannels);
        return result;
    }

    //==============================================================================
    // Global alignment, per channel, from the first stretch of non-silent stimulus.
    auto onset = -1;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* x = stimulus.audio.getReadPointer (ch);

        for (int i = 0; i < (onset < 0 ? length : onset); ++i)
        {
            if (quantise (x[i], bits) != 0)
            {
                onset = i;
                break;
            }
        }
    }

    if (onset < 0)
    {
        result.error = "Stimulus is digital silence";
        return result;
    }

    const auto available = captureLength - onset;
    const auto window = juce::jmin (alignmentWindow, length - onset, available);
    const auto maxLag = (int) juce::jmin (maxLagRequested, (juce::int64) (available - window));

    if (window < 1024 || maxLag < 0)
    {
        result.error = "Recording is too short to align against the stimulus";
        return result;
    }

    result.channels.resize ((size_t) numChannels);
    auto reference = 0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto estimate = estimateLag (stimulus.audio.getReadPointer (ch, onset), window,
                                           capture.getReadPointer (ch, onset), window + maxLag, maxLag);

        auto& c = result.channels[(size_t) ch];
        c.lag = estimate.lag;
        c.correlation = std::abs (estimate.correlation);
        c.inverted = estimate.correlation < 0.0;

        if (estimate.ambiguity > 0.9)
            result.warnings.add (channelName (ch) + ": alignment is ambiguous (periodic stimulus?). "
                                 "Use noise, a chirp or music.");

        if (c.correlation > result.channels[(size_t) reference].correlation)
            reference = ch;
    }

    const auto& ref = result.channels[(size_t) reference];

    if (ref.correlation < 0.5)
    {
        result.error = "No recognisable copy of the stimulus in the recording (best correlation "
                     + juce::String (ref.correlation, 3) + ")";
        return result;
    }

    result.lag = ref.lag;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& c = result.channels[(size_t) ch];

        if (c.inverted)
            result.warnings.add (channelName (ch) + " is polarity-inverted");

        if (c.correlation >= 0.5 && c.lag != result.lag)
            result.warnings.add (channelName (ch) + " aligns at " + juce::String (c.lag) + " samples but channel "
                                 + juce::String (reference) + " at " + juce::String (result.lag) + " (inter-channel skew)");
    }

    for (int i = 0; i < numChannels; ++i)
    {
        const auto* s = stimulus.audio.getReadPointer (i, onset);
        const auto own = std::abs (normalisedCorrelation (s, capture.getReadPointer (i, onset + (int) result.lag), window));

        for (int j = 0; j < numChannels; ++j)
        {
            if (i == j || std::abs (normalisedCorrelation (s, stimulus.audio.getReadPointer (j, onset), window)) > 0.99)
                continue;

            const auto other = std::abs (normalisedCorrelation (s, capture.getReadPointer (j, onset + (int) result.lag), window));

            if (other > own + 0.1)
                result.warnings.add ("Stimulus channel " + juce::String (i) + " appears on recorded channel "
                                     + juce::String (j) + " (swapped or misaligned slots?)");
        }
    }

    //==============================================================================
    // Track alignment through the take, so dropped or repeated buffers show up as slips
    // rather than as everything after them mismatching.
    const auto sign = (float) (ref.inverted ? -1.0 : 1.0);
    const auto* x = stimulus.audio.getReadPointer (reference);
    const auto* y = capture.getReadPointer (reference);
    auto tracked = result.lag;

    const auto squaredError = [&] (juce::int64 n, juce::int64 lag)
    {
        const auto index = n + lag;
        const auto e = (index >= 0 && index < captureLength ? sign * y[index] : 0.0f) - x[n];
        return (double) e * e;
    };

    for (int start = 0; start < length; start += slipWindow)
    {
        const auto len = juce::jmin (slipWindow, length - start);
        const auto fits = start + tracked >= 0 && start + tracked + len <= captureLength;

        if (! hasSignal (x + start, len, bits)
            || (fits && sign * normalisedCorrelation (x + start, y + start + tracked, len) >= 0.5))
            continue;

        const auto searchStart = juce::jmax ((juce::int64) 0, start + tracked - slipSearchRadius);
        const auto searchLength = (int) juce::jmin ((juce::int64) captureLength - searchStart,
                                                    (juce::int64) len + 2 * slipSearchRadius);

        if (searchLength <= len)
            continue;

        const auto estimate = estimateLag (x + start, len, y + searchStart, searchLength, searchLength - len);
        const auto candidate = searchStart + estimate.lag - start;

        if (sign * estimate.correlation < 0.9 || estimate.ambiguity >= 0.9 || candidate == tracked)
            continue;

        // The previous window still matched well enough, so the slip lies between its start
        // and the end of this one. Place it where the total error of old-then-new is least.
        const auto from = juce::jmax ((juce::int64) start - slipWindow,
                                      result.slips.empty() ? (juce::int64) 0 : result.slips.back().stimulusIndex + 1,
                                      (juce::int64) 0);
        const auto to = (juce::int64) start + len;

        auto newError = 0.0;

        for (auto n = from; n < to; ++n)
            newError += squaredError (n, candidate);

        auto oldError = 0.0, bestCost = newError;
        auto bestIndex = from;

        for (auto p = from; p < to; ++p)
        {
            oldError += squaredError (p, tracked);
            newError -= squaredError (p, candidate);

            if (oldError + newError < bestCost)
            {
                bestCost = oldError + newError;
                bestIndex = p + 1;
            }
        }

        result.slips.push_back ({ bestIndex, tracked, candidate });
        tracked = candidate;
    }

    //==============================================================================
    // Sample-by-sample comparison at the comparison bit depth.
    const auto negativeFullScale = -(1 << (bits - 1));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = result.channels[(size_t) ch];
        const auto* s = stimulus.audio.getReadPointer (ch);
        const auto* r = capture.getReadPointer (ch);

        double ss = 0.0, sr = 0.0;
        juce::int64 lastMismatch = -eventMergeGap - 1;
        auto storing = false;
        LagCursor cursor (result);

        for (int n = 0; n < length; ++n)
        {
            const auto index = n + cursor.at (n);
            const auto inRange = index >= 0 && index < captureLength;
            const auto rv = inRange ? r[index] : 0.0f;
            const auto qs = quantise (s[n], bits);
            const auto qr = quantise (rv, bits);

            ss += (double) s[n] * s[n];
            sr += (double) s[n] * rv;
            ++c.compared;

            if (inRange && qs == qr)
                continue;

            if (inRange && qs == negativeFullScale && qr == negativeFullScale + 1)
            {
                ++c.fullScaleClamps;
                continue;
            }

            const auto error = std::abs (qr - qs);
            ++c.mismatches;
            c.maxAbsError = juce::jmax (c.maxAbsError, error);

            if (n - lastMismatch > eventMergeGap)
            {
                storing = c.events.size() < maxEventsPerChannel;

                if (storing)
                    c.events.push_back ({ n, 0, 0 });
                else
                    c.eventsTruncated = true;
            }

            if (storing)
            {
                auto& e = c.events.back();
                e.length = n - e.start + 1;
                e.maxAbsError = juce::jmax (e.maxAbsError, error);
            }

            lastMismatch = n;
        }

        if (ss > 0.0)
        {
            const auto gain = sr / ss;
            auto residual = 0.0;
            LagCursor residualCursor (result);

            for (int n = 0; n < length; ++n)
            {
                const auto index = n + residualCursor.at (n);
                const auto rv = (index >= 0 && index < captureLength) ? (double) r[index] : 0.0;
                const auto e = rv - gain * s[n];
                residual += e * e;
            }

            c.gainDb = toDb (std::abs (gain), 20.0);
            c.residualDb = gain != 0.0 ? toDb (residual / (gain * gain * ss), 10.0) : 0.0;
        }

        for (juce::int64 i = 0; i < juce::jmin (result.lag, (juce::int64) captureLength); ++i)
            if (quantise (r[i], bits) != 0)
                ++c.preRollNonZero;

        for (auto i = juce::jmax ((juce::int64) 0, length + result.getFinalLag()); i < captureLength; ++i)
            if (quantise (r[i], bits) != 0)
                ++c.postRollNonZero;

        if (c.fullScaleClamps > 0)
            result.warnings.add (channelName (ch) + ": " + juce::String (c.fullScaleClamps)
                                 + " negative full-scale samples came back 1 LSB high (JUCE's float->int output "
                                   "conversion clamps -FS); not counted as mismatches");

        if (c.preRollNonZero > 0)
            result.warnings.add (channelName (ch) + ": " + juce::String (c.preRollNonZero)
                                 + " non-zero samples before the stimulus arrived (stale buffers, or analogue noise)");

        if (c.postRollNonZero > 0)
            result.warnings.add (channelName (ch) + ": " + juce::String (c.postRollNonZero)
                                 + " non-zero samples after the stimulus ended (replayed buffers, or analogue noise)");
    }

    return result;
}

Timing measureTiming (const Take& take, const Analysis& analysis)
{
    Timing timing;

    if (! analysis.isValid() || take.blocks.empty() || take.device.sampleRate <= 0.0)
        return timing;

    const auto lastIndex = analysis.stimulusLength - 1 + analysis.getFinalLag();

    if (lastIndex >= take.capture.getNumSamples())
        return timing;

    auto it = std::upper_bound (take.blocks.begin(), take.blocks.end(), lastIndex,
                                [] (juce::int64 value, const BlockStamp& b) { return value < b.captureIndex; });

    if (it == take.blocks.begin() || (it == take.blocks.end() && take.blockLogOverflowed))
        return timing;

    --it;

    const auto sinceMs = [&] (juce::int64 ticks)
    {
        return juce::Time::highResolutionTicksToSeconds (ticks - take.pressTicks) * 1000.0;
    };

    timing.pressToFirstCallbackMs = sinceMs (take.firstCallbackTicks);
    timing.pressToLastRoundTripMs = sinceMs (it->ticks);
    timing.stimulusMs = (double) analysis.stimulusLength * 1000.0 / take.device.sampleRate;
    timing.overheadMs = timing.pressToLastRoundTripMs - timing.stimulusMs;
    timing.valid = true;
    return timing;
}

} // namespace RoundTrip
