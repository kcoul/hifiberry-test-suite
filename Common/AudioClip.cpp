#include "AudioClip.h"

namespace RoundTrip
{
juce::Result loadClip (const juce::File& file, AudioClip& result)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return juce::Result::fail ("Can't read audio file " + file.getFullPathName());

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max() / 2)
        return juce::Result::fail ("Unsupported length (" + juce::String (reader->lengthInSamples) + " samples)");

    const auto numSamples = (int) reader->lengthInSamples;

    result.file = file;
    result.sampleRate = reader->sampleRate;
    result.bitsPerSample = (int) reader->bitsPerSample;
    result.isFloat = reader->usesFloatingPointData;
    result.audio.setSize ((int) reader->numChannels, numSamples);

    if (! reader->read (&result.audio, 0, numSamples, 0, true, true))
        return juce::Result::fail ("Failed reading " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result writeClip (const juce::File& file,
                        const juce::AudioBuffer<float>& audio,
                        double sampleRate,
                        int bitsPerSample,
                        bool isFloat)
{
    using Options = juce::AudioFormatWriterOptions;

    const auto writeFloat = isFloat || bitsPerSample > 24;

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return juce::Result::fail ("Can't create " + file.getFullPathName());

    const auto options = Options{}.withSampleRate (sampleRate)
                                  .withNumChannels (audio.getNumChannels())
                                  .withBitsPerSample (writeFloat ? 32 : bitsPerSample)
                                  .withSampleFormat (writeFloat ? Options::SampleFormat::floatingPoint
                                                                : Options::SampleFormat::integral);

    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);

    if (writer == nullptr)
        return juce::Result::fail ("Can't create a WAV writer for " + file.getFullPathName());

    auto ok = true;

    if (writeFloat)
    {
        ok = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
    else
    {
        // Quantise here rather than via JUCE's float->int path, which clamps -FS to -FS+1.
        const auto numChannels = audio.getNumChannels();
        const auto shift = 32 - bitsPerSample;
        constexpr int chunk = 8192;

        juce::HeapBlock<int> block ((size_t) (numChannels * chunk));
        std::vector<const int*> channels ((size_t) numChannels + 1, nullptr);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[(size_t) ch] = block.get() + ch * chunk;

        for (int start = 0; ok && start < audio.getNumSamples(); start += chunk)
        {
            const auto num = juce::jmin (chunk, audio.getNumSamples() - start);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* src = audio.getReadPointer (ch, start);
                auto* dst = block.get() + ch * chunk;

                for (int i = 0; i < num; ++i)
                    dst[i] = (int) ((juce::uint32) quantise (src[i], bitsPerSample) << shift);
            }

            ok = writer->write (channels.data(), num);
        }
    }

    writer.reset();

    return ok ? juce::Result::ok() : juce::Result::fail ("Failed writing " + file.getFullPathName());
}

juce::File getRecordingFileFor (const juce::File& stimulus)
{
    return stimulus.getSiblingFile (stimulus.getFileNameWithoutExtension() + "_RTL.wav");
}

juce::File getReportFileFor (const juce::File& stimulus)
{
    return stimulus.getSiblingFile (stimulus.getFileNameWithoutExtension() + "_RTL.json");
}

} // namespace RoundTrip
