#pragma once

#include <QObject>
#include <QString>

/// Read-only view model exposing basic information about the Aegisub core
/// that this Qt UI process is linked against.
class AppInfoViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppInfoViewModel(QObject *parent = nullptr);

    /// Aegisub core version string, taken from the same version.cpp/git_version.h
    /// machinery the wxWidgets executable uses.
    QString version() const;

private:
    QString version_;
};
