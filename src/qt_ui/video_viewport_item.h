#pragma once

#include <QImage>
#include <QQuickPaintedItem>

/// QML element that paints the frame produced by VideoViewModel.
///
/// VideoViewportItem instead of Image + QQuickImageProvider: the view model owns
/// the QImage directly, and this item keeps the video's original aspect ratio
/// while letterboxing it on a pure black background. It also screenshots
/// cleanly with PrintWindow, which is how the M2a verification is captured.
///
/// Not `final`: qmlRegisterType derives an internal QQmlElement<T> from it.
class VideoViewportItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)

public:
    explicit VideoViewportItem(QQuickItem *parent = nullptr);

    QImage frame() const;
    void setFrame(const QImage &frame);

    void paint(QPainter *painter) override;

signals:
    void frameChanged();

private:
    QImage frame_;
};
