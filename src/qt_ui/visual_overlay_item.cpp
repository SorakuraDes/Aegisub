#include "viewmodels/visual_tools_vm.h"
#include "visual_overlay_item.h"
#include "video_viewport_item.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double hit_radius = 7;
constexpr double handle_radius = 3.5;
constexpr double coord_min = std::numeric_limits<int>::min();
constexpr double coord_max = std::numeric_limits<int>::max();
const QColor outline("#4fc3f7");
const QColor selected("#b4ecff");

struct ClipHandle {
    int x;
    int y;

    QPointF center(const QRectF &rect) const {
        return {x < 0 ? rect.left() : x > 0 ? rect.right() : rect.center().x(),
                y < 0 ? rect.top() : y > 0 ? rect.bottom() : rect.center().y()};
    }
};

constexpr std::array<ClipHandle, 8> handles {{
    {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}
}};

bool finite(const QPointF &point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

QPointF toScript(const QPointF &point, const QRectF &video, const QSize &res) {
    return {(point.x() - video.left()) / video.width() * res.width(),
            (point.y() - video.top()) / video.height() * res.height()};
}

QPointF fromScript(const QPointF &point, const QRectF &video, const QSize &res) {
    return {video.left() + point.x() / res.width() * video.width(),
            video.top() + point.y() / res.height() * video.height()};
}

QRectF fromScript(const QRectF &rect, const QRectF &video, const QSize &res) {
    return {fromScript(rect.topLeft(), video, res), fromScript(rect.bottomRight(), video, res)};
}

QRectF around(const QPointF &point, double radius) {
    return {point.x() - radius, point.y() - radius, radius * 2, radius * 2};
}

const ClipHandle *hitHandle(const QPointF &point, const QRectF &clip, const QRectF &video) {
    const auto region = video.adjusted(-hit_radius, -hit_radius, hit_radius, hit_radius);
    const ClipHandle *best = nullptr;
    double distance_sq = hit_radius * hit_radius;
    for (const auto &handle : handles) {
        const auto center = handle.center(clip);
        const auto delta = center - point;
        const double candidate = QPointF::dotProduct(delta, delta);
        if (region.contains(center) && candidate < distance_sq) {
            distance_sq = candidate;
            best = &handle;
        }
    }
    return best;
}

// Keep all computations in double until coordinates have been confined to the int domain.
// Translation clamps the delta, rather than the edges, so it preserves the original size.
double roundCoord(double value) {
    return std::clamp(std::round(value), coord_min, coord_max);
}

void resizeAxis(double &low, double &high, int side, double delta) {
    if (side == 0)
        return;
    const double anchor = side < 0 ? high : low;
    double edge = roundCoord((side < 0 ? low : high) + delta);
    if (edge == anchor) {
        edge = anchor + side;
        if (edge < coord_min || edge > coord_max)
            edge = anchor - side;
    }
    low = std::min(anchor, edge);
    high = std::max(anchor, edge);
}

void drawClipOutline(QPainter *painter, const QRectF &rect, const QRectF &video) {
    // Draw only the visible parts of the original edges. Intersecting and
    // drawing a rectangle would invent edges where a clip extends off-screen.
    const double x1 = std::max(rect.left(), video.left());
    const double x2 = std::min(rect.right(), video.right());
    const double y1 = std::max(rect.top(), video.top());
    const double y2 = std::min(rect.bottom(), video.bottom());
    if (x1 <= x2 && y1 <= y2) {
        for (double x : {rect.left(), rect.right()}) {
            if (x >= video.left() && x <= video.right())
                painter->drawLine(QLineF(x, y1, x, y2));
        }
        for (double y : {rect.top(), rect.bottom()}) {
            if (y >= video.top() && y <= video.bottom())
                painter->drawLine(QLineF(x1, y, x2, y));
        }
    }
}
} // namespace

VisualOverlayItem::VisualOverlayItem(QQuickItem *parent)
: QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (!isVisible()) {
            finishDrag();
            setPointerPosition(std::nullopt);
        }
    });
    connect(this, &QQuickItem::enabledChanged, this, [this] {
        if (!isEnabled()) {
            finishDrag();
            setPointerPosition(std::nullopt);
        }
    });
}

VisualOverlayItem::~VisualOverlayItem() {
    if (vm_) {
        disconnect(vm_, nullptr, this, nullptr);
        if (drag_ != Drag::None)
            vm_->endDrag();
        vm_->clearHoverCoord();
    }
}

VideoViewportItem *VisualOverlayItem::viewport() const { return viewport_.data(); }

void VisualOverlayItem::setViewport(VideoViewportItem *viewport) {
    if (viewport_ == viewport)
        return;
    if (viewport_)
        disconnect(viewport_, nullptr, this, nullptr);
    viewport_ = viewport;
    if (viewport_) {
        connect(viewport_, &VideoViewportItem::contentRectChanged,
                this, &VisualOverlayItem::geometryInvalidated);
        connect(viewport_, &QObject::destroyed, this, [this] {
            geometryInvalidated();
            emit viewportChanged();
        });
    }
    geometryInvalidated();
    emit viewportChanged();
}

VisualToolsViewModel *VisualOverlayItem::viewModel() const { return vm_.data(); }

void VisualOverlayItem::setViewModel(VisualToolsViewModel *view_model) {
    if (vm_ == view_model)
        return;
    finishDrag();
    if (vm_) {
        disconnect(vm_, nullptr, this, nullptr);
        vm_->clearHoverCoord();
    }
    vm_ = view_model;
    if (vm_) {
        connect(vm_, &VisualToolsViewModel::activeToolChanged, this, [this] {
            finishDrag();
            update();
        });
        connect(vm_, &VisualToolsViewModel::visualStateChanged, this, [this] {
            if (drag_ != Drag::None && (!vm_->hasActiveLine() || drag_res_ != playRes()))
                finishDrag();
            update();
        });
        connect(vm_, &VisualToolsViewModel::dragEnded, this, &VisualOverlayItem::clearDrag);
        connect(vm_, &QObject::destroyed, this, [this] {
            clearDrag();
            update();
            emit viewModelChanged();
        });
    }
    refreshHover();
    update();
    emit viewModelChanged();
}

QSize VisualOverlayItem::playRes() const {
    return vm_ ? QSize(vm_->playResX(), vm_->playResY()) : QSize();
}

QRectF VisualOverlayItem::videoRect() const {
    if (!viewport_ || viewport_->contentRect().isEmpty())
        return {};
    return mapRectFromItem(viewport_, viewport_->contentRect());
}

QRectF VisualOverlayItem::scriptClip() const {
    if (!vm_->hasRectClip())
        return QRectF(QPointF(), QSizeF(playRes()));
    return QRectF(QPointF(vm_->clipX1(), vm_->clipY1()),
                  QPointF(vm_->clipX2(), vm_->clipY2())).normalized();
}

void VisualOverlayItem::paint(QPainter *painter) {
    const QRectF video = videoRect();
    const QSize res = playRes();
    if (!vm_ || !vm_->hasActiveLine() || video.isEmpty() || res.isEmpty())
        return;
    const auto tool = vm_->activeTool();
    if (tool == VisualToolsViewModel::DragPos) {
        const QPointF center = fromScript(QPointF(vm_->posX(), vm_->posY()), video, res);
        if (!video.adjusted(-hit_radius, -hit_radius, hit_radius, hit_radius).contains(center))
            return;
        const bool highlighted = drag_ == Drag::Position
            || (pointer_ && around(center, hit_radius).contains(*pointer_));
        const QColor color = highlighted ? selected : outline;
        const QLineF horizontal(center - QPointF(9, 0), center + QPointF(9, 0));
        const QLineF vertical(center - QPointF(0, 9), center + QPointF(0, 9));
        painter->setPen(QPen(Qt::black, 3.5));
        painter->drawLines({horizontal, vertical});
        painter->setPen(QPen(color, 1.5));
        painter->drawLines({horizontal, vertical});
        painter->setBrush(vm_->hasExplicitPos() ? QBrush(color) : QBrush(Qt::black));
        painter->drawRect(around(center, handle_radius));
    }
    else if (tool == VisualToolsViewModel::RectClip) {
        const QRectF clip = fromScript(scriptClip(), video, res);
        QPainterPath outside;
        outside.addRect(video);
        const auto inside = clip.intersected(video);
        if (!inside.isEmpty())
            outside.addRect(inside); // Odd-even fill leaves the clip interior unshaded.
        painter->fillPath(outside, QColor(0, 0, 0, 115));
        QPen pen(outline, 1.5, vm_->hasRectClip() ? Qt::SolidLine : Qt::DashLine);
        painter->setPen(pen);
        drawClipOutline(painter, clip, video);
        const auto hovered = pointer_ ? hitHandle(*pointer_, clip, video) : nullptr;
        for (const auto &handle : handles) {
            const auto center = handle.center(clip);
            if (!video.adjusted(-hit_radius, -hit_radius, hit_radius, hit_radius).contains(center))
                continue;
            painter->setPen(QPen(Qt::black, 1.25));
            painter->setBrush(&handle == hovered ? selected : outline);
            painter->drawRect(around(center, handle_radius));
        }
    }
}

void VisualOverlayItem::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) {
    QQuickPaintedItem::geometryChange(new_geometry, old_geometry);
    if (new_geometry != old_geometry)
        geometryInvalidated();
}

void VisualOverlayItem::geometryInvalidated() {
    finishDrag();
    // A stored local position may no longer match the cursor after layout changes.
    setPointerPosition(std::nullopt);
}

void VisualOverlayItem::setPointerPosition(std::optional<QPointF> point) {
    pointer_ = point;
    refreshHover();
    update();
}

void VisualOverlayItem::refreshHover() {
    if (!vm_)
        return;
    const QRectF video = videoRect();
    const QSize res = playRes();
    if (pointer_ && !video.isEmpty() && !res.isEmpty() && video.contains(*pointer_)) {
        const auto coord = toScript(*pointer_, video, res);
        if (finite(coord)) {
            vm_->setHoverCoord(coord.x(), coord.y());
            return;
        }
    }
    vm_->clearHoverCoord();
}

void VisualOverlayItem::hoverEnterEvent(QHoverEvent *event) {
    setPointerPosition(event->position());
}

void VisualOverlayItem::hoverMoveEvent(QHoverEvent *event) {
    setPointerPosition(event->position());
}

void VisualOverlayItem::hoverLeaveEvent(QHoverEvent *) {
    setPointerPosition(std::nullopt);
}

void VisualOverlayItem::mousePressEvent(QMouseEvent *event) {
    const auto point = event->position();
    setPointerPosition(point);
    event->ignore();
    const QRectF video = videoRect();
    const QSize res = playRes();
    if (!vm_ || !vm_->hasActiveLine() || video.isEmpty() || res.isEmpty()
        || drag_ != Drag::None || event->button() != Qt::LeftButton)
        return;
    const QPointF coord = toScript(point, video, res);
    if (!finite(coord))
        return;

    const bool in_video = video.contains(point);
    Drag drag = Drag::None;
    bool reposition = false;
    QPointF pos;
    QRectF clip;
    const ClipHandle *handle = nullptr;
    const auto tool = vm_->activeTool();
    if (tool == VisualToolsViewModel::DragPos) {
        pos = QPointF(vm_->posX(), vm_->posY());
        const bool on_handle = around(fromScript(pos, video, res), hit_radius).contains(point);
        if (!in_video && !on_handle)
            return;
        drag = Drag::Position;
        reposition = !on_handle;
        if (reposition)
            pos = coord;
    }
    else if (tool == VisualToolsViewModel::RectClip) {
        clip = scriptClip();
        handle = hitHandle(point, fromScript(clip, video, res), video);
        if (handle)
            drag = Drag::ResizeClip;
        else if (!in_video)
            return;
        else if (clip.contains(coord))
            drag = Drag::TranslateClip;
        else
            drag = Drag::NewClip;
    }
    if (drag == Drag::None || !vm_->beginDrag())
        return;

    drag_ = drag;
    drag_video_ = video;
    drag_res_ = res;
    drag_start_ = coord;
    pos_start_ = pos;
    clip_start_ = clip;
    resize_x_ = handle ? handle->x : 0;
    resize_y_ = handle ? handle->y : 0;
    setKeepMouseGrab(true);
    event->accept();
    if (reposition)
        vm_->updatePos(pos.x(), pos.y());
}

void VisualOverlayItem::moveDrag(const QPointF &point) {
    if (drag_ == Drag::None || !vm_)
        return;
    if (!vm_->hasActiveLine() || drag_res_ != playRes() || drag_video_ != videoRect()) {
        finishDrag();
        return;
    }
    const QPointF coord = toScript(point, drag_video_, drag_res_);
    const QPointF delta = coord - drag_start_;
    if (!finite(coord) || !finite(delta))
        return;

    if (drag_ == Drag::Position) {
        // Avoid floating-point residue rewriting an untouched position on release.
        if (delta.isNull() && pos_start_ == QPointF(vm_->posX(), vm_->posY()))
            return;
        const QPointF pos = pos_start_ + delta;
        if (finite(pos))
            vm_->updatePos(pos.x(), pos.y());
        return;
    }

    double x1 = clip_start_.left(), x2 = clip_start_.right();
    double y1 = clip_start_.top(), y2 = clip_start_.bottom();
    switch (drag_) {
        case Drag::TranslateClip: {
            const double dx = std::clamp(std::round(delta.x()), coord_min - x1, coord_max - x2);
            const double dy = std::clamp(std::round(delta.y()), coord_min - y1, coord_max - y2);
            x1 += dx; x2 += dx;
            y1 += dy; y2 += dy;
            break;
        }
        case Drag::ResizeClip:
            if (std::round(delta.x()) != 0 && resize_x_ != 0)
                resizeAxis(x1, x2, resize_x_, delta.x());
            if (std::round(delta.y()) != 0 && resize_y_ != 0)
                resizeAxis(y1, y2, resize_y_, delta.y());
            break;
        case Drag::NewClip: {
            const double ax = roundCoord(drag_start_.x()), ay = roundCoord(drag_start_.y());
            const double bx = roundCoord(coord.x()), by = roundCoord(coord.y());
            x1 = std::min(ax, bx); x2 = std::max(ax, bx);
            y1 = std::min(ay, by); y2 = std::max(ay, by);
            if (x1 == x2 || y1 == y2)
                return;
            break;
        }
        default:
            return;
    }
    if (scriptClip() != QRectF(QPointF(x1, y1), QPointF(x2, y2)))
        vm_->updateRectClip(int(x1), int(y1), int(x2), int(y2));
}

void VisualOverlayItem::mouseMoveEvent(QMouseEvent *event) {
    setPointerPosition(event->position());
    if (drag_ == Drag::None) {
        event->ignore();
        return;
    }
    event->accept();
    moveDrag(event->position());
}

void VisualOverlayItem::mouseReleaseEvent(QMouseEvent *event) {
    setPointerPosition(event->position());
    if (drag_ == Drag::None || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    event->accept();
    moveDrag(event->position());
    finishDrag();
}

void VisualOverlayItem::clearDrag() {
    if (drag_ == Drag::None)
        return;
    drag_ = Drag::None;
    setKeepMouseGrab(false);
    ungrabMouse();
    update();
}

void VisualOverlayItem::finishDrag() {
    if (drag_ == Drag::None)
        return;
    clearDrag();
    if (vm_)
        vm_->endDrag();
}

void VisualOverlayItem::mouseUngrabEvent() {
    finishDrag();
    setPointerPosition(std::nullopt);
}
