// AirESP32ace
// Created by VM, August 2026

import AppKit
import Foundation
import SwiftUI
import UniformTypeIdentifiers

enum DeviceManagementGuideStep: String, CaseIterable {
    case chooser
    case firmware
    case setupWarning
    case setupConnect
    case setupForm
    case firmwareWorking
    case firmwareSuccess
}

struct DeviceOutlineGlyph: View {
    var color: Color = .white

    var body: some View {
        GeometryReader { proxy in
            let side = min(proxy.size.width, proxy.size.height)
            let stroke = max(1.2, side * 0.085)
            ZStack {
                Circle()
                    .stroke(color, lineWidth: stroke)
                    .padding(stroke * 0.58)

                Capsule(style: .continuous)
                    .fill(color)
                    .frame(
                        width: stroke * 1.18,
                        height: side * 0.18
                    )
                    .offset(
                        x: side * 0.455,
                        y: -side * 0.235
                    )

                Capsule(style: .continuous)
                    .fill(color)
                    .frame(
                        width: stroke * 1.12,
                        height: side * 0.13
                    )
                    .offset(
                        x: side * 0.445,
                        y: side * 0.235
                    )
            }
            .frame(width: side, height: side)
            .position(
                x: proxy.size.width / 2,
                y: proxy.size.height / 2
            )
        }
        .accessibilityHidden(true)
    }
}

struct DeviceManagementView: View {
    @ObservedObject var model: CompanionModel
    let dismiss: () -> Void
    private let guideStep: DeviceManagementGuideStep?

    @StateObject private var installer = DeviceFirmwareInstaller()
    @State private var step: Step = .chooser
    @State private var customFirmwareURL: URL?
    @State private var setupError: String?
    @State private var pendingInstallation: PendingInstallation?

    private enum PendingInstallation {
        case firmware
        case newDevice
    }

    private enum Step: Equatable {
        case chooser
        case firmware
        case setupWarning
        case setupConnect
        case setupForm
        case working(title: String)
        case success(title: String, detail: String)
        case failure(title: String, detail: String)
    }

    init(
        model: CompanionModel,
        guideStep: DeviceManagementGuideStep? = nil,
        dismiss: @escaping () -> Void
    ) {
        self.model = model
        self.dismiss = dismiss
        self.guideStep = guideStep
        _step = State(initialValue: Self.initialStep(for: guideStep))
    }

    private static func initialStep(
        for guideStep: DeviceManagementGuideStep?
    ) -> Step {
        switch guideStep {
        case .firmware:
            .firmware
        case .setupWarning:
            .setupWarning
        case .setupConnect:
            .setupConnect
        case .setupForm:
            .setupForm
        case .firmwareWorking:
            .working(title: "Updating firmware")
        case .firmwareSuccess:
            .success(
                title: "Firmware updated",
                detail: "The tracker restarted with the new firmware. Its Wi-Fi, API, city, and display settings were preserved."
            )
        case .chooser, .none:
            .chooser
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            header

            Divider().overlay(.white.opacity(0.08))

            ZStack {
                switch step {
                case .chooser:
                    chooser
                        .transition(panelTransition)
                case .firmware:
                    firmwareUpdate
                        .transition(panelTransition)
                case .setupWarning:
                    setupWarning
                        .transition(panelTransition)
                case .setupConnect:
                    setupConnection
                        .transition(panelTransition)
                case .setupForm:
                    setupForm
                        .transition(panelTransition)
                case let .working(title):
                    working(title: title)
                        .transition(panelTransition)
                case let .success(title, detail):
                    result(
                        title: title,
                        detail: detail,
                        success: true
                    )
                    .transition(panelTransition)
                case let .failure(title, detail):
                    result(
                        title: title,
                        detail: detail,
                        success: false
                    )
                    .transition(panelTransition)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .animation(.easeInOut(duration: 0.24), value: step)
        }
        .frame(width: 650, height: 610)
        .background(
            LinearGradient(
                colors: [
                    Color(red: 0.025, green: 0.032, blue: 0.031),
                    Color.black,
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
        )
        .clipShape(RoundedRectangle(cornerRadius: 26, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 26, style: .continuous)
                .stroke(.white.opacity(0.12), lineWidth: 1)
        )
        .shadow(color: .black.opacity(0.75), radius: 45, y: 24)
        .onAppear {
            installer.startMonitoring()
        }
        .onDisappear {
            installer.stopMonitoring()
        }
        .onChange(of: installer.serialPort) { _, newPort in
            guard step == .setupConnect, newPort != nil else { return }
            Task { @MainActor in
                try? await Task.sleep(for: .milliseconds(450))
                guard step == .setupConnect,
                      installer.serialPort != nil
                else { return }
                withAnimation {
                    step = .setupForm
                }
            }
        }
        .alert(
            "Confirm USB target",
            isPresented: Binding(
                get: { pendingInstallation != nil },
                set: { if !$0 { pendingInstallation = nil } }
            )
        ) {
            Button("Cancel", role: .cancel) {
                pendingInstallation = nil
            }
            Button(
                pendingInstallation == .newDevice
                    ? "Erase & Set Up"
                    : "Install Firmware",
                role: .destructive
            ) {
                let action = pendingInstallation
                pendingInstallation = nil
                switch action {
                case .firmware:
                    performFirmwareInstallation()
                case .newDevice:
                    performNewDeviceInstallation()
                case .none:
                    break
                }
            }
        } message: {
            Text(usbConfirmationMessage)
        }
    }

    private var panelTransition: AnyTransition {
        .asymmetric(
            insertion: .opacity.combined(with: .scale(scale: 0.985)),
            removal: .opacity
        )
    }

    private var header: some View {
        HStack(spacing: 12) {
            DeviceOutlineGlyph(color: DeviceSetupTheme.green)
                .frame(width: 25, height: 25)

            VStack(alignment: .leading, spacing: 2) {
                Text("AirESP32ace")
                    .font(.system(size: 19, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text(headerSubtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            Button {
                if !installer.isBusy {
                    dismiss()
                }
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 12, weight: .semibold))
                    .frame(width: 30, height: 30)
                    .background(.white.opacity(0.055), in: Circle())
            }
            .buttonStyle(.plain)
            .foregroundStyle(.secondary)
            .disabled(installer.isBusy)
        }
        .padding(.horizontal, 22)
        .frame(height: 68)
    }

    private var headerSubtitle: String {
        switch step {
        case .chooser: "One paired device"
        case .firmware: "Firmware Update"
        case .setupWarning, .setupConnect, .setupForm: "Set Up New Device"
        case .working: "Installing"
        case .success: "Complete"
        case .failure: "Installation stopped"
        }
    }

    private var chooser: some View {
        VStack(spacing: 24) {
            Spacer(minLength: 12)

            SetupDeviceArtwork(size: 178)

            VStack(spacing: 7) {
                Text("Manage your device")
                    .font(.system(size: 25, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text("Firmware and first-time setup use a direct USB-C connection.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 14) {
                choiceButton(
                    title: "Set Up New Device",
                    detail: "Replace the paired tracker",
                    symbol: "plus.circle.fill"
                ) {
                    step = .setupWarning
                }

                choiceButton(
                    title: "Update Firmware",
                    detail: "Keep settings and data",
                    symbol: "arrow.down.circle.fill"
                ) {
                    step = .firmware
                }
            }
            .frame(maxWidth: 540)

            Spacer(minLength: 18)
        }
        .padding(28)
    }

    private func choiceButton(
        title: String,
        detail: String,
        symbol: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            HStack(spacing: 13) {
                Image(systemName: symbol)
                    .font(.system(size: 22, weight: .medium))
                    .foregroundStyle(DeviceSetupTheme.green)
                VStack(alignment: .leading, spacing: 3) {
                    Text(title)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundStyle(.white)
                    Text(detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 4)
                Image(systemName: "chevron.right")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.tertiary)
            }
            .padding(.horizontal, 16)
            .frame(maxWidth: .infinity, minHeight: 76)
            .background(DeviceSetupTheme.panel)
            .clipShape(RoundedRectangle(cornerRadius: 17, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 17, style: .continuous)
                    .stroke(.white.opacity(0.09), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }

    private var firmwareUpdate: some View {
        VStack(spacing: 20) {
            DeviceCableStatus(
                serialPort: installer.serialPort,
                networkReachable: model.deviceReachable
            )

            usbTargetPicker

            VStack(alignment: .leading, spacing: 13) {
                Text("FIRMWARE")
                    .font(.system(size: 10, weight: .semibold, design: .monospaced))
                    .tracking(1.2)
                    .foregroundStyle(.secondary)

                HStack(spacing: 12) {
                    Image(systemName: "shippingbox.fill")
                        .font(.system(size: 20))
                        .foregroundStyle(DeviceSetupTheme.green)
                        .frame(width: 38, height: 38)
                        .background(.white.opacity(0.055), in: Circle())

                    VStack(alignment: .leading, spacing: 3) {
                        Text(customFirmwareURL?.lastPathComponent ?? "Included Firmware")
                            .font(.system(size: 14, weight: .semibold))
                            .foregroundStyle(.white)
                            .lineLimit(1)
                        Text(customFirmwareURL == nil
                             ? installer.includedFirmwareSummary
                             : "Application image selected from Finder")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    Button("Choose .bin…") {
                        chooseFirmware()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
            }
            .padding(17)
            .frame(maxWidth: 540)
            .background(DeviceSetupTheme.panel)
            .clipShape(RoundedRectangle(cornerRadius: 17, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 17, style: .continuous)
                    .stroke(.white.opacity(0.09), lineWidth: 1)
            )

            Text("Wireless updating is intentionally unavailable in this firmware layout. USB-C prevents an interrupted network update from leaving the tracker unusable.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 500)

            Spacer(minLength: 0)

            actionRow(
                back: { step = .chooser },
                title: "Install Update",
                enabled: installer.serialPort != nil && installer.firmwareAvailable
            ) {
                requestFirmwareInstallation()
            }
        }
        .padding(28)
    }

    private var setupWarning: some View {
        VStack(spacing: 24) {
            Spacer()

            ZStack {
                Circle()
                    .fill(Color.orange.opacity(0.10))
                    .frame(width: 104, height: 104)
                Image(systemName: "exclamationmark.triangle.fill")
                    .font(.system(size: 38, weight: .medium))
                    .foregroundStyle(.orange)
            }

            VStack(spacing: 9) {
                Text("Replace the paired device?")
                    .font(.system(size: 25, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text("Aerospace Companion supports one tracker. Finishing setup will replace the current paired device, while keeping your cities, tracked flights, and Mac-side preferences.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 475)
            }

            Spacer()

            actionRow(
                back: { step = .chooser },
                title: "Continue",
                enabled: true
            ) {
                step = installer.serialPort == nil
                    ? .setupConnect
                    : .setupForm
            }
        }
        .padding(32)
    }

    private var setupConnection: some View {
        VStack(spacing: 18) {
            Spacer(minLength: 12)

            DeviceCableStatus(
                // Keep the guide deterministic even when the developer's
                // tracker happens to be attached while screenshots render.
                serialPort: guideStep == .setupConnect ? nil : installer.serialPort,
                networkReachable: false,
                large: true
            )

            VStack(spacing: 7) {
                Text("Connect the new tracker")
                    .font(.system(size: 23, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text("Use a USB-C data cable. Setup continues automatically when the device appears.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 450)
            }

            Spacer()

            HStack {
                Button("Back") { step = .setupWarning }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                Spacer()
            }
        }
        .padding(32)
    }

    private var setupForm: some View {
        VStack(spacing: 16) {
            HStack {
                connectionPill
                Spacer()
                usbTargetPicker
                    .frame(maxWidth: 340)
            }

            ScrollView {
                VStack(spacing: 13) {
                    setupSection(title: "HOME WI-FI", symbol: "wifi") {
                        HStack(spacing: 10) {
                            setupField("Network name", text: $model.homeWiFiSSID)
                            setupField(
                                "Password",
                                text: $model.homeWiFiPassword,
                                secure: true
                            )
                        }
                    }

                    setupSection(
                        title: "IPHONE HOTSPOT · OPTIONAL",
                        symbol: "personalhotspot"
                    ) {
                        HStack(spacing: 10) {
                            setupField("Hotspot name", text: $model.hotspotWiFiSSID)
                            setupField(
                                "Password",
                                text: $model.hotspotWiFiPassword,
                                secure: true
                            )
                        }
                    }

                    setupSection(title: "FLIGHT DATA", symbol: "airplane") {
                        VStack(spacing: 9) {
                            setupField(
                                "FlightAware AeroAPI key (Mac only)",
                                text: $model.flightAwareAPIKey,
                                secure: true
                            )
                            HStack(spacing: 10) {
                                setupField(
                                    "OpenSky client ID",
                                    text: $model.openSkyClientID
                                )
                                setupField(
                                    "OpenSky client secret",
                                    text: $model.openSkyClientSecret,
                                    secure: true
                                )
                            }
                            setupField(
                                "AirLabs API key",
                                text: $model.airLabsAPIKey,
                                secure: true
                            )
                        }
                    }
                }
            }
            .scrollIndicators(.never)

            if let setupError {
                Text(setupError)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            actionRow(
                back: { step = .setupWarning },
                title: "Set Up Device",
                enabled: installer.serialPort != nil &&
                    !model.homeWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty &&
                    installer.firmwareAvailable
            ) {
                requestNewDeviceInstallation()
            }
        }
        .padding(24)
    }

    private func setupSection<Content: View>(
        title: String,
        symbol: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 11) {
            Label(title, systemImage: symbol)
                .font(.system(size: 10, weight: .semibold, design: .monospaced))
                .tracking(1.0)
                .foregroundStyle(.secondary)
            content()
        }
        .padding(15)
        .background(DeviceSetupTheme.panel)
        .clipShape(RoundedRectangle(cornerRadius: 15, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 15, style: .continuous)
                .stroke(.white.opacity(0.075), lineWidth: 1)
        )
    }

    private func setupField(
        _ placeholder: String,
        text: Binding<String>,
        secure: Bool = false
    ) -> some View {
        MacConfigTextField(
            placeholder: placeholder,
            text: guideStep == nil ? text : .constant(""),
            isSecure: secure
        )
        .frame(minWidth: 0, maxWidth: .infinity)
        .frame(height: 26)
    }

    private var connectionPill: some View {
        HStack(spacing: 7) {
            Circle()
                .fill(installer.serialPort == nil ? Color.orange : DeviceSetupTheme.green)
                .frame(width: 7, height: 7)
            Text(installer.serialPort == nil ? "USB-C disconnected" : "USB-C connected")
                .font(.caption.weight(.medium))
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 11)
        .frame(height: 28)
        .background(.white.opacity(0.06), in: Capsule())
    }

    private var usbTargetPicker: some View {
        HStack(spacing: 9) {
            Image(systemName: "cable.connector")
                .foregroundStyle(
                    installer.serialPort == nil
                        ? Color.orange
                        : DeviceSetupTheme.green
                )

            if installer.serialCandidates.isEmpty {
                Text("No USB target detected")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(.secondary)
            } else {
                Text("USB TARGET")
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .tracking(0.8)
                    .foregroundStyle(.secondary)

                Picker(
                    "USB target",
                    selection: Binding(
                        get: { installer.serialPort ?? "" },
                        set: { installer.selectSerialPort($0) }
                    )
                ) {
                    ForEach(installer.serialCandidates, id: \.path) { candidate in
                        Text(candidate.pickerLabel)
                            .tag(candidate.path)
                    }
                }
                .labelsHidden()
                .controlSize(.small)
            }
        }
        .padding(.horizontal, 12)
        .frame(minHeight: 34)
        .background(.white.opacity(0.05), in: Capsule())
        .overlay(
            Capsule()
                .stroke(.white.opacity(0.09), lineWidth: 1)
        )
    }

    private var usbConfirmationMessage: String {
        guard let candidate = installer.selectedCandidate else {
            return "No USB device is selected. Cancel and connect AirESP32ace with a USB-C data cable."
        }
        let action = pendingInstallation == .newDevice
            ? "erase and configure"
            : "write firmware to"
        return "AirESP32ace will \(action):\n\n\(candidate.confirmationDescription)\n\nVerify this is your AirESP32ace device. Choosing the wrong ESP32-S3 can overwrite its firmware."
    }

    private func working(title: String) -> some View {
        VStack(spacing: 25) {
            Spacer()

            ZStack {
                Circle()
                    .stroke(.white.opacity(0.07), lineWidth: 7)
                Circle()
                    .trim(from: 0, to: max(0.015, installer.progress))
                    .stroke(
                        DeviceSetupTheme.green,
                        style: StrokeStyle(lineWidth: 7, lineCap: .round)
                    )
                    .rotationEffect(.degrees(-90))
                    .animation(.easeOut(duration: 0.18), value: installer.progress)
                Text("\(Int((installer.progress * 100).rounded()))%")
                    .font(.system(size: 28, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .foregroundStyle(.white)
            }
            .frame(width: 132, height: 132)

            VStack(spacing: 7) {
                Text(title)
                    .font(.system(size: 23, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text(installer.statusText)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 440)
            }

            ProgressView(value: installer.progress)
                .progressViewStyle(.linear)
                .tint(DeviceSetupTheme.green)
                .frame(width: 360)

            Text("Keep the USB-C cable connected.")
                .font(.caption)
                .foregroundStyle(.tertiary)

            Spacer()
        }
        .padding(32)
    }

    private func result(
        title: String,
        detail: String,
        success: Bool
    ) -> some View {
        VStack(spacing: 22) {
            Spacer()

            ZStack {
                Circle()
                    .fill((success ? DeviceSetupTheme.green : Color.red).opacity(0.12))
                Image(systemName: success ? "checkmark" : "exclamationmark")
                    .font(.system(size: 38, weight: .semibold))
                    .foregroundStyle(success ? DeviceSetupTheme.green : .red)
            }
            .frame(width: 106, height: 106)

            VStack(spacing: 8) {
                Text(title)
                    .font(.system(size: 25, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                Text(detail)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 470)
            }

            Spacer()

            Button(success ? "OK" : "Try Again") {
                if success {
                    dismiss()
                } else {
                    step = .chooser
                }
            }
            .buttonStyle(.borderedProminent)
            .tint(success ? DeviceSetupTheme.green : .red)
            .controlSize(.large)
            .frame(maxWidth: 240)
        }
        .padding(34)
    }

    private func actionRow(
        back: @escaping () -> Void,
        title: String,
        enabled: Bool,
        action: @escaping () -> Void
    ) -> some View {
        HStack {
            Button("Back", action: back)
                .buttonStyle(.bordered)
                .controlSize(.large)

            Spacer()

            Button(title, action: action)
                .buttonStyle(.borderedProminent)
                .tint(DeviceSetupTheme.green)
                .controlSize(.large)
                .disabled(!enabled)
        }
    }

    private func chooseFirmware() {
        let panel = NSOpenPanel()
        panel.title = "Choose AirESP32ace application firmware"
        panel.message = "Select an ESP32-S3 application .bin file. Device settings are preserved."
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        if let binaryType = UTType(filenameExtension: "bin") {
            panel.allowedContentTypes = [binaryType]
        }
        guard panel.runModal() == .OK else { return }
        customFirmwareURL = panel.url
    }

    private func requestFirmwareInstallation() {
        guard installer.selectedCandidate != nil else { return }
        pendingInstallation = .firmware
    }

    private func performFirmwareInstallation() {
        step = .working(title: "Updating firmware")
        installer.installApplicationFirmware(customImage: customFirmwareURL) { result in
            switch result {
            case .success:
                step = .success(
                    title: "Firmware updated",
                    detail: "The tracker restarted with the new firmware. Its Wi-Fi, API, city, and display settings were preserved."
                )
            case let .failure(error):
                step = .failure(
                    title: "Update failed",
                    detail: error.localizedDescription
                )
            }
        }
    }

    private func requestNewDeviceInstallation() {
        let homeSSID = model.homeWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !homeSSID.isEmpty else {
            setupError = "Home Wi-Fi name is required."
            return
        }
        setupError = nil
        guard installer.selectedCandidate != nil else { return }
        pendingInstallation = .newDevice
    }

    private func performNewDeviceInstallation() {
        let homeSSID = model.homeWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !homeSSID.isEmpty else {
            setupError = "Home Wi-Fi name is required."
            return
        }

        let preset = RadarLocationPreset.preset(
            label: model.portableLocationLabel,
            metar: model.portableMetarStation
        )
        let configuration = DeviceProvisioningConfiguration(
            activeProfile: model.activeDeviceWiFiProfile.rawValue,
            homeSSID: homeSSID,
            homePassword: model.homeWiFiPassword,
            hotspotSSID: model.hotspotWiFiSSID.trimmingCharacters(in: .whitespacesAndNewlines),
            hotspotPassword: model.hotspotWiFiPassword,
            locationKey: preset?.key ?? "SJC",
            locationLabel: model.portableLocationLabel,
            latitude: model.portableLatitude,
            longitude: model.portableLongitude,
            timezone: model.portableTimezone,
            metar: model.portableMetarStation,
            radarHalfDegrees: model.portableRadarHalfDegrees,
            aircraftLabels: model.aircraftLabelsEnabled,
            radarWedge: model.radarWedgeEnabled,
            accent: model.accentColor.rawValue,
            brightnessPercent: 100,
            radarRadiusMiles: model.radarRadiusMiles,
            radarCityKeys: model.radarCityKeys,
            clockCityKeys: model.clockCityKeys,
            openSkyEnabled: true,
            openSkyClientID: model.openSkyClientID,
            openSkyClientSecret: model.openSkyClientSecret,
            airLabsAPIKey: model.airLabsAPIKey,
            flightAwareAvailable: !model.flightAwareAPIKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        )

        step = .working(title: "Setting up new device")
        installer.installNewDevice(configuration: configuration) { result in
            switch result {
            case .success:
                step = .success(
                    title: "New device ready",
                    detail: "The tracker has been flashed and configured. It is joining \(homeSSID). Its settings page will show the assigned IP address; enter that address in the companion if your router gives the new tracker a different one."
                )
            case let .failure(error):
                step = .failure(
                    title: "Setup failed",
                    detail: error.localizedDescription
                )
            }
        }
    }
}

private struct DeviceCableStatus: View {
    let serialPort: String?
    let networkReachable: Bool
    var large = false

    @State private var cableVisible = false

    var body: some View {
        VStack(spacing: large ? 4 : -4) {
            SetupDeviceArtwork(size: large ? 218 : 154)

            ZStack {
                if serialPort == nil {
                    PremiumUSBCableArtwork(connectorWidth: (large ? 218 : 154) * 0.40)
                        .opacity(cableVisible ? 1.0 : 0.68)
                        .scaleEffect(cableVisible ? 1.0 : 0.975)
                } else {
                    ZStack {
                        Circle()
                            .fill(DeviceSetupTheme.green.opacity(0.14))
                        Image(systemName: "checkmark")
                            .font(.system(size: 17, weight: .bold))
                            .foregroundStyle(DeviceSetupTheme.green)
                    }
                    .frame(width: 45, height: 45)
                    .transition(.opacity.combined(with: .scale))
                }
            }
            .frame(width: large ? 110 : 78, height: large ? 218 : 154)

            VStack(spacing: 3) {
                Text(serialPort == nil ? "Waiting for USB-C" : "Device connected")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(.white)
                if let serialPort {
                    Text(URL(fileURLWithPath: serialPort).lastPathComponent)
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundStyle(.secondary)
                } else if networkReachable {
                    Text("Network connection found · USB-C is still required")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .task {
            guard serialPort == nil else { return }
            cableVisible = false
            withAnimation(.easeInOut(duration: 1.05).repeatForever(autoreverses: true)) {
                cableVisible = true
            }
        }
        .onChange(of: serialPort) { _, value in
            if value == nil {
                cableVisible = false
                withAnimation(.easeInOut(duration: 1.05).repeatForever(autoreverses: true)) {
                    cableVisible = true
                }
            }
        }
    }
}

private struct SetupDeviceArtwork: View {
    let size: CGFloat

    private static let image: NSImage? = {
        for url in DeviceFirmwareResources.deviceArtworkCandidates {
            if let image = NSImage(contentsOf: url) {
                return image
            }
        }
        return nil
    }()

    var body: some View {
        Group {
            if let image = Self.image {
                Image(nsImage: image)
                    .resizable()
                    .scaledToFit()
            } else {
                ZStack {
                    Circle()
                        .fill(.black)
                    Circle()
                        .stroke(
                            LinearGradient(
                                colors: [.white.opacity(0.75), .white.opacity(0.2)],
                                startPoint: .topLeading,
                                endPoint: .bottomTrailing
                            ),
                            lineWidth: 10
                        )
                    DeviceOutlineGlyph(color: .white.opacity(0.65))
                        .frame(width: size * 0.30, height: size * 0.30)
                }
            }
        }
        .frame(width: size, height: size)
        .shadow(color: .black.opacity(0.45), radius: 18, y: 10)
    }
}

private struct PremiumUSBCableArtwork: View {
    let connectorWidth: CGFloat

    private static let image: NSImage? = AppResources.bundle.url(
        forResource: "usb_c_cable_premium",
        withExtension: "png"
    ).flatMap(NSImage.init(contentsOf:))

    var body: some View {
        Group {
            if let image = Self.image {
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.high)
                    .scaledToFit()
            } else {
                Image(systemName: "cable.connector")
                    .font(.system(size: 30, weight: .light))
                    .foregroundStyle(.white)
            }
        }
        .frame(width: connectorWidth, height: connectorWidth * 2.5)
        .accessibilityLabel("USB-C cable")
    }
}

private enum DeviceSetupTheme {
    static let green = Color(red: 0.12, green: 0.72, blue: 0.24)
    static let panel = LinearGradient(
        colors: [.white.opacity(0.065), .white.opacity(0.025)],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
}

struct DeviceProvisioningConfiguration: Sendable {
    let activeProfile: String
    let homeSSID: String
    let homePassword: String
    let hotspotSSID: String
    let hotspotPassword: String
    let locationKey: String
    let locationLabel: String
    let latitude: String
    let longitude: String
    let timezone: String
    let metar: String
    let radarHalfDegrees: String
    let aircraftLabels: Bool
    let radarWedge: Bool
    let accent: String
    let brightnessPercent: Int
    let radarRadiusMiles: Int
    let radarCityKeys: [String]
    let clockCityKeys: [String]
    let openSkyEnabled: Bool
    let openSkyClientID: String
    let openSkyClientSecret: String
    let airLabsAPIKey: String
    let flightAwareAvailable: Bool

    var nvsCSV: String {
        let accentValue: Int = switch accent {
        case "cyan": 1
        case "orange": 2
        case "red": 3
        default: 0
        }
        let rows: [(String, String, String, String)] = [
            ("key", "type", "encoding", "value"),
            ("aircfg", "namespace", "", ""),
            ("active", "data", "string", activeProfile == "hotspot" ? "hotspot" : "home"),
            ("home_ssid", "data", "string", homeSSID),
            ("home_pass", "data", "string", homePassword),
            ("hot_ssid", "data", "string", hotspotSSID),
            ("hot_pass", "data", "string", hotspotPassword),
            ("loc_key", "data", "string", locationKey),
            ("loc_label", "data", "string", locationLabel),
            ("loc_lat", "data", "string", latitude),
            ("loc_lon", "data", "string", longitude),
            ("tz", "data", "string", timezone),
            ("metar", "data", "string", metar),
            ("rad_half", "data", "string", radarHalfDegrees),
            ("radar_city", "data", "string", radarCityKeys.joined(separator: ",")),
            ("clock_city", "data", "string", clockCityKeys.joined(separator: ",")),
            ("ac_labels", "data", "u8", aircraftLabels ? "1" : "0"),
            ("wedge", "data", "u8", radarWedge ? "1" : "0"),
            ("accent", "data", "u8", "\(accentValue)"),
            ("bright", "data", "u8", "\(min(100, max(10, brightnessPercent)))"),
            ("rad_range", "data", "u16", "\([10, 20, 50].contains(radarRadiusMiles) ? radarRadiusMiles : 20)"),
            ("osky_en", "data", "u8", openSkyEnabled ? "1" : "0"),
            ("osky_id", "data", "string", openSkyClientID),
            ("osky_sec", "data", "string", openSkyClientSecret),
            ("airlabs", "data", "string", airLabsAPIKey),
            ("fa_avail", "data", "u8", flightAwareAvailable ? "1" : "0"),
            ("revision", "data", "u32", "1"),
        ]
        return rows.map { row in
            [row.0, row.1, row.2, row.3]
                .map(Self.csvField)
                .joined(separator: ",")
        }
        .joined(separator: "\n") + "\n"
    }

    private static func csvField(_ value: String) -> String {
        guard value.contains(",") || value.contains("\"") ||
                value.contains("\n") || value.contains("\r")
        else { return value }
        return "\"\(value.replacingOccurrences(of: "\"", with: "\"\""))\""
    }
}

@MainActor
final class DeviceFirmwareInstaller: ObservableObject {
    @Published private(set) var serialPort: String?
    @Published private(set) var serialCandidates: [DeviceSerialPortCandidate] = []
    @Published private(set) var progress = 0.0
    @Published private(set) var statusText = "Preparing…"
    @Published private(set) var isBusy = false

    private var monitorTask: Task<Void, Never>?
    private var activeProcess: Process?
    private var processOutput = ""
    private var temporaryDirectory: URL?

    var firmwareAvailable: Bool {
        DeviceFirmwareResources.includedFirmware != nil
    }

    var includedFirmwareSummary: String {
        guard let package = DeviceFirmwareResources.includedFirmware else {
            return "Included firmware is unavailable"
        }
        let attributes = try? FileManager.default.attributesOfItem(
            atPath: package.application.path
        )
        let date = attributes?[.modificationDate] as? Date
        guard let date else { return "ESP32-S3 application image" }
        return "Built \(date.formatted(date: .abbreviated, time: .shortened))"
    }

    var selectedCandidate: DeviceSerialPortCandidate? {
        guard let serialPort else { return nil }
        return serialCandidates.first { $0.path == serialPort }
    }

    func selectSerialPort(_ path: String) {
        guard serialCandidates.contains(where: { $0.path == path }) else { return }
        serialPort = path
    }

    func startMonitoring() {
        refreshSerialPort()
        guard monitorTask == nil else { return }
        monitorTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(850))
                guard !Task.isCancelled else { break }
                self?.refreshSerialPort()
            }
        }
    }

    func stopMonitoring() {
        monitorTask?.cancel()
        monitorTask = nil
    }

    func installApplicationFirmware(
        customImage: URL?,
        completion: @escaping @MainActor @Sendable (Result<Void, Error>) -> Void
    ) {
        guard let port = serialPort else {
            completion(.failure(DeviceFirmwareError.deviceNotConnected))
            return
        }
        guard let included = DeviceFirmwareResources.includedFirmware else {
            completion(.failure(DeviceFirmwareError.firmwareMissing))
            return
        }
        let application = customImage ?? included.application
        beginOperation(status: "Preparing firmware…")
        runFlash(
            port: port,
            segments: [(0x10000, application)],
            completion: completion
        )
    }

    func installNewDevice(
        configuration: DeviceProvisioningConfiguration,
        completion: @escaping @MainActor @Sendable (Result<Void, Error>) -> Void
    ) {
        guard let port = serialPort else {
            completion(.failure(DeviceFirmwareError.deviceNotConnected))
            return
        }
        guard let included = DeviceFirmwareResources.includedFirmware else {
            completion(.failure(DeviceFirmwareError.firmwareMissing))
            return
        }
        guard let generator = DeviceFirmwareResources.nvsGenerator else {
            completion(.failure(DeviceFirmwareError.nvsGeneratorMissing))
            return
        }
        guard let python = DeviceFirmwareResources.python else {
            completion(.failure(DeviceFirmwareError.pythonMissing))
            return
        }

        beginOperation(status: "Creating secure device configuration…")
        do {
            let directory = FileManager.default.temporaryDirectory
                .appendingPathComponent(
                    "AirspaceDeviceSetup-\(UUID().uuidString)",
                    isDirectory: true
                )
            try FileManager.default.createDirectory(
                at: directory,
                withIntermediateDirectories: true,
                attributes: [.posixPermissions: 0o700]
            )
            temporaryDirectory = directory
            let csv = directory.appendingPathComponent("device-config.csv")
            let nvs = directory.appendingPathComponent("device-config.bin")
            try configuration.nvsCSV.write(
                to: csv,
                atomically: true,
                encoding: .utf8
            )
            try FileManager.default.setAttributes(
                [.posixPermissions: 0o600],
                ofItemAtPath: csv.path
            )

            runProcess(
                executable: python,
                arguments: [
                    generator.path,
                    "generate",
                    csv.path,
                    nvs.path,
                    "0x6000",
                ],
                parseFlashProgress: false
            ) { [weak self] result in
                guard let self else { return }
                switch result {
                case .success:
                    self.progress = 0.08
                    self.statusText = "Installing firmware and settings…"
                    self.runFlash(
                        port: port,
                        segments: [
                            (0x0, included.bootloader),
                            (0x8000, included.partitionTable),
                            (0x9000, nvs),
                            (0x10000, included.application),
                        ],
                        completion: completion
                    )
                case let .failure(error):
                    self.finishOperation()
                    completion(.failure(error))
                }
            }
        } catch {
            finishOperation()
            completion(.failure(error))
        }
    }

    private func beginOperation(status: String) {
        progress = 0.02
        statusText = status
        processOutput = ""
        isBusy = true
    }

    private func runFlash(
        port: String,
        segments: [(Int, URL)],
        completion: @escaping @MainActor @Sendable (Result<Void, Error>) -> Void
    ) {
        guard let python = DeviceFirmwareResources.python else {
            finishOperation()
            completion(.failure(DeviceFirmwareError.pythonMissing))
            return
        }
        var arguments = [
            "-m", "esptool",
            "--chip", "esp32s3",
            "--port", port,
            "--baud", "460800",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", "32MB",
        ]
        for (offset, url) in segments.sorted(by: { $0.0 < $1.0 }) {
            arguments.append(String(format: "0x%x", offset))
            arguments.append(url.path)
        }
        statusText = "Writing firmware to the tracker…"
        runProcess(
            executable: python,
            arguments: arguments,
            parseFlashProgress: true
        ) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success:
                self.progress = 1.0
                self.statusText = "Restarting the tracker…"
                Task { @MainActor in
                    try? await Task.sleep(for: .milliseconds(650))
                    self.finishOperation()
                    completion(.success(()))
                }
            case let .failure(error):
                self.finishOperation()
                completion(.failure(error))
            }
        }
    }

    private func runProcess(
        executable: URL,
        arguments: [String],
        parseFlashProgress: Bool,
        completion: @escaping @MainActor @Sendable (Result<Void, Error>) -> Void
    ) {
        let process = Process()
        let pipe = Pipe()
        process.executableURL = executable
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = pipe
        process.environment = ProcessInfo.processInfo.environment
        activeProcess = process

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty,
                  let chunk = String(data: data, encoding: .utf8)
            else { return }
            Task { @MainActor [weak self] in
                self?.consumeOutput(
                    chunk,
                    parseFlashProgress: parseFlashProgress
                )
            }
        }

        process.terminationHandler = { [weak self] finished in
            pipe.fileHandleForReading.readabilityHandler = nil
            let trailing = pipe.fileHandleForReading.readDataToEndOfFile()
            let trailingText = String(data: trailing, encoding: .utf8) ?? ""
            Task { @MainActor [weak self] in
                guard let self else { return }
                if !trailingText.isEmpty {
                    self.consumeOutput(
                        trailingText,
                        parseFlashProgress: parseFlashProgress
                    )
                }
                self.activeProcess = nil
                if finished.terminationStatus == 0 {
                    completion(.success(()))
                } else {
                    let detail = self.processOutput
                        .split(separator: "\n")
                        .suffix(5)
                        .joined(separator: "\n")
                    completion(
                        .failure(
                            DeviceFirmwareError.toolFailed(
                                detail.isEmpty
                                    ? "The ESP32 flasher exited with code \(finished.terminationStatus)."
                                    : detail
                            )
                        )
                    )
                }
            }
        }

        do {
            try process.run()
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            activeProcess = nil
            completion(.failure(error))
        }
    }

    private func consumeOutput(
        _ chunk: String,
        parseFlashProgress: Bool
    ) {
        processOutput.append(chunk)
        if processOutput.count > 16_000 {
            processOutput = String(processOutput.suffix(16_000))
        }
        guard parseFlashProgress else { return }

        let pattern = #"\((\d+)\s*%\)"#
        guard let expression = try? NSRegularExpression(pattern: pattern),
              let match = expression.matches(
                in: chunk,
                range: NSRange(chunk.startIndex..., in: chunk)
              ).last,
              let range = Range(match.range(at: 1), in: chunk),
              let percent = Double(chunk[range])
        else {
            if chunk.localizedCaseInsensitiveContains("connecting") {
                progress = max(progress, 0.06)
                statusText = "Connecting to the ESP32-S3…"
            } else if chunk.localizedCaseInsensitiveContains("writing") {
                progress = max(progress, 0.10)
                statusText = "Writing firmware to flash…"
            } else if chunk.localizedCaseInsensitiveContains("verifying") ||
                        chunk.localizedCaseInsensitiveContains("hash of data verified") {
                progress = max(progress, 0.96)
                statusText = "Verifying firmware…"
            }
            return
        }
        progress = max(progress, 0.08 + min(100, percent) / 100 * 0.86)
        statusText = "Writing firmware · \(Int(percent))%"
    }

    private func finishOperation() {
        isBusy = false
        activeProcess = nil
        if let temporaryDirectory {
            try? FileManager.default.removeItem(at: temporaryDirectory)
            self.temporaryDirectory = nil
        }
    }

    private func refreshSerialPort() {
        guard !isBusy else { return }
        let discovered = DeviceSerialDiscovery.serialCandidates()
        serialCandidates = discovered
        if let serialPort,
           discovered.contains(where: { $0.path == serialPort }) {
            return
        }
        serialPort = DeviceSerialDiscovery.preferredCandidate(
            from: discovered
        )?.path
    }
}

struct DeviceSerialPortCandidate: Equatable {
    let path: String
    let vendorID: Int?
    let productID: Int?
    let vendorName: String?
    let productName: String?

    var preferenceScore: Int {
        let filename = URL(fileURLWithPath: path).lastPathComponent.lowercased()
        var score = 0

        // ESP32-S3 native USB Serial/JTAG. This is the identity exposed by the
        // Waveshare board, including before AirESP32ace is installed.
        if vendorID == 0x303A { score += 1_000 }
        if productID == 0x1001 { score += 400 }
        if vendorName?.localizedCaseInsensitiveContains("Espressif") == true {
            score += 300
        }
        if productName?.localizedCaseInsensitiveContains("JTAG/serial") == true {
            score += 250
        }

        // Retain support for USB-UART bridges used by development boards, but
        // rank them below the native Espressif interface.
        if filename.hasPrefix("cu.usbmodem") { score += 100 }
        if filename.hasPrefix("cu.usbserial") ||
            filename.hasPrefix("cu.slab_usbtouart") ||
            filename.hasPrefix("cu.wchusbserial") {
            score += 50
        }
        return score
    }

    var isNativeEspressif: Bool {
        vendorID == 0x303A && productID == 0x1001
    }

    var pickerLabel: String {
        let port = URL(fileURLWithPath: path).lastPathComponent
        if isNativeEspressif {
            return "AirESP32ace / ESP32-S3 · \(port)"
        }
        if let productName, !productName.isEmpty {
            return "\(productName) · \(port)"
        }
        return port
    }

    var confirmationDescription: String {
        let port = URL(fileURLWithPath: path).lastPathComponent
        let identity: String
        if isNativeEspressif {
            identity = "Espressif ESP32-S3 USB Serial/JTAG"
        } else {
            identity = [vendorName, productName]
                .compactMap { value in
                    guard let value, !value.isEmpty else { return nil }
                    return value
                }
                .joined(separator: " · ")
        }
        let usbIDs: String
        if let vendorID, let productID {
            usbIDs = String(format: "VID %04X · PID %04X", vendorID, productID)
        } else {
            usbIDs = "USB identity unavailable"
        }
        return [identity.isEmpty ? "USB serial device" : identity, port, usbIDs]
            .joined(separator: "\n")
    }
}

enum DeviceSerialDiscovery {
    static func preferredPort() -> String? {
        preferredCandidate(from: serialCandidates())?.path
    }

    static func preferredCandidate(
        from candidates: [DeviceSerialPortCandidate]
    ) -> DeviceSerialPortCandidate? {
        candidates.max { lhs, rhs in
            if lhs.preferenceScore != rhs.preferenceScore {
                return lhs.preferenceScore < rhs.preferenceScore
            }
            // Stable selection when two equivalent ports are attached.
            return lhs.path.localizedStandardCompare(rhs.path) == .orderedDescending
        }
    }

    static func serialCandidates() -> [DeviceSerialPortCandidate] {
        filesystemCandidates()
    }

    private static func filesystemCandidates()
        -> [DeviceSerialPortCandidate] {
        let directory = URL(fileURLWithPath: "/dev", isDirectory: true)
        let names = (try? FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]
        )) ?? []
        return names.compactMap { url in
            guard isSupportedSerialPath(url.path) else { return nil }
            return DeviceSerialPortCandidate(
                path: url.path,
                vendorID: nil,
                productID: nil,
                vendorName: nil,
                productName: nil
            )
        }
    }

    private static func isSupportedSerialPath(_ path: String) -> Bool {
        let name = URL(fileURLWithPath: path).lastPathComponent.lowercased()
        return name.hasPrefix("cu.usbmodem") ||
            name.hasPrefix("cu.usbserial") ||
            name.hasPrefix("cu.slab_usbtouart") ||
            name.hasPrefix("cu.wchusbserial")
    }
}

enum DeviceFirmwareError: LocalizedError {
    case deviceNotConnected
    case firmwareMissing
    case nvsGeneratorMissing
    case pythonMissing
    case toolFailed(String)

    var errorDescription: String? {
        switch self {
        case .deviceNotConnected:
            "Connect the tracker with a USB-C data cable and try again."
        case .firmwareMissing:
            "The included firmware package is missing. Rebuild the companion app bundle or choose an application .bin file."
        case .nvsGeneratorMissing:
            "The device-configuration tool is missing from this app build."
        case .pythonMissing:
            "The ESP32 flashing runtime was not found. Install the ESP-IDF tools on this Mac, then reopen Aerospace Companion."
        case let .toolFailed(detail):
            "The tracker could not be flashed. \(detail)"
        }
    }
}

enum DeviceFirmwareResources {
    struct FirmwarePackage {
        let bootloader: URL
        let partitionTable: URL
        let application: URL
    }

    static var includedFirmware: FirmwarePackage? {
        for directory in firmwareDirectories {
            let bootloader = directory.appendingPathComponent("bootloader.bin")
            let partition = directory.appendingPathComponent("partition-table.bin")
            let application = directory.appendingPathComponent("airspace_instrument.bin")
            if FileManager.default.fileExists(atPath: bootloader.path),
               FileManager.default.fileExists(atPath: partition.path),
               FileManager.default.fileExists(atPath: application.path) {
                return FirmwarePackage(
                    bootloader: bootloader,
                    partitionTable: partition,
                    application: application
                )
            }
        }
        return nil
    }

    static var nvsGenerator: URL? {
        for candidate in nvsGeneratorCandidates
        where FileManager.default.fileExists(atPath: candidate.path) {
            return candidate
        }
        return nil
    }

    static var python: URL? {
        let environment = ProcessInfo.processInfo.environment
        var candidates: [URL] = []
        if let configured = environment["AIRSPACE_PYTHON"], !configured.isEmpty {
            candidates.append(URL(fileURLWithPath: configured))
        }

        let home = FileManager.default.homeDirectoryForCurrentUser
        let environments = home.appendingPathComponent(
            ".espressif/python_env",
            isDirectory: true
        )
        if let entries = try? FileManager.default.contentsOfDirectory(
            at: environments,
            includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]
        ) {
            candidates.append(contentsOf: entries
                .sorted { $0.lastPathComponent > $1.lastPathComponent }
                .map { $0.appendingPathComponent("bin/python") })
        }
        candidates.append(contentsOf: [
            URL(fileURLWithPath: "/opt/homebrew/bin/python3"),
            URL(fileURLWithPath: "/usr/local/bin/python3"),
            URL(fileURLWithPath: "/usr/bin/python3"),
        ])
        return candidates.first {
            FileManager.default.isExecutableFile(atPath: $0.path)
        }
    }

    static var deviceArtworkCandidates: [URL] {
        var result: [URL] = []
        if let resources = Bundle.main.resourceURL {
            result.append(
                resources
                    .appendingPathComponent("Device", isDirectory: true)
                    .appendingPathComponent("device-clean.png")
            )
        }
        if let resources = AppResources.bundle.resourceURL {
            result.append(resources.appendingPathComponent("device-clean.png"))
        }
        if let root = repositoryRoot {
            result.append(
                root.appendingPathComponent(
                    "output/guide/assets/device-states/device-clean.png"
                )
            )
        }
        return result
    }

    private static var firmwareDirectories: [URL] {
        var result: [URL] = []
        if let resources = Bundle.main.resourceURL {
            result.append(resources.appendingPathComponent("Firmware", isDirectory: true))
        }
        if let root = repositoryRoot {
            result.append(
                root.appendingPathComponent(
                    "firmware/airspace-instrument/build",
                    isDirectory: true
                )
            )
        }
        return result
    }

    private static var nvsGeneratorCandidates: [URL] {
        var result: [URL] = []
        if let resources = Bundle.main.resourceURL {
            result.append(
                resources
                    .appendingPathComponent("FirmwareTools", isDirectory: true)
                    .appendingPathComponent("nvs_partition_gen.py")
            )
        }
        if let root = repositoryRoot {
            result.append(
                root.appendingPathComponent(
                    "tools/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
                )
            )
        }
        return result
    }

    private static var repositoryRoot: URL? {
        #if DEBUG
            var cursor = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            for _ in 0..<8 {
                let marker = cursor.appendingPathComponent("firmware", isDirectory: true)
                if FileManager.default.fileExists(atPath: marker.path) {
                    return cursor
                }
                cursor.deleteLastPathComponent()
            }
        #endif
        let working = URL(
            fileURLWithPath: FileManager.default.currentDirectoryPath,
            isDirectory: true
        )
        var workingCursor = working
        for _ in 0..<6 {
            let marker = workingCursor.appendingPathComponent("firmware", isDirectory: true)
            if FileManager.default.fileExists(atPath: marker.path) {
                return workingCursor
            }
            workingCursor.deleteLastPathComponent()
        }
        return nil
    }
}
