// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation

enum FlagRenderer {
    static let pixelSize = 40

    static func trackedEndpoint(_ airport: Airport) throws -> Data {
        guard let country = AirportCityCatalog.flagCountry(for: airport) else {
            throw TrackedPackageProof.fail("The country of an endpoint airport could not be verified. Refresh the flight before syncing its flags.")
        }
        return try rgb565Flag(countryCode: country)
    }

    static func rgb565Flag(countryCode: String?) throws -> Data {
        let code = normalizedCountryCode(countryCode)
        let size = NSSize(width: pixelSize, height: pixelSize)
        let image = NSImage(size: size)
        image.lockFocus()

        NSGraphicsContext.current?.imageInterpolation = .high
        NSColor.black.setFill()
        NSRect(origin: .zero, size: size).fill()

        guard let code else {
            image.unlockFocus()
            return try ImageWriter.rgb565Data(
                from: image,
                width: pixelSize,
                height: pixelSize
            )
        }

        let outerRect = NSRect(x: 0.75, y: 0.75, width: 38.5, height: 38.5)
        NSColor(calibratedWhite: 0.72, alpha: 0.82).setFill()
        NSBezierPath(ovalIn: outerRect).fill()

        let flagRect = NSRect(x: 2.6, y: 2.6, width: 34.8, height: 34.8)
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(ovalIn: flagRect).addClip()
        if let image = flagImage(code) {
            let sourceSize = image.size
            let sourceAspect =
                sourceSize.width / max(1, sourceSize.height)
            let targetAspect =
                flagRect.width / flagRect.height
            let drawRect: NSRect
            if sourceAspect > targetAspect {
                let width = flagRect.height * sourceAspect
                drawRect = NSRect(
                    x: flagRect.midX - width / 2,
                    y: flagRect.minY,
                    width: width,
                    height: flagRect.height
                )
            } else {
                let height = flagRect.width / sourceAspect
                drawRect = NSRect(
                    x: flagRect.minX,
                    y: flagRect.midY - height / 2,
                    width: flagRect.width,
                    height: height
                )
            }
            image.draw(
                in: drawRect,
                from: .zero,
                operation: .sourceOver,
                fraction: 1,
                respectFlipped: true,
                hints: [.interpolation: NSImageInterpolation.high]
            )
        } else {
            drawFlag(code, in: flagRect)
        }
        NSGraphicsContext.restoreGraphicsState()

        NSColor(calibratedWhite: 0.92, alpha: 0.44).setStroke()
        let border = NSBezierPath(ovalIn: flagRect.insetBy(dx: 0.35, dy: 0.35))
        border.lineWidth = 0.7
        border.stroke()

        image.unlockFocus()
        return try ImageWriter.rgb565Data(
            from: image,
            width: pixelSize,
            height: pixelSize
        )
    }

    private static func drawFlag(_ code: String, in rect: NSRect) {
        switch code {
        case "US":
            horizontalStripes(
                in: rect,
                colors: Array(repeating: [hex(0xb22234), .white], count: 7)
                    .flatMap { $0 }
                    .prefix(13)
                    .map { $0 }
            )
            let canton = NSRect(
                x: rect.minX,
                y: rect.midY,
                width: rect.width * 0.52,
                height: rect.height * 0.50
            )
            fill(canton, hex(0x3c3b6e))
            for row in 0..<4 {
                for column in 0..<4 {
                    dot(
                        x: canton.minX + 3.3 + CGFloat(column) * 3.7,
                        y: canton.minY + 3.0 + CGFloat(row) * 3.7,
                        radius: 0.65,
                        color: .white
                    )
                }
            }
        case "KR":
            fill(rect, .white)
            let center = CGPoint(x: rect.midX, y: rect.midY)
            let radius = rect.width * 0.19
            fillHalfCircle(center: center, radius: radius, upper: true, color: hex(0xcd2e3a))
            fillHalfCircle(center: center, radius: radius, upper: false, color: hex(0x0047a0))
            dot(x: center.x - radius / 2, y: center.y, radius: radius / 2, color: hex(0x0047a0))
            dot(x: center.x + radius / 2, y: center.y, radius: radius / 2, color: hex(0xcd2e3a))
            for angle in [-42.0, 42.0, 138.0, 222.0] {
                drawTrigram(center: center, radius: rect.width * 0.36, angle: angle)
            }
        case "DE":
            horizontalStripes(in: rect, colors: [.black, hex(0xdd0000), hex(0xffce00)])
        case "RU":
            horizontalStripes(in: rect, colors: [.white, hex(0x0039a6), hex(0xd52b1e)])
        case "GB":
            unionJack(in: rect)
        case "FR":
            verticalStripes(in: rect, colors: [hex(0x0055a4), .white, hex(0xef4135)])
        case "IT":
            verticalStripes(in: rect, colors: [hex(0x009246), .white, hex(0xce2b37)])
        case "NL":
            horizontalStripes(in: rect, colors: [hex(0xae1c28), .white, hex(0x21468b)])
        case "BE":
            verticalStripes(in: rect, colors: [.black, hex(0xfdda24), hex(0xef3340)])
        case "AT":
            horizontalStripes(in: rect, colors: [hex(0xed2939), .white, hex(0xed2939)])
        case "ES":
            horizontalStripes(in: rect, colors: [hex(0xaa151b), hex(0xf1bf00), hex(0xf1bf00), hex(0xaa151b)])
        case "JP":
            fill(rect, .white)
            dot(x: rect.midX, y: rect.midY, radius: rect.width * 0.18, color: hex(0xbc002d))
        case "CN":
            fill(rect, hex(0xde2910))
            star(center: CGPoint(x: rect.minX + 10, y: rect.maxY - 10), radius: 5, color: hex(0xffde00))
        case "CA":
            verticalStripes(in: rect, colors: [hex(0xd80621), .white, .white, hex(0xd80621)])
            star(center: CGPoint(x: rect.midX, y: rect.midY), radius: 5, color: hex(0xd80621))
        case "CH":
            fill(rect, hex(0xff0000))
            fill(NSRect(x: rect.midX - 2.2, y: rect.midY - 8, width: 4.4, height: 16), .white)
            fill(NSRect(x: rect.midX - 8, y: rect.midY - 2.2, width: 16, height: 4.4), .white)
        case "SE":
            fill(rect, hex(0x006aa7))
            fill(NSRect(x: rect.minX + 10, y: rect.minY, width: 4, height: rect.height), hex(0xfecc02))
            fill(NSRect(x: rect.minX, y: rect.midY - 2, width: rect.width, height: 4), hex(0xfecc02))
        case "NO":
            nordicCross(in: rect, field: hex(0xba0c2f), outer: .white, inner: hex(0x00205b))
        case "DK":
            nordicCross(in: rect, field: hex(0xc60c30), outer: .white, inner: .white)
        case "FI":
            nordicCross(in: rect, field: .white, outer: hex(0x003580), inner: hex(0x003580))
        case "AE":
            horizontalStripes(in: rect, colors: [hex(0x00732f), .white, .black])
            fill(NSRect(x: rect.minX, y: rect.minY, width: rect.width * 0.27, height: rect.height), hex(0xff0000))
        case "IN":
            horizontalStripes(in: rect, colors: [hex(0xff9933), .white, hex(0x138808)])
            let wheel = NSBezierPath(ovalIn: NSRect(x: rect.midX - 3.7, y: rect.midY - 3.7, width: 7.4, height: 7.4))
            wheel.lineWidth = 1
            hex(0x000080).setStroke()
            wheel.stroke()
        case "BR":
            fill(rect, hex(0x009c3b))
            let diamond = NSBezierPath()
            diamond.move(to: CGPoint(x: rect.midX, y: rect.maxY - 5))
            diamond.line(to: CGPoint(x: rect.maxX - 5, y: rect.midY))
            diamond.line(to: CGPoint(x: rect.midX, y: rect.minY + 5))
            diamond.line(to: CGPoint(x: rect.minX + 5, y: rect.midY))
            diamond.close()
            hex(0xffdf00).setFill()
            diamond.fill()
            dot(x: rect.midX, y: rect.midY, radius: 5.2, color: hex(0x002776))
        case "MX":
            verticalStripes(in: rect, colors: [hex(0x006847), .white, hex(0xce1126)])
            dot(x: rect.midX, y: rect.midY, radius: 2.2, color: hex(0x8b5a2b))
        case "AU", "NZ":
            fill(rect, hex(0x012169))
            star(center: CGPoint(x: rect.maxX - 10, y: rect.midY + 3), radius: 3.2, color: code == "AU" ? .white : hex(0xcc142b))
            star(center: CGPoint(x: rect.maxX - 16, y: rect.minY + 8), radius: 2.2, color: code == "AU" ? .white : hex(0xcc142b))
        default:
            fill(rect, hex(0x202823))
            let text = NSAttributedString(
                string: code,
                attributes: [
                    .font: NSFont.systemFont(ofSize: 12, weight: .bold),
                    .foregroundColor: NSColor.white
                ]
            )
            let size = text.size()
            text.draw(at: CGPoint(x: rect.midX - size.width / 2, y: rect.midY - size.height / 2))
        }
    }

    private static func flagImage(_ code: String) -> NSImage? {
        NSImage(
            contentsOf: ProjectAssetCatalog.assetRoot
                .appendingPathComponent("flags", isDirectory: true)
                .appendingPathComponent("\(code.lowercased()).png")
        )
    }

    private static func horizontalStripes(in rect: NSRect, colors: [NSColor]) {
        let height = rect.height / CGFloat(colors.count)
        for (index, color) in colors.enumerated() {
            fill(
                NSRect(
                    x: rect.minX,
                    y: rect.maxY - CGFloat(index + 1) * height,
                    width: rect.width,
                    height: height + 0.5
                ),
                color
            )
        }
    }

    private static func verticalStripes(in rect: NSRect, colors: [NSColor]) {
        let width = rect.width / CGFloat(colors.count)
        for (index, color) in colors.enumerated() {
            fill(
                NSRect(
                    x: rect.minX + CGFloat(index) * width,
                    y: rect.minY,
                    width: width + 0.5,
                    height: rect.height
                ),
                color
            )
        }
    }

    private static func unionJack(in rect: NSRect) {
        fill(rect, hex(0x012169))
        diagonal(in: rect, width: 7.0, color: .white)
        diagonal(in: rect, width: 3.0, color: hex(0xc8102e))
        fill(NSRect(x: rect.midX - 4.5, y: rect.minY, width: 9, height: rect.height), .white)
        fill(NSRect(x: rect.minX, y: rect.midY - 4.5, width: rect.width, height: 9), .white)
        fill(NSRect(x: rect.midX - 2.5, y: rect.minY, width: 5, height: rect.height), hex(0xc8102e))
        fill(NSRect(x: rect.minX, y: rect.midY - 2.5, width: rect.width, height: 5), hex(0xc8102e))
    }

    private static func diagonal(in rect: NSRect, width: CGFloat, color: NSColor) {
        color.setStroke()
        for endpoints in [
            (CGPoint(x: rect.minX, y: rect.minY), CGPoint(x: rect.maxX, y: rect.maxY)),
            (CGPoint(x: rect.minX, y: rect.maxY), CGPoint(x: rect.maxX, y: rect.minY))
        ] {
            let path = NSBezierPath()
            path.move(to: endpoints.0)
            path.line(to: endpoints.1)
            path.lineWidth = width
            path.stroke()
        }
    }

    private static func nordicCross(in rect: NSRect, field: NSColor, outer: NSColor, inner: NSColor) {
        fill(rect, field)
        fill(NSRect(x: rect.minX + 10, y: rect.minY, width: 7, height: rect.height), outer)
        fill(NSRect(x: rect.minX, y: rect.midY - 3.5, width: rect.width, height: 7), outer)
        fill(NSRect(x: rect.minX + 12, y: rect.minY, width: 3, height: rect.height), inner)
        fill(NSRect(x: rect.minX, y: rect.midY - 1.5, width: rect.width, height: 3), inner)
    }

    private static func fillHalfCircle(center: CGPoint, radius: CGFloat, upper: Bool, color: NSColor) {
        let path = NSBezierPath()
        path.move(to: center)
        path.appendArc(
            withCenter: center,
            radius: radius,
            startAngle: upper ? 0 : 180,
            endAngle: upper ? 180 : 360
        )
        path.close()
        color.setFill()
        path.fill()
    }

    private static func drawTrigram(center: CGPoint, radius: CGFloat, angle: CGFloat) {
        NSGraphicsContext.saveGraphicsState()
        let transform = NSAffineTransform()
        transform.translateX(by: center.x, yBy: center.y)
        transform.rotate(byDegrees: angle)
        transform.translateX(by: radius, yBy: 0)
        transform.concat()
        NSColor.black.setFill()
        for offset in [-3.0, 0.0, 3.0] {
            NSRect(x: -4, y: offset - 0.65, width: 8, height: 1.3).fill()
        }
        NSGraphicsContext.restoreGraphicsState()
    }

    private static func star(center: CGPoint, radius: CGFloat, color: NSColor) {
        let path = NSBezierPath()
        for point in 0..<10 {
            let angle = CGFloat(point) * .pi / 5 - .pi / 2
            let pointRadius = point.isMultiple(of: 2) ? radius : radius * 0.42
            let p = CGPoint(
                x: center.x + cos(angle) * pointRadius,
                y: center.y + sin(angle) * pointRadius
            )
            if point == 0 { path.move(to: p) } else { path.line(to: p) }
        }
        path.close()
        color.setFill()
        path.fill()
    }

    private static func dot(x: CGFloat, y: CGFloat, radius: CGFloat, color: NSColor) {
        color.setFill()
        NSBezierPath(
            ovalIn: NSRect(
                x: x - radius,
                y: y - radius,
                width: radius * 2,
                height: radius * 2
            )
        ).fill()
    }

    private static func fill(_ rect: NSRect, _ color: NSColor) {
        color.setFill()
        rect.fill()
    }

    private static func hex(_ value: UInt32) -> NSColor {
        NSColor(
            calibratedRed: CGFloat((value >> 16) & 0xff) / 255,
            green: CGFloat((value >> 8) & 0xff) / 255,
            blue: CGFloat(value & 0xff) / 255,
            alpha: 1
        )
    }

    private static func normalizedCountryCode(_ value: String?) -> String? {
        guard let value else { return nil }
        let normalized = value
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        guard normalized.count == 2,
              normalized.unicodeScalars.allSatisfy({
                  $0.value >= 65 && $0.value <= 90
              })
        else {
            return nil
        }
        return normalized
    }
}
