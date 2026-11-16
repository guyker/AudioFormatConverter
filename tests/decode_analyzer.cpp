#include "DecodeAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

}

int main(int argc, char* argv[])
{
    try {
        require(argc == 9,
            "Expected clean, retagged, truncated, dual-mono, antiphase, padded, full-scale, and silence paths.");
        MediaRecord clean;
        DecodeAnalyzer::Analyze(argv[1], clean, true);
        require(clean.integrity.status == "ok", "Clean FLAC failed integrity verification.");
        require(clean.integrity.decodedSamples.has_value() &&
            *clean.integrity.decodedSamples > 0, "Clean FLAC decoded sample count is missing.");
        require(!clean.flacMd5 || clean.integrity.flacMd5Matches == true,
            "Clean FLAC STREAMINFO MD5 did not verify.");
        require(clean.integrity.fileSha256.has_value() &&
            clean.integrity.pcmSha256.has_value(), "Clean FLAC hashes are missing.");
        require(clean.signal.status == "partial" && !clean.signal.measurements.empty(),
            "Signal analysis did not produce measurements.");
        require(clean.signal.measurements.contains("integratedLufs"),
            "EBU R128 integrated loudness was not measured.");
        require(std::abs(clean.signal.measurements.at("integratedLufs") - (-21.1)) <= 0.1,
            "997 Hz synthetic sine integrated loudness is outside the expected tolerance.");
        require(std::abs(clean.signal.measurements.at("channel0.rmsDbfs") - (-21.1)) <= 0.2,
            "997 Hz synthetic sine RMS level is outside the expected tolerance.");
        require(clean.signal.measurements.contains("truePeakDbtp"),
            "EBU R128 true-peak measurement was not produced.");

        MediaRecord retagged;
        DecodeAnalyzer::Analyze(argv[2], retagged, false);
        require(retagged.integrity.status == "ok", "Retagged FLAC failed integrity verification.");
        require(retagged.integrity.pcmSha256 == clean.integrity.pcmSha256,
            "Retagging changed the decoded PCM hash.");
        require(retagged.integrity.fileSha256 != clean.integrity.fileSha256,
            "File hash did not detect the metadata-only change.");

        MediaRecord truncated;
        DecodeAnalyzer::Analyze(argv[3], truncated, false);
        require(truncated.integrity.status == "failed",
            "Truncated FLAC was not reported as an integrity failure.");
        require(truncated.integrity.firstErrorPosition.has_value(),
            "Truncated FLAC failure did not report an error position.");

        MediaRecord dualMono;
        DecodeAnalyzer::Analyze(argv[4], dualMono, true);
        require(dualMono.signal.measurements.at("stereoCorrelation") > 0.999,
            "In-phase stereo was not identified as correlated.");
        require(!dualMono.findings.empty() && dualMono.findings.front().code == "DUAL_MONO",
            "Dual-mono finding was not generated.");

        MediaRecord antiphase;
        DecodeAnalyzer::Analyze(argv[5], antiphase, true);
        require(antiphase.signal.measurements.at("stereoCorrelation") < -0.999,
            "Antiphase stereo was not measured per channel.");
        require(!antiphase.findings.empty() &&
            antiphase.findings.front().code == "PHASE_INVERTED_CHANNEL",
            "Phase-inverted channel finding was not generated.");

        MediaRecord padded;
        padded.declaredBitsPerSample = 24;
        DecodeAnalyzer::Analyze(argv[6], padded, true);
        require(padded.signal.usedBits == 16,
            "24-bit FLAC made from 16-bit samples did not report 16 used bits.");
        require(!padded.findings.empty() && padded.findings.front().code == "ZERO_PADDED_HIRES",
            "Zero-padded high-resolution finding was not generated.");

        MediaRecord fullScale;
        DecodeAnalyzer::Analyze(argv[7], fullScale, true);
        require(fullScale.signal.clippedSamples.has_value() &&
            *fullScale.signal.clippedSamples > 0,
            "Positive full-scale integer samples were not counted as clipped.");
        require(std::ranges::any_of(fullScale.findings, [](const CatalogFinding& finding) {
            return finding.code == "CLIPPED_SAMPLES";
        }), "Full-scale integer samples did not produce a clipping finding.");

        MediaRecord silence;
        silence.sampleRateHz = 48000;
        DecodeAnalyzer::Analyze(argv[8], silence, true);
        require(silence.signal.leadingSilenceSeconds.has_value() &&
            std::abs(*silence.signal.leadingSilenceSeconds - 0.2) <= 0.02,
            "Leading silence duration is outside the expected tolerance.");
        require(silence.signal.trailingSilenceSeconds.has_value() &&
            std::abs(*silence.signal.trailingSilenceSeconds - 0.2) <= 0.02,
            "Trailing silence duration is outside the expected tolerance.");
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
