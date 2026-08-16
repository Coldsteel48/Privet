#pragma once

#include <string>

namespace facial_auth {

enum class VerifyOutcome { Match, NoMatch, Unavailable };

// The actual camera-capture + face-detect + embed + match loop, shared by
// facial-auth-verify (invoked by pam_facial.so at real login time) and
// facial-auth-enroll's --test mode (facial-auth-control's "Test
// Recognition" button). Both must run identical logic — a passing Test
// that doesn't reflect what login will actually do is worse than no Test
// at all. Reads /etc/facial-auth/config.conf and the given user's
// enrollment itself; never throws (mirrors V4L2Camera/FaceDetector's own
// exception-safety expectations onto its caller — callers still need a
// top-level catch for unexpected library-level throws).
VerifyOutcome runVerification(const std::string& username);

}  // namespace facial_auth
