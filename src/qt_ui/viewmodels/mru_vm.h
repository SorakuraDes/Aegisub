#pragma once

#include <libaegisub/signal.h>
#include <QObject>
#include <QStringList>

class MruViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList subtitles READ subtitles NOTIFY changed)
    Q_PROPERTY(QStringList videos READ videos NOTIFY changed)
    Q_PROPERTY(QStringList audio READ audio NOTIFY changed)
public:
    explicit MruViewModel(QObject *parent = nullptr);
    QStringList subtitles() const { return lists_[0]; }
    QStringList videos() const { return lists_[1]; }
    QStringList audio() const { return lists_[2]; }
    void recordSubtitle(const QString &path) { record(0, path); }
    void recordVideo(const QString &path) { record(1, path); }
    void recordAudio(const QString &path) { record(2, path); }
signals:
    void changed();
private:
    void record(int kind, const QString &path);
    void applyLimit();
    void save() const;

    QString filePath_;
    QStringList lists_[3];
    int limit_ = 16;
    agi::signal::Connection limitConnection_;
};
