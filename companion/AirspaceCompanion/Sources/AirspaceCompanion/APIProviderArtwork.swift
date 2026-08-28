// AirESP32ace
// Created by VM, August 2026

import AppKit
import SwiftUI

enum APIProviderArtwork: String, CaseIterable {
    case flightAware = "api_logo_flightaware"
    case openSky = "api_logo_opensky"
    case airLabs = "api_logo_airlabs"

    var displayName: String {
        switch self {
        case .flightAware: "FlightAware"
        case .openSky: "OpenSky"
        case .airLabs: "AirLabs"
        }
    }

    var deviceSize: CGSize {
        switch self {
        case .flightAware: CGSize(width: 74, height: 40)
        case .openSky: CGSize(width: 126, height: 38)
        case .airLabs: CGSize(width: 126, height: 30)
        }
    }

    var image: NSImage? {
        guard let url = AppResources.bundle.url(forResource: rawValue, withExtension: "png") else {
            return nil
        }
        return NSImage(contentsOf: url)
    }
}

struct APIProviderLogo: View {
    let provider: APIProviderArtwork
    var width: CGFloat
    var height: CGFloat

    var body: some View {
        Group {
            if let image = provider.image {
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.high)
                    .antialiased(true)
                    .scaledToFit()
            } else {
                Text(provider.displayName)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundStyle(Color(red: 0.38, green: 0.79, blue: 1.0))
            }
        }
        .frame(width: width, height: height)
        .accessibilityLabel(provider.displayName)
    }
}
