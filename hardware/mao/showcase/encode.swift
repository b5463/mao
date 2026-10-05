// Encode a PNG/JPEG frame sequence (and an optional WAV) into an H.264/AAC MP4 with AVFoundation.
//   swiftc -O encode.swift -o build/encode
//   build/encode <frames_dir> <fps> <out.mp4> [audio.wav] [bitrate_bps]
import AVFoundation
import CoreGraphics
import Foundation
import ImageIO

let args = CommandLine.arguments
guard args.count >= 4 else { print("usage: encode <frames_dir> <fps> <out.mp4> [audio.wav] [bitrate]"); exit(2) }
let dir = URL(fileURLWithPath: args[1])
let fps = Int32(args[2])!
let out = URL(fileURLWithPath: args[3])
let audioURL: URL? = args.count > 4 && args[4] != "-" ? URL(fileURLWithPath: args[4]) : nil
let bitrate = args.count > 5 ? Int(args[5])! : 24_000_000

let files = try FileManager.default.contentsOfDirectory(atPath: dir.path).filter { $0.hasSuffix(".png") || $0.hasSuffix(".jpg") }.sorted()
func load(_ name: String) -> CGImage {
    let src = CGImageSourceCreateWithURL(dir.appendingPathComponent(name) as CFURL, nil)!
    return CGImageSourceCreateImageAtIndex(src, 0, nil)!
}
let first = load(files[0])
let W = first.width, H = first.height
try? FileManager.default.removeItem(at: out)
let writer = try AVAssetWriter(outputURL: out, fileType: .mp4)
writer.shouldOptimizeForNetworkUse = true

let vIn = AVAssetWriterInput(mediaType: .video, outputSettings: [
    AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: W, AVVideoHeightKey: H,
    AVVideoColorPropertiesKey: [AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
                                AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
                                AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2],
    AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: bitrate,
                                      AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                                      AVVideoMaxKeyFrameIntervalKey: Int(fps) * 2,
                                      AVVideoAllowFrameReorderingKey: true]])
vIn.expectsMediaDataInRealTime = false
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: vIn, sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32ARGB,
    kCVPixelBufferWidthKey as String: W, kCVPixelBufferHeightKey as String: H])
writer.add(vIn)

var aIn: AVAssetWriterInput? = nil
var reader: AVAssetReader? = nil
var aOut: AVAssetReaderTrackOutput? = nil
if let url = audioURL {
    let asset = AVURLAsset(url: url)
    let track = asset.tracks(withMediaType: .audio).first!
    reader = try AVAssetReader(asset: asset)
    aOut = AVAssetReaderTrackOutput(track: track, outputSettings: [
        AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false,
        AVLinearPCMIsBigEndianKey: false, AVLinearPCMIsNonInterleaved: false])
    reader!.add(aOut!)
    let ai = AVAssetWriterInput(mediaType: .audio, outputSettings: [
        AVFormatIDKey: kAudioFormatMPEG4AAC, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2, AVEncoderBitRateKey: 256000])
    ai.expectsMediaDataInRealTime = false
    writer.add(ai)
    aIn = ai
}

writer.startWriting()
writer.startSession(atSourceTime: .zero)
reader?.startReading()
let group = DispatchGroup()
let cs = CGColorSpace(name: CGColorSpace.sRGB)!

group.enter()
var next = 0
vIn.requestMediaDataWhenReady(on: DispatchQueue(label: "video")) {
    while vIn.isReadyForMoreMediaData {
        if next >= files.count { vIn.markAsFinished(); group.leave(); return }
        autoreleasepool {
            let img = load(files[next])
            var pb: CVPixelBuffer? = nil
            CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &pb)
            CVPixelBufferLockBaseAddress(pb!, [])
            let ctx = CGContext(data: CVPixelBufferGetBaseAddress(pb!), width: W, height: H, bitsPerComponent: 8,
                                bytesPerRow: CVPixelBufferGetBytesPerRow(pb!), space: cs,
                                bitmapInfo: CGImageAlphaInfo.noneSkipFirst.rawValue)!
            ctx.draw(img, in: CGRect(x: 0, y: 0, width: W, height: H))
            CVPixelBufferUnlockBaseAddress(pb!, [])
            adaptor.append(pb!, withPresentationTime: CMTime(value: CMTimeValue(next), timescale: fps))
            next += 1
            if next % 150 == 0 { print("video \(next)/\(files.count)") }
        }
    }
}
if let ai = aIn, let ao = aOut {
    group.enter()
    ai.requestMediaDataWhenReady(on: DispatchQueue(label: "audio")) {
        while ai.isReadyForMoreMediaData {
            if let sb = ao.copyNextSampleBuffer() { ai.append(sb) } else { ai.markAsFinished(); group.leave(); return }
        }
    }
}
group.wait()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
if writer.status != .completed { print("failed: \(String(describing: writer.error))"); exit(1) }
print("wrote \(out.path): \(files.count) frames \(W)x\(H) @ \(fps) fps" + (audioURL != nil ? " + audio" : ""))
