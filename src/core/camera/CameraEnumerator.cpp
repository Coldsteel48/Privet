#include "CameraEnumerator.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>

namespace facial_auth {

namespace {

// "video12" -> 12, or -1 if the name isn't "video" followed by digits.
int videoNodeIndex(const std::string& name) {
    constexpr char kPrefix[] = "video";
    constexpr std::size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (name.size() <= kPrefixLen || name.compare(0, kPrefixLen, kPrefix) != 0) {
        return -1;
    }
    const std::string digits = name.substr(kPrefixLen);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
        return -1;
    }
    return std::stoi(digits);
}

std::string fixedCharsToString(const __u8* chars, std::size_t maxLen) {
    return std::string(reinterpret_cast<const char*>(chars),
                        strnlen(reinterpret_cast<const char*>(chars), maxLen));
}

// Maps each /dev/videoN's canonical path to a /dev/v4l/by-id/... symlink
// that resolves to it, when one exists. config/facial-auth.conf.example
// specifically recommends by-id paths over raw /dev/videoN — the numeric
// suffix is assignment-order-dependent and not guaranteed stable across
// reboots/replugs on a multi-node webcam — so this is what should end up
// in CameraStreamOption::devicePath whenever udev has created one, both
// to match an already-configured device_path and so a fresh Save writes
// the stable form.
std::map<std::string, std::string> byIdPathsByTarget() {
    std::map<std::string, std::string> result;
    DIR* dir = opendir("/dev/v4l/by-id");
    if (dir == nullptr) return result;  // no udev by-id dir (unusual, but not fatal)

    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string byIdPath = std::string("/dev/v4l/by-id/") + entry->d_name;
        char resolved[PATH_MAX];
        if (realpath(byIdPath.c_str(), resolved) != nullptr) {
            result[resolved] = byIdPath;
        }
    }
    closedir(dir);
    return result;
}

bool nodeSupportsFourcc(int fd, std::uint32_t fourcc) {
    v4l2_fmtdesc desc{};
    desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (desc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &desc) == 0; ++desc.index) {
        if (desc.pixelformat == fourcc) return true;
    }
    return false;
}

// Picks a frame size to request for the given fourcc on this node: 640x480
// (facial_auth::Config's historical default geometry) if the driver
// offers it, else the first discrete size VIDIOC_ENUM_FRAMESIZES reports,
// else (for a stepwise/continuous-only node, uncommon for UVC webcams)
// its max. Returns false only if the fourcc isn't actually enumerable at
// all, which shouldn't happen given nodeSupportsFourcc() already passed.
bool pickFrameSize(int fd, std::uint32_t fourcc, int& width, int& height) {
    v4l2_frmsizeenum frm{};
    frm.pixel_format = fourcc;
    bool found = false;
    for (frm.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frm) == 0; ++frm.index) {
        if (frm.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
            width = static_cast<int>(frm.stepwise.max_width);
            height = static_cast<int>(frm.stepwise.max_height);
            return true;
        }
        if (!found) {
            width = static_cast<int>(frm.discrete.width);
            height = static_cast<int>(frm.discrete.height);
            found = true;
        }
        if (frm.discrete.width == 640 && frm.discrete.height == 480) {
            width = 640;
            height = 480;
            return true;
        }
    }
    return found;
}

struct DiscoveredNode {
    int index;
    std::string busInfo;
    std::string cardName;
    std::optional<CameraStreamOption> ir;
    std::optional<CameraStreamOption> rgb;
};

}  // namespace

std::vector<CameraDevice> listCameras() {
    std::vector<DiscoveredNode> nodes;
    const std::map<std::string, std::string> byIdPaths = byIdPathsByTarget();

    DIR* dir = opendir("/dev");
    if (dir == nullptr) {
        return {};
    }

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        const int index = videoNodeIndex(name);
        if (index < 0) continue;

        const std::string rawPath = "/dev/" + name;
        const int fd = ::open(rawPath.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) continue;  // permission denied, or a race with unplug/replug

        v4l2_capability cap{};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
            ::close(fd);
            continue;
        }
        // On multi-node devices (e.g. the confirmed Logi 4K Stream Edition,
        // which exposes 4 /dev/videoN nodes for one physical camera), a
        // driver that sets V4L2_CAP_DEVICE_CAPS reports cap.capabilities as
        // the aggregate of *all* of the device's nodes — every node reports
        // the same value, including ones that support neither capture nor
        // streaming themselves. The actual per-node capabilities are in
        // cap.device_caps in that case; only fall back to cap.capabilities
        // for older drivers that don't set V4L2_CAP_DEVICE_CAPS at all.
        const __u32 nodeCaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                                          : cap.capabilities;
        const bool usable =
            (nodeCaps & V4L2_CAP_VIDEO_CAPTURE) && (nodeCaps & V4L2_CAP_STREAMING);
        if (!usable) {
            ::close(fd);
            continue;  // metadata/other non-capture node on a multi-node webcam
        }

        const auto byIdIt = byIdPaths.find(rawPath);
        const std::string devicePath = byIdIt != byIdPaths.end() ? byIdIt->second : rawPath;

        DiscoveredNode node;
        node.index = index;
        node.busInfo = fixedCharsToString(cap.bus_info, sizeof(cap.bus_info));
        node.cardName = fixedCharsToString(cap.card, sizeof(cap.card));

        int width = 0, height = 0;
        if (nodeSupportsFourcc(fd, V4L2_PIX_FMT_GREY) &&
            pickFrameSize(fd, V4L2_PIX_FMT_GREY, width, height)) {
            node.ir = CameraStreamOption{devicePath, PixelFormat::GREY, width, height};
        }
        if (nodeSupportsFourcc(fd, V4L2_PIX_FMT_YUYV) &&
            pickFrameSize(fd, V4L2_PIX_FMT_YUYV, width, height)) {
            node.rgb = CameraStreamOption{devicePath, PixelFormat::YUYV, width, height};
        }
        ::close(fd);

        if (!node.ir && !node.rgb) continue;  // no format this app can actually capture
        nodes.push_back(std::move(node));
    }
    closedir(dir);

    std::sort(nodes.begin(), nodes.end(),
              [](const auto& a, const auto& b) { return a.index < b.index; });

    // Group nodes into physical cameras by bus_info (stable per physical
    // USB device/port — distinguishes two identical camera models plugged
    // in separately, unlike the card name alone). Within a device, the
    // first GREY-capable node found becomes its ir option and the first
    // YUYV-capable node becomes its rgb option; nodes are visited in
    // ascending /dev/videoN order so this is deterministic.
    std::vector<CameraDevice> result;
    std::map<std::string, std::size_t> deviceIndexByBusInfo;
    for (const auto& node : nodes) {
        auto it = deviceIndexByBusInfo.find(node.busInfo);
        std::size_t deviceIndex;
        if (it == deviceIndexByBusInfo.end()) {
            deviceIndex = result.size();
            deviceIndexByBusInfo.emplace(node.busInfo, deviceIndex);
            result.push_back(CameraDevice{node.cardName, std::nullopt, std::nullopt});
        } else {
            deviceIndex = it->second;
        }
        CameraDevice& device = result[deviceIndex];
        if (node.ir && !device.ir) device.ir = node.ir;
        if (node.rgb && !device.rgb) device.rgb = node.rgb;
    }

    // Disambiguate identical friendly names (two physical cameras of the
    // same model) by appending a running count.
    std::map<std::string, int> nameCounts;
    for (const auto& device : result) nameCounts[device.friendlyName]++;
    std::map<std::string, int> nameSeen;
    for (auto& device : result) {
        if (nameCounts[device.friendlyName] > 1) {
            device.friendlyName += " (" + std::to_string(++nameSeen[device.friendlyName]) + ")";
        }
    }

    return result;
}

}  // namespace facial_auth
