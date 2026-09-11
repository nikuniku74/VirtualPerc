import AVFoundation
import Foundation

// One-off/reproducible fixture builder for the full mixer-send recording.
// Each output contains three seconds of silence followed by 140 seconds from
// the middle of a song, matching bench_live.py's acquisition assumptions.
guard CommandLine.arguments.count == 3 || CommandLine.arguments.count == 5 else {
    fputs("usage: swift extract_live.swift input.m4a output-dir [start-sec duration-sec]\n", stderr)
    exit(2)
}

let source = URL(fileURLWithPath: CommandLine.arguments[1])
let destination = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
try FileManager.default.createDirectory(at: destination,
                                        withIntermediateDirectories: true)

let starts: [Double] = CommandLine.arguments.count == 5
    ? [Double(CommandLine.arguments[3]) ?? 0.0]
    : [75, 370, 680, 975, 1270]
let leadSeconds = 3.0
let bodySeconds = CommandLine.arguments.count == 5
    ? (Double(CommandLine.arguments[4]) ?? 140.0)
    : 140.0

for (index, start) in starts.enumerated() {
    let input = try AVAudioFile(forReading: source)
    let format = input.processingFormat
    let rate = format.sampleRate
    let outputURL = destination.appendingPathComponent("s\(index + 1).wav")
    let wavSettings: [String: Any] = [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVSampleRateKey: rate,
        AVNumberOfChannelsKey: format.channelCount,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsFloatKey: false,
        AVLinearPCMIsBigEndianKey: false,
        AVLinearPCMIsNonInterleaved: false
    ]
    let output = try AVAudioFile(forWriting: outputURL,
                                 settings: wavSettings,
                                 commonFormat: format.commonFormat,
                                 interleaved: format.isInterleaved)
    let capacity: AVAudioFrameCount = 8192
    guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: capacity),
          let silence = AVAudioPCMBuffer(pcmFormat: format,
                                         frameCapacity: AVAudioFrameCount(rate * leadSeconds))
    else { fatalError("cannot allocate audio buffers") }

    silence.frameLength = silence.frameCapacity
    for channel in 0..<Int(format.channelCount) {
        if let samples = silence.floatChannelData?[channel] {
            samples.initialize(repeating: 0, count: Int(silence.frameLength))
        }
    }
    try output.write(from: silence)

    input.framePosition = AVAudioFramePosition(start * rate)
    var remaining = AVAudioFramePosition(bodySeconds * rate)
    while remaining > 0 {
        let count = min(AVAudioFrameCount(remaining), capacity)
        try input.read(into: buffer, frameCount: count)
        if buffer.frameLength == 0 { break }
        try output.write(from: buffer)
        remaining -= AVAudioFramePosition(buffer.frameLength)
    }
}
