#include "V4L2Camera.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace facial_auth {

namespace {

constexpr unsigned int kRequestedBufferCount = 4;

int xioctl(int fd, int request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

}  // namespace

V4L2Camera::V4L2Camera(CameraConfig config) : config_(std::move(config)) {}

V4L2Camera::~V4L2Camera() {
    close();
}

bool V4L2Camera::open() {
    if (fd_ >= 0) {
        return true;  // already open
    }

    fd_ = ::open(config_.devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        return false;
    }

    v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0 ||
        !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING)) {
        close();
        return false;
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<__u32>(config_.width);
    fmt.fmt.pix.height = static_cast<__u32>(config_.height);
    fmt.fmt.pix.pixelformat = toV4L2Fourcc(config_.pixelFormat);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        close();
        return false;
    }
    // The driver may adjust width/height/format to the nearest supported
    // mode — reflect that back so captureFrame()'s Mat construction uses
    // the actual negotiated geometry, not the requested one.
    config_.width = static_cast<int>(fmt.fmt.pix.width);
    config_.height = static_cast<int>(fmt.fmt.pix.height);

    v4l2_requestbuffers req{};
    req.count = kRequestedBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        close();
        return false;
    }

    buffers_.resize(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            close();
            return false;
        }

        void* start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
                            static_cast<off_t>(buf.m.offset));
        if (start == MAP_FAILED) {
            close();
            return false;
        }
        buffers_[i].start = start;
        buffers_[i].length = buf.length;

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            close();
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        close();
        return false;
    }

    streaming_ = true;
    return true;
}

std::optional<cv::Mat> V4L2Camera::captureFrame() {
    if (fd_ < 0 || !streaming_) {
        return std::nullopt;
    }

    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    const int pollResult = poll(&pfd, 1, config_.timeoutMs);
    if (pollResult <= 0) {
        return std::nullopt;  // timeout, or poll() error — both transient/retryable to the caller
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        return std::nullopt;
    }

    cv::Mat frame;
    switch (config_.pixelFormat) {
        case PixelFormat::YUYV: {
            cv::Mat yuyv(config_.height, config_.width, CV_8UC2, buffers_[buf.index].start);
            cv::cvtColor(yuyv, frame, cv::COLOR_YUV2BGR_YUYV);
            frame = frame.clone();  // must copy out before the buffer is requeued below
            break;
        }
        case PixelFormat::GREY:
        case PixelFormat::Y16:
            xioctl(fd_, VIDIOC_QBUF, &buf);  // hand the buffer back before throwing
            throw std::runtime_error(
                "V4L2Camera: PixelFormat::" + toString(config_.pixelFormat) +
                " capture is not implemented yet — confirm the real format via "
                "`v4l2-ctl --list-formats-ext` first (see project plan / docs)");
        case PixelFormat::MJPEG:
        case PixelFormat::Unknown:
        default:
            xioctl(fd_, VIDIOC_QBUF, &buf);
            throw std::runtime_error("V4L2Camera: unsupported pixel format for capture");
    }

    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        return std::nullopt;
    }

    return frame;
}

void V4L2Camera::close() {
    if (streaming_ && fd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }

    for (auto& buffer : buffers_) {
        if (buffer.start != nullptr) {
            munmap(buffer.start, buffer.length);
            buffer.start = nullptr;
        }
    }
    buffers_.clear();

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace facial_auth
