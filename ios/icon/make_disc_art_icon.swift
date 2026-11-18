// Makes an app icon from the original 1996 disc artwork: a square crop of the
// Allied disc's installer screen (SETUP95/SETUP.BMP, 640x480) with the
// Command & Conquer Red Alert logo along the top.
//
// The artwork is EA's, so the result is a local file and stays out of the
// repository: write it to assets/AppIcon-1024.png (git-ignored) and the iOS
// build uses it instead of the original-design icon in ios/Assets.xcassets.
//
//   swift ios/icon/make_disc_art_icon.swift /Volumes/<allied disc>/SETUP95/SETUP.BMP assets/AppIcon-1024.png

import AppKit
import CoreGraphics
import Foundation

let arguments = CommandLine.arguments
guard arguments.count >= 2 else {
	FileHandle.standardError.write("usage: make_disc_art_icon.swift <SETUP.BMP> [output.png]\n".data(using: .utf8)!)
	exit(2)
}
let input = arguments[1]
let output = arguments.count > 2 ? arguments[2] : "assets/AppIcon-1024.png"

guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: input) as CFURL, nil),
	let artwork = CGImageSourceCreateImageAtIndex(source, 0, nil)
else { fatalError("could not read \(input)") }
guard artwork.width == 640, artwork.height == 480 else {
	fatalError("expected the 640x480 installer screen, got \(artwork.width)x\(artwork.height)")
}

// The logo spans x 226...632 at the top; a 406-pixel square below it shows the
// missile and red wireframe drawings and stops above the "INSTALLATION" caption.
guard let square = artwork.cropping(to: CGRect(x: 226, y: 0, width: 406, height: 406)) else {
	fatalError("could not crop the artwork")
}

let size = 1024
guard let context = CGContext(
	data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
	space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
else { fatalError("could not create the drawing context") }
context.interpolationQuality = .high
context.draw(square, in: CGRect(x: 0, y: 0, width: size, height: size))

guard let image = context.makeImage(),
	let png = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])
else { fatalError("could not encode the icon") }
let destination = URL(fileURLWithPath: output)
try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
try png.write(to: destination)
print("wrote \(output)")
