// swift-tools-version: 6.0

// AirESP32ace
// Created by VM, August 2026

import PackageDescription

let package = Package(
    name: "AirESP32ace",
    platforms: [
        .macOS(.v15)
    ],
    products: [
        .executable(name: "AirESP32ace", targets: ["AirspaceCompanion"])
    ],
    targets: [
        .executableTarget(
            name: "AirspaceCompanion",
            path: "Sources/AirspaceCompanion",
            resources: [
                .process("Resources")
            ]
        ),
        .testTarget(
            name: "AirspaceCompanionTests",
            dependencies: ["AirspaceCompanion"]
        )
    ]
)
