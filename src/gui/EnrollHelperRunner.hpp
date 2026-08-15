#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Wraps `pkexec facial-auth-enroll ...` invocations. facial-auth-control
// runs unprivileged; this is the ONLY way it ever touches
// /var/lib/facial-auth/ or /etc/facial-auth/config.conf — always through
// this fixed, already-privileged helper binary, never directly. See the
// project plan's "Privilege architecture" section.
//
// Requires a running polkit authentication agent in the desktop session
// (standard on most desktop environments) — pkexec falls back to a
// terminal text prompt otherwise, which won't work with QProcess's piped
// stdio.
class EnrollHelperRunner : public QObject {
    Q_OBJECT

public:
    struct Result {
        bool ok = false;       // did the helper invocation itself succeed
        QString message;       // human-readable error, if !ok
        int samples = 0;       // enroll: number of samples captured
        QString cameraMode;    // enroll/status: "ir" | "rgb"
        bool enrolled = false; // status: whether the user has an enrollment on file
        QString enrolledAt;    // status: ISO-8601 timestamp
    };

    explicit EnrollHelperRunner(QObject* parent = nullptr);

    bool isBusy() const;

    void enroll(const QString& username, bool reEnroll, const QString& cameraMode,
                bool iUnderstandTheRisk);
    void remove(const QString& username);
    void queryStatus(const QString& username);
    void writeConfig(const QStringList& setKeyValuePairs);  // each entry "key=value"

signals:
    void finished(EnrollHelperRunner::Result result);

private:
    void run(const QStringList& enrollArgs);
    Result parseOutput(const QString& stdoutText, int exitCode) const;

    QProcess* process_;
};

Q_DECLARE_METATYPE(EnrollHelperRunner::Result)
