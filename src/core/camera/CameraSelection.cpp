#include "CameraSelection.hpp"

namespace facial_auth {

CameraDeviceSelection selectCameraDevice(std::vector<CameraDevice> scanned,
                                          const std::string& devicePath, PixelFormat pixelFormat,
                                          CameraMode cameraMode, int frameWidth, int frameHeight,
                                          const std::string& notDetectedLabel,
                                          const std::string& noCamerasLabel) {
    CameraDeviceSelection result;
    result.devices = std::move(scanned);

    int selectedIndex = -1;
    std::string mode = toString(cameraMode);

    if (!devicePath.empty()) {
        for (int i = 0; i < static_cast<int>(result.devices.size()); ++i) {
            const CameraDevice& device = result.devices[i];
            const bool irMatches = device.ir && device.ir->devicePath == devicePath;
            const bool rgbMatches = device.rgb && device.rgb->devicePath == devicePath;
            if (!irMatches && !rgbMatches) continue;
            selectedIndex = i;
            if (irMatches && rgbMatches) {
                mode = pixelFormat == PixelFormat::YUYV ? "rgb" : "ir";
            } else {
                mode = irMatches ? "ir" : "rgb";
            }
            break;
        }
        if (selectedIndex < 0) {
            CameraDevice fallback;
            fallback.friendlyName = notDetectedLabel;
            const CameraStreamOption option{devicePath, pixelFormat, frameWidth, frameHeight};
            if (mode == "rgb") {
                fallback.rgb = option;
            } else {
                fallback.ir = option;
            }
            result.devices.push_back(std::move(fallback));
            selectedIndex = static_cast<int>(result.devices.size()) - 1;
            result.usedNotDetectedFallback = true;
        }
    }

    if (result.devices.empty()) {
        result.devices.push_back(CameraDevice{noCamerasLabel, std::nullopt, std::nullopt});
        result.usedNoCamerasPlaceholder = true;
    }
    if (selectedIndex < 0) {
        selectedIndex = 0;  // no configured path yet — default to the first detected camera
    }

    result.selectedIndex = selectedIndex;
    result.mode = std::move(mode);
    return result;
}

CameraModeSelection selectCameraMode(const CameraDevice& device, const std::string& desiredMode) {
    CameraModeSelection result;
    result.irAvailable = device.ir.has_value();
    result.rgbAvailable = device.rgb.has_value();

    if (desiredMode == "ir" && result.irAvailable) {
        result.selectedMode = "ir";
    } else if (desiredMode == "rgb" && result.rgbAvailable) {
        result.selectedMode = "rgb";
    } else if (result.irAvailable) {
        result.selectedMode = "ir";
    } else if (result.rgbAvailable) {
        result.selectedMode = "rgb";
    }

    result.showRgbOnlyDisclaimer = result.rgbAvailable && !result.irAvailable;
    return result;
}

std::string resolveModeForDeviceChange(const CameraDevice& device,
                                        const std::string& lastConfirmedMode) {
    std::string desired = lastConfirmedMode;
    if (desired == "ir" && !device.ir) {
        desired = "rgb";
    } else if (desired == "rgb" && !device.rgb) {
        desired = "ir";
    }
    return desired;
}

}  // namespace facial_auth
