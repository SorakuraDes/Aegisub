#pragma once

// Pull in agi::signal (via the view model) before Qt defines the slots macro.
#include "viewmodels/visual_tools_vm.h"
#include "video_viewport_item.h"

#include <QPointer>

#include <optional>

/// Paints and manipulates the active visual tool in script coordinates.
class VisualOverlayItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(VideoViewportItem *viewport READ viewport WRITE setViewport NOTIFY viewportChanged)
    Q_PROPERTY(VisualToolsViewModel *viewModel READ viewModel WRITE setViewModel NOTIFY viewModelChanged)

public:
    explicit VisualOverlayItem(QQuickItem *parent = nullptr);
    ~VisualOverlayItem() override;

    VideoViewportItem *viewport() const;
    void setViewport(VideoViewportItem *viewport);
    VisualToolsViewModel *viewModel() const;
    void setViewModel(VisualToolsViewModel *view_model);
    void paint(QPainter *painter) override;

signals:
    void viewportChanged();
    void viewModelChanged();

protected:
    void geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;

private:
    enum class Drag { None, Position, TranslateClip, ResizeClip, NewClip };

    QSize playRes() const;
    QRectF videoRect() const;
    QRectF scriptClip() const;
    void setPointerPosition(std::optional<QPointF> point);
    void refreshHover();
    void geometryInvalidated();
    void clearDrag();
    void finishDrag();
    void moveDrag(const QPointF &point);

    QPointer<VideoViewportItem> viewport_;
    QPointer<VisualToolsViewModel> vm_;
    std::optional<QPointF> pointer_;
    Drag drag_ = Drag::None;
    QRectF drag_video_;
    QSize drag_res_;
    QPointF drag_start_;
    QPointF pos_start_;
    QRectF clip_start_;
    int resize_x_ = 0;
    int resize_y_ = 0;
};
