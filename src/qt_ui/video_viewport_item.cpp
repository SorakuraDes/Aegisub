#include "video_viewport_item.h"

#include <QPainter>

VideoViewportItem::VideoViewportItem(QQuickItem *parent)
: QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(false);
}

QImage VideoViewportItem::frame() const { return frame_; }

void VideoViewportItem::setFrame(const QImage &frame) {
    if (frame_ == frame)
        return;

    frame_ = frame;
    update();
    emit frameChanged();
}

void VideoViewportItem::paint(QPainter *painter) {
    painter->fillRect(QRectF(0, 0, width(), height()), Qt::black);
    if (frame_.isNull())
        return;

    // Letterbox: keep the video's aspect ratio and centre it in the viewport.
    const QSizeF target = QSizeF(frame_.size()).scaled(QSizeF(width(), height()),
                                                       Qt::KeepAspectRatio);
    const QRectF destination((width() - target.width()) / 2.0,
                             (height() - target.height()) / 2.0,
                             target.width(),
                             target.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(destination, frame_);
}
