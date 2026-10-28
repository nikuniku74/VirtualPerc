// Production PNG exports: ImageGen supplies the glass mark; native type stays crisp.
import AppKit

guard CommandLine.arguments.count == 3 else {
    fatalError("usage: swift export-brand.swift <transparent-mark.png> <output-directory>")
}
let sourceURL = URL(fileURLWithPath: CommandLine.arguments[1])
let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
guard let image = NSImage(contentsOf: sourceURL),
      let mark = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    fatalError("Cannot read transparent mark")
}
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

func render(_ side: Int, _ name: String, opaque: Bool = false, draw: (CGContext) -> Void) throws {
    guard let context = CGContext(data: nil, width: side, height: side,
        bitsPerComponent: 8, bytesPerRow: side * 4,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: (opaque ? CGImageAlphaInfo.noneSkipLast : .premultipliedLast).rawValue) else {
        fatalError("Cannot allocate brand canvas")
    }
    context.interpolationQuality = .high
    draw(context)
    guard let result = context.makeImage(),
          let data = NSBitmapImageRep(cgImage: result).representation(using: .png, properties: [:]) else {
        fatalError("Cannot encode brand PNG")
    }
    try data.write(to: output.appendingPathComponent(name), options: .atomic)
}

try render(1024, "logo-1024.png") { context in
    context.draw(mark, in: CGRect(x: 187, y: 320, width: 650, height: 650))
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: false)
    for (text, size, tracking, y) in [("VIRTUAL", 145.0, 5.0, 140.0),
                                     ("PERCUSSIONIST", 62.0, 9.0, 95.0)] {
        let font = NSFont(name: text == "VIRTUAL" ? "AvenirNext-Heavy" : "AvenirNext-Medium", size: size)
            ?? NSFont.systemFont(ofSize: size, weight: .semibold)
        let line = NSAttributedString(string: text, attributes: [
            .font: font, .kern: tracking, .foregroundColor: NSColor.white])
        precondition(line.size().width < 920, "Wordmark exceeds safe margin")
        line.draw(at: NSPoint(x: (1024 - line.size().width) / 2, y: y))
    }
    NSGraphicsContext.restoreGraphicsState()
}
for (side, name) in [(128, "mark-128.png"), (64, "favicon-64.png")] {
    try render(side, name) { context in
        context.draw(mark, in: CGRect(x: 0, y: 0, width: side, height: side))
    }
}
for (side, name) in [(1024, "icon-1024.png"), (180, "apple-touch-180.png")] {
    try render(side, name, opaque: true) { context in
        let scale = CGFloat(side) / 1024
        context.scaleBy(x: scale, y: scale)
        context.setFillColor(CGColor(red: 0.035, green: 0.018, blue: 0.045, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: 1024, height: 1024))
        let colors: [CGFloat] = [0.13, 0.015, 0.075, 1, 0.035, 0.018, 0.045, 1]
        if let glow = CGGradient(colorSpace: CGColorSpaceCreateDeviceRGB(),
                                 colorComponents: colors, locations: [0, 1], count: 2) {
            context.drawRadialGradient(glow, startCenter: CGPoint(x: 512, y: 512), startRadius: 0,
                endCenter: CGPoint(x: 512, y: 512), endRadius: 720, options: .drawsAfterEndLocation)
        }
        context.draw(mark, in: CGRect(x: 64, y: 64, width: 896, height: 896))
    }
}
print("Transparent lockup and matching opaque app icons exported to \(output.path)")
