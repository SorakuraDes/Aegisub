#pragma once

#include <QObject>
#include <QString>

class AssFile;
class VideoViewModel;

class ResampleViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoDimensionsChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoDimensionsChanged)

public:
    ResampleViewModel(AssFile *file, VideoViewModel *videoVM, QObject *parent = nullptr);

    int sourceWidth() const { return source_width_; }
    int sourceHeight() const { return source_height_; }
    int videoWidth() const;
    int videoHeight() const;

    Q_INVOKABLE void refreshSource();
    // Returns false for invalid input without modifying the script.
    Q_INVOKABLE bool apply(const QString &width, const QString &height);

signals:
    void sourceChanged();
    void videoDimensionsChanged();

private:
    AssFile *file_;
    VideoViewModel *video_vm_;
    int source_width_ = 384;
    int source_height_ = 288;
};
