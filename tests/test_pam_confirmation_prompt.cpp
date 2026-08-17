#include "minitest.hpp"

#include "core/pam/PamConfirmationPrompt.hpp"

using namespace facial_auth;

TEST(AcceptsYAndYes) {
    ASSERT_TRUE(isAffirmativeResponse("y"));
    ASSERT_TRUE(isAffirmativeResponse("Y"));
    ASSERT_TRUE(isAffirmativeResponse("yes"));
    ASSERT_TRUE(isAffirmativeResponse("YES"));
    ASSERT_TRUE(isAffirmativeResponse("Yes"));
}

TEST(TrimsSurroundingWhitespace) {
    ASSERT_TRUE(isAffirmativeResponse("  y  "));
    ASSERT_TRUE(isAffirmativeResponse("\tyes\n"));
}

TEST(DeclinesEmptyNullAndGarbage) {
    ASSERT_TRUE(!isAffirmativeResponse(""));
    ASSERT_TRUE(!isAffirmativeResponse("   "));
    ASSERT_TRUE(!isAffirmativeResponse(nullptr));
    ASSERT_TRUE(!isAffirmativeResponse("n"));
    ASSERT_TRUE(!isAffirmativeResponse("no"));
    ASSERT_TRUE(!isAffirmativeResponse("sure"));
    ASSERT_TRUE(!isAffirmativeResponse("yess"));
    ASSERT_TRUE(!isAffirmativeResponse("yy"));
}

TEST(TimeoutDefaultsWhenKeyAbsent) {
    ASSERT_EQ(parseConfirmationTimeoutMs(""), kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("camera_mode = ir\nmatch_threshold = 0.36\n"),
              kDefaultConfirmationTimeoutMs);
}

TEST(TimeoutParsesConfiguredSeconds) {
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = 5\n"), 5000);
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec=7\n"), 7000);
    ASSERT_EQ(parseConfirmationTimeoutMs("  confirmation_timeout_sec = 10  \n"), 10000);
    ASSERT_EQ(parseConfirmationTimeoutMs(
                  "camera_mode = ir\nconfirmation_timeout_sec = 15\nmatch_threshold = 0.36\n"),
              15000);
}

TEST(TimeoutIgnoresCommentedOutKey) {
    ASSERT_EQ(parseConfirmationTimeoutMs("# confirmation_timeout_sec = 5\n"),
              kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("; confirmation_timeout_sec = 5\n"),
              kDefaultConfirmationTimeoutMs);
}

TEST(TimeoutDefaultsOnUnparsableOrNonPositiveValue) {
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = abc\n"),
              kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = 5abc\n"),
              kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = 0\n"),
              kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = -5\n"),
              kDefaultConfirmationTimeoutMs);
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = \n"),
              kDefaultConfirmationTimeoutMs);
}

TEST(TimeoutClampsToBounds) {
    ASSERT_EQ(parseConfirmationTimeoutMs("confirmation_timeout_sec = 999999\n"),
              kMaxConfirmationTimeoutMs);
}

TEST(HasDisplayEnvTrueWhenEitherSet) {
    ASSERT_TRUE(hasDisplayEnv(":0", nullptr));
    ASSERT_TRUE(hasDisplayEnv(nullptr, "wayland-0"));
    ASSERT_TRUE(hasDisplayEnv(":0", "wayland-0"));
}

TEST(HasDisplayEnvFalseWhenAbsentOrEmpty) {
    ASSERT_TRUE(!hasDisplayEnv(nullptr, nullptr));
    ASSERT_TRUE(!hasDisplayEnv("", ""));
    ASSERT_TRUE(!hasDisplayEnv("", nullptr));
    ASSERT_TRUE(!hasDisplayEnv(nullptr, ""));
}

MINITEST_MAIN()
