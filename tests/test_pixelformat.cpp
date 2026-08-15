#include "minitest.hpp"

#include "core/camera/PixelFormat.hpp"

using namespace facial_auth;

TEST(RoundTripsFourccForKnownFormats) {
    for (auto fmt : {PixelFormat::GREY, PixelFormat::Y16, PixelFormat::YUYV, PixelFormat::MJPEG}) {
        ASSERT_TRUE(fromV4L2Fourcc(toV4L2Fourcc(fmt)) == fmt);
    }
}

TEST(UnknownFourccMapsToUnknown) {
    ASSERT_TRUE(fromV4L2Fourcc(0xdeadbeefu) == PixelFormat::Unknown);
}

TEST(StringParsingIsCaseInsensitive) {
    ASSERT_TRUE(pixelFormatFromString("GREY") == PixelFormat::GREY);
    ASSERT_TRUE(pixelFormatFromString("gray") == PixelFormat::GREY);
    ASSERT_TRUE(pixelFormatFromString("Y16") == PixelFormat::Y16);
    ASSERT_TRUE(pixelFormatFromString("yuyv") == PixelFormat::YUYV);
    ASSERT_TRUE(pixelFormatFromString("mjpeg") == PixelFormat::MJPEG);
    ASSERT_TRUE(pixelFormatFromString("nonsense") == PixelFormat::Unknown);
    ASSERT_TRUE(pixelFormatFromString("") == PixelFormat::Unknown);
}

TEST(BytesPerPixelMatchesKnownFormats) {
    ASSERT_EQ(bytesPerPixel(PixelFormat::GREY), 1);
    ASSERT_EQ(bytesPerPixel(PixelFormat::Y16), 2);
    ASSERT_EQ(bytesPerPixel(PixelFormat::YUYV), 2);
}

MINITEST_MAIN()
