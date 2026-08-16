#include "minitest.hpp"

#include "core/pam/PamServiceConfig.hpp"

using namespace facial_auth;

TEST(AllowListAcceptsKnownServicesOnly) {
    ASSERT_TRUE(isAllowedPamService("sudo"));
    ASSERT_TRUE(isAllowedPamService("gdm-password"));
    ASSERT_TRUE(isAllowedPamService("sddm"));
    ASSERT_TRUE(isAllowedPamService("lightdm"));
    ASSERT_TRUE(!isAllowedPamService("sshd"));
    ASSERT_TRUE(!isAllowedPamService("login"));
    ASSERT_TRUE(!isAllowedPamService(""));
}

TEST(DetectsAbsentByDefault) {
    const std::string content = "auth    required    pam_unix.so\n";
    ASSERT_TRUE(detectPamFacialState(content) == PamFacialState::Absent);
}

TEST(DetectsSafeSufficientLine) {
    const std::string content = "auth    sufficient   pam_facial.so\nauth    required pam_unix.so\n";
    ASSERT_TRUE(detectPamFacialState(content) == PamFacialState::EnabledSafe);
}

TEST(DetectsUnsafeControlFlags) {
    ASSERT_TRUE(detectPamFacialState("auth required pam_facial.so\n") == PamFacialState::EnabledUnsafe);
    ASSERT_TRUE(detectPamFacialState("auth requisite pam_facial.so\n") == PamFacialState::EnabledUnsafe);
    ASSERT_TRUE(detectPamFacialState("auth [success=ok default=die] pam_facial.so\n") ==
                PamFacialState::EnabledUnsafe);
}

TEST(IgnoresCommentedOutLines) {
    const std::string content = "# auth sufficient pam_facial.so\nauth required pam_unix.so\n";
    ASSERT_TRUE(detectPamFacialState(content) == PamFacialState::Absent);
}

TEST(EnableInsertsLineFirst) {
    const std::string before = "# example header\nauth required pam_unix.so\n";
    const std::string after = enableInContent(before);
    ASSERT_EQ(after.rfind("auth    sufficient   pam_facial.so\n", 0), 0u);
    ASSERT_TRUE(after.find("# example header") != std::string::npos);
    ASSERT_TRUE(after.find("auth required pam_unix.so") != std::string::npos);
}

TEST(EnableIsIdempotentWhenAlreadySafe) {
    const std::string before = "auth    sufficient   pam_facial.so\nauth required pam_unix.so\n";
    ASSERT_EQ(enableInContent(before), before);
}

TEST(EnableThrowsOnUnsafeExistingLine) {
    const std::string before = "auth required pam_facial.so\nauth required pam_unix.so\n";
    ASSERT_THROWS(enableInContent(before));
}

TEST(DisableRemovesLineRegardlessOfFlag) {
    const std::string before =
        "auth    sufficient   pam_facial.so\nauth required pam_unix.so\n";
    const std::string after = disableInContent(before);
    ASSERT_TRUE(after.find("pam_facial.so") == std::string::npos);
    ASSERT_TRUE(after.find("pam_unix.so") != std::string::npos);
}

TEST(DisableRemovesUnsafeLineTooAsRecoveryPath) {
    const std::string before = "auth required pam_facial.so\nauth required pam_unix.so\n";
    const std::string after = disableInContent(before);
    ASSERT_TRUE(after.find("pam_facial.so") == std::string::npos);
    ASSERT_TRUE(detectPamFacialState(after) == PamFacialState::Absent);
}

TEST(DisableIsIdempotentWhenAbsent) {
    const std::string before = "auth required pam_unix.so\n";
    ASSERT_EQ(disableInContent(before), before);
}

MINITEST_MAIN()
