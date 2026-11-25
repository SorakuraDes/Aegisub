#pragma once

// Pull in agi::signal (via the view model) before Qt defines the slots macro.
#include "viewmodels/visual_tools_vm.h"
#include "video_viewport_item.h"

#include <QColor>
#include <QPointer>

#include <array>
#include <optional>

/// Paints and manipulates the active visual tool in script coordinates.
class VisualOverlayItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(VideoViewportItem *viewport READ viewport WRITE setViewport NOTIFY viewportChanged)
    Q_PROPERTY(VisualToolsViewModel *viewModel READ viewModel WRITE setViewModel NOTIFY viewModelChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY colorsChanged)

public:
    explicit VisualOverlayItem(QQuickItem *parent = nullptr);
    ~VisualOverlayItem() override;

    VideoViewportItem *viewport() const;
    void setViewport(VideoViewportItem *viewport);
    VisualToolsViewModel *viewModel() const;
    void setViewModel(VisualToolsViewModel *view_model);
    QColor accentColor() const { return accent_color_; }
    void setAccentColor(const QColor &color);
    QColor highlightColor() const { return highlight_color_; }
    void setHighlightColor(const QColor &color);
    void paint(QPainter *painter) override;

signals:
    void viewportChanged();
    void viewModelChanged();
    void colorsChanged();

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
    enum class Drag { None, Position, TranslateClip, ResizeClip, NewClip,
                      MoveStart, MoveEnd, Rotation, ScaleX, ScaleY, ScaleCorner };

    QSize playRes() const;
    QRectF videoRect() const;
    QRectF scriptClip() const;
    void setPointerPosition(std::optional<QPointF> point);
    void refreshHover();
    void geometryInvalidated();
    void clearDrag();
    void finishDrag();
    void moveDrag(const QPointF &point, Qt::KeyboardModifiers modifiers);

    QPointer<VideoViewportItem> viewport_;
    QPointer<VisualToolsViewModel> vm_;
    std::optional<QPointF> pointer_;
    Drag drag_ = Drag::None;
    QRectF drag_video_;
    QSize drag_res_;
    QPointF drag_start_;
    QPointF pos_start_;
    QRectF clip_start_;
    std::array<QPointF, 2> move_start_;
    QPointF rotation_origin_;
    std::optional<double> rotation_last_ray_;
    double rotation_delta_ = 0;
    double rotation_start_ = 0;
    QPointF scale_start_;
    double scale_pixels_per_doubling_ = 0;
    QPointF drag_start_screen_;
    QColor accent_color_{"#4fc3f7"};
    QColor highlight_color_{"#b4ecff"};
    int resize_x_ = 0;
    int resize_y_ = 0;
};
