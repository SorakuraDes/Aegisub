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
    updateContentRect();
    update();
    emit frameChanged();
}

QRectF VideoViewportItem::contentRect() const { return content_rect_; }

void VideoViewportItem::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) {
    QQuickPaintedItem::geometryChange(new_geometry, old_geometry);
    if (new_geometry.size() != old_geometry.size()) {
        updateContentRect();
        update();
    }
}

void VideoViewportItem::updateContentRect() {
    QRectF destination;
    if (!frame_.isNull() && width() > 0 && height() > 0) {
        const QSizeF target = QSizeF(frame_.size()).scaled(QSizeF(width(), height()),
                                                           Qt::KeepAspectRatio);
        destination = QRectF((width() - target.width()) / 2.0,
                             (height() - target.height()) / 2.0,
                             target.width(), target.height());
    }
    if (content_rect_ == destination)
        return;
    content_rect_ = destination;
    emit contentRectChanged();
}

void VideoViewportItem::paint(QPainter *painter) {
    painter->fillRect(QRectF(0, 0, width(), height()), Qt::black);
    if (!content_rect_.isEmpty()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(content_rect_, frame_);
    }
}
