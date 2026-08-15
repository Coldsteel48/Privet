#include "EnrollHelperRunner.hpp"

#include "generated/facial_auth_paths.hpp"

EnrollHelperRunner::EnrollHelperRunner(QObject* parent)
    : QObject(parent), process_(new QProcess(this)) {
    qRegisterMetaType<Result>();

    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                const QString output = QString::fromUtf8(process_->readAllStandardOutput());
                emit finished(parseOutput(output, exitCode));
            });

    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            Result result;
            result.ok = false;
            result.message = "failed to start pkexec — is polkit installed and an "
                              "authentication agent running in this session?";
            emit finished(result);
        }
    });
}

bool EnrollHelperRunner::isBusy() const {
    return process_->state() != QProcess::NotRunning;
}

void EnrollHelperRunner::run(const QStringList& enrollArgs) {
    if (isBusy()) return;
    QStringList args;
    args << QString::fromUtf8(facial_auth::paths::kEnrollBinary) << enrollArgs;
    process_->start("pkexec", args);
}

void EnrollHelperRunner::enroll(const QString& username, bool reEnroll, const QString& cameraMode,
                                 bool iUnderstandTheRisk) {
    QStringList args{"--user", username, "--camera-mode", cameraMode};
    if (reEnroll) args << "--re-enroll";
    if (iUnderstandTheRisk) args << "--i-understand-the-risk";
    run(args);
}

void EnrollHelperRunner::remove(const QString& username) {
    run({"--user", username, "--delete"});
}

void EnrollHelperRunner::queryStatus(const QString& username) {
    run({"--status", "--user", username});
}

void EnrollHelperRunner::writeConfig(const QStringList& setKeyValuePairs) {
    QStringList args{"--write-config"};
    for (const auto& kv : setKeyValuePairs) {
        args << "--set" << kv;
    }
    run(args);
}

EnrollHelperRunner::Result EnrollHelperRunner::parseOutput(const QString& stdoutText,
                                                            int exitCode) const {
    // facial-auth-enroll prints exactly one machine-parseable line, e.g.:
    //   STATUS=ok SAMPLES=8 CAMERA_MODE=ir
    //   STATUS=ok ENROLLED=true CAMERA_MODE=ir SAMPLES=8 ENROLLED_AT="2026-08-16T12:00:00Z"
    //   STATUS=error MESSAGE="camera busy"
    const QString line = stdoutText.trimmed();

    const auto extract = [&](const QString& key) -> QString {
        const QString marker = key + "=";
        int idx = line.indexOf(marker);
        if (idx < 0) return {};
        idx += marker.size();
        if (idx < line.size() && line[idx] == QLatin1Char('"')) {
            const int end = line.indexOf(QLatin1Char('"'), idx + 1);
            return end > idx ? line.mid(idx + 1, end - idx - 1) : QString();
        }
        const int end = line.indexOf(QLatin1Char(' '), idx);
        return end > idx ? line.mid(idx, end - idx) : line.mid(idx);
    };

    Result result;
    result.ok = exitCode == 0 && line.contains(QLatin1String("STATUS=ok"));
    result.message = extract("MESSAGE");
    result.cameraMode = extract("CAMERA_MODE");
    result.enrolledAt = extract("ENROLLED_AT");
    result.enrolled = extract("ENROLLED") == QLatin1String("true");
    const QString samples = extract("SAMPLES");
    if (!samples.isEmpty()) result.samples = samples.toInt();

    if (!result.ok && result.message.isEmpty()) {
        result.message = exitCode != 0 ? QString("helper exited with code %1").arg(exitCode)
                                        : QString("unexpected output from facial-auth-enroll");
    }
    return result;
}
