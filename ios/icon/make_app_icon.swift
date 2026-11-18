// Draws the iOS app icon: an original design (no game art), so it can live in
// the repository, which ships no Red Alert assets.
//
//   swift ios/icon/make_app_icon.swift ios/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png

import AppKit
import CoreGraphics
import Foundation

let size: CGFloat = 1024
let output = CommandLine.arguments.count > 1
	? CommandLine.arguments[1]
	: "ios/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"

func color(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
	CGColor(
		srgbRed: CGFloat((hex >> 16) & 0xFF) / 255,
		green: CGFloat((hex >> 8) & 0xFF) / 255,
		blue: CGFloat(hex & 0xFF) / 255,
		alpha: alpha)
}

func starPath(center: CGPoint, outer: CGFloat, inner: CGFloat) -> CGPath {
	let path = CGMutablePath()
	for index in 0..<10 {
		let radius = index % 2 == 0 ? outer : inner
		let angle = CGFloat.pi / 2 + CGFloat(index) * CGFloat.pi / 5
		let point = CGPoint(x: center.x + radius * cos(angle), y: center.y + radius * sin(angle))
		if index == 0 { path.move(to: point) } else { path.addLine(to: point) }
	}
	path.closeSubpath()
	return path
}

let space = CGColorSpace(name: CGColorSpace.sRGB)!
guard let context = CGContext(
	data: nil, width: Int(size), height: Int(size), bitsPerComponent: 8, bytesPerRow: 0,
	space: space, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
else { fatalError("could not create the drawing context") }

// CoreGraphics has y going up; the design below uses that orientation.
let middle = CGPoint(x: size / 2, y: size * 0.56)

// Background: a dark red alert glow fading to near black.
let glow = CGGradient(
	colorsSpace: space,
	colors: [color(0x9A0E0A), color(0x420605), color(0x0B0808)] as CFArray,
	locations: [0, 0.55, 1])!
context.drawRadialGradient(
	glow, startCenter: middle, startRadius: 0, endCenter: middle, endRadius: size * 0.75,
	options: [.drawsAfterEndLocation])

// Radar rings and sweep lines, a nod to the sidebar radar.
context.setStrokeColor(color(0xFF4A3A, 0.16))
context.setLineWidth(6)
for radius in stride(from: CGFloat(150), through: 700, by: 110) {
	context.strokeEllipse(in: CGRect(x: middle.x - radius, y: middle.y - radius, width: radius * 2, height: radius * 2))
}
context.setLineWidth(4)
for angle in stride(from: CGFloat(0), to: CGFloat.pi, by: CGFloat.pi / 4) {
	context.move(to: CGPoint(x: middle.x - 760 * cos(angle), y: middle.y - 760 * sin(angle)))
	context.addLine(to: CGPoint(x: middle.x + 760 * cos(angle), y: middle.y + 760 * sin(angle)))
}
context.strokePath()

// Star: drop shadow, gold rim, red body with a light-to-dark fill, inner bevel.
let star = starPath(center: middle, outer: 330, inner: 136)
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: -18), blur: 40, color: color(0x000000, 0.7))
context.addPath(star)
context.setFillColor(color(0xE6BF57))
context.fillPath()
context.restoreGState()

context.addPath(star)
context.setStrokeColor(color(0xE6BF57))
context.setLineWidth(34)
context.setLineJoin(.miter)
context.strokePath()

context.saveGState()
context.addPath(star)
context.clip()
let body = CGGradient(
	colorsSpace: space, colors: [color(0xFF3B26), color(0xC10F0B), color(0x6E0605)] as CFArray,
	locations: [0, 0.5, 1])!
context.drawLinearGradient(
	body, start: CGPoint(x: middle.x, y: middle.y + 330), end: CGPoint(x: middle.x, y: middle.y - 330), options: [])
// Facets: each point is split into a light and a dark half, like a pressed metal star.
for index in 0..<5 {
	let angle = CGFloat.pi / 2 + CGFloat(index) * 2 * CGFloat.pi / 5
	let tip = CGPoint(x: middle.x + 330 * cos(angle), y: middle.y + 330 * sin(angle))
	let side = angle + CGFloat.pi / 5
	let valley = CGPoint(x: middle.x + 136 * cos(side), y: middle.y + 136 * sin(side))
	let facet = CGMutablePath()
	facet.move(to: middle)
	facet.addLine(to: tip)
	facet.addLine(to: valley)
	facet.closeSubpath()
	context.addPath(facet)
	context.setFillColor(color(0x000000, 0.22))
	context.fillPath()
}
context.restoreGState()

context.addPath(starPath(center: middle, outer: 330, inner: 136))
context.setStrokeColor(color(0x7A5A12, 0.9))
context.setLineWidth(6)
context.strokePath()

// Title: heavy condensed letters with a dark outline so they read at small sizes.
let fontNames = ["Impact", "Futura-CondensedExtraBold", "AvenirNextCondensed-Heavy", "HelveticaNeue-CondensedBlack"]
let font = fontNames.lazy.compactMap { NSFont(name: $0, size: 168) }.first ?? NSFont.boldSystemFont(ofSize: 160)
let title = NSAttributedString(string: "RED ALERT", attributes: [
	.font: font,
	.kern: 6,
	.foregroundColor: NSColor(srgbRed: 0.98, green: 0.92, blue: 0.80, alpha: 1),
	.strokeColor: NSColor(srgbRed: 0.08, green: 0.02, blue: 0.02, alpha: 1),
	.strokeWidth: -7,
])
let line = CTLineCreateWithAttributedString(title)
let bounds = CTLineGetImageBounds(line, context)
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: -8), blur: 18, color: color(0x000000, 0.8))
// Round joins keep the outline from spiking at sharp letter corners such as the A's apex.
context.setLineJoin(.round)
context.textPosition = CGPoint(x: (size - bounds.width) / 2 - bounds.minX, y: 92)
CTLineDraw(line, context)
context.restoreGState()

guard let image = context.makeImage() else { fatalError("could not render the icon") }
let destination = URL(fileURLWithPath: output)
try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
let rep = NSBitmapImageRep(cgImage: image)
guard let png = rep.representation(using: .png, properties: [:]) else { fatalError("could not encode the PNG") }
try png.write(to: destination)
print("wrote \(output)")
