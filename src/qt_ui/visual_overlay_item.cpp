#include "viewmodels/visual_tools_vm.h"
#include "visual_overlay_item.h"
#include "video_viewport_item.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double hit_radius = 7;
constexpr double handle_radius = 3.5;
constexpr double tool_hit_radius = 8;
constexpr double rotation_radius = 56;
constexpr double rotation_dead_zone = 5;
constexpr double radians_per_degree = 3.14159265358979323846 / 180;
constexpr double scale_inset = 10;
constexpr double scale_max_extent = 104;
constexpr double scale_max_value = 10000;
constexpr double coord_min = std::numeric_limits<int>::min();
constexpr double coord_max = std::numeric_limits<int>::max();

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
double length(const QPointF &point) {
    return std::hypot(point.x(), point.y());
}

QPointF unit(const QPointF &point) {
    const double distance = length(point);
    return distance > 0 ? point / distance : QPointF(1, 0);
}

std::array<QPointF, 2> moveEndpoints(const VisualToolsViewModel &vm) {
    return {QPointF(vm.moveX1(), vm.moveY1()), QPointF(vm.moveX2(), vm.moveY2())};
}

std::array<QPointF, 2> moveHandles(const VisualToolsViewModel &vm, const QRectF &video, const QSize &res) {
    auto ends = moveEndpoints(vm);
    for (auto &end : ends)
        end = fromScript(end, video, res);
    const auto vector = ends[1] - ends[0];
    const double distance = length(vector);
    // Keep both endpoints independently grabbable even when the line is stationary.
    if (distance < 18) {
        const auto offset = unit(vector) * ((18 - distance) / 2);
        ends[0] -= offset;
        ends[1] += offset;
    }
    return ends;
}

template<size_t N>
int hitToolHandle(const QPointF &point, const std::array<QPointF, N> &centers,
                  const QRectF &allowed) {
    int best = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < N; ++i) {
        const auto &center = centers[i];
        const auto delta = point - center;
        if (!allowed.contains(center) || std::abs(delta.x()) > tool_hit_radius
            || std::abs(delta.y()) > tool_hit_radius)
            continue;
        const double distance = QPointF::dotProduct(delta, delta);
        if (distance < nearest) {
            best = int(i);
            nearest = distance;
        }
    }
    return best;
}

bool hitRotation(const QPointF &point, const QPointF &origin) {
    return std::abs(length(point - origin) - rotation_radius) <= tool_hit_radius;
}

double scriptAngle(const QPointF &vector) {
    // ASS angles increase counterclockwise; script Y still increases downward.
    return std::atan2(-vector.y(), vector.x()) / radians_per_degree;
}

QPointF rotationRay(double angle, const QRectF &video, const QSize &res) {
    const double radians = std::remainder(angle, 360.0) * radians_per_degree;
    return unit({std::cos(radians) * video.width() / res.width(),
                 -std::sin(radians) * video.height() / res.height()}) * rotation_radius;
}

struct ScaleGeometry {
    QRectF box;
    double pixels_per_doubling;

    std::array<QPointF, 3> handles() const {
        return {QPointF(box.right(), box.center().y()),
                QPointF(box.center().x(), box.bottom()), box.bottomRight()};
    }
};

std::optional<ScaleGeometry> scaleGeometry(const VisualToolsViewModel &vm, const QRectF &video,
                                           const QSize &res) {
    const double limit = std::min({scale_max_extent, video.width() - 2 * scale_inset,
                                  video.height() - 2 * scale_inset});
    if (limit < 24)
        return std::nullopt;
    const auto mapped = fromScript({vm.posX(), vm.posY()}, video, res);
    if (!finite(mapped) || !std::isfinite(vm.scaleX()) || !std::isfinite(vm.scaleY()))
        return std::nullopt;
    const QPointF anchor(std::clamp(mapped.x(), video.left() + scale_inset,
                                  video.right() - scale_inset - limit),
                         std::clamp(mapped.y(), video.top() + scale_inset,
                                  video.bottom() - scale_inset - limit));
    const double base = limit * 8 / 13;
    const double pixels_per_doubling = base / 2;
    const auto extent = [=](double scale) {
        return std::clamp(base + pixels_per_doubling * std::log2(std::max(1.0, scale) / 100),
                          std::min(24.0, limit * 0.75), limit);
    };
    return ScaleGeometry{QRectF(anchor, QSizeF(extent(vm.scaleX()), extent(vm.scaleY()))),
                         pixels_per_doubling};
}

void drawLabel(QPainter *painter, const QRectF &video, QPointF point, const QString &text,
               const QColor &color) {
    auto font = painter->font();
    font.setPixelSize(12);
    painter->setFont(font);
    const auto metrics = painter->fontMetrics();
    const QSizeF size(metrics.horizontalAdvance(text) + 8, metrics.height() + 4);
    if (video.width() < size.width() || video.height() < size.height())
        return;
    point.setX(std::clamp(point.x(), video.left(), video.right() - size.width()));
    point.setY(std::clamp(point.y(), video.top(), video.bottom() - size.height()));
    const QRectF label(point, size);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 220));
    painter->drawRoundedRect(label, 3, 3);
    painter->setPen(color);
    painter->drawText(label, Qt::AlignCenter, text);
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

void VisualOverlayItem::setAccentColor(const QColor &color) {
    if (!color.isValid() || accent_color_ == color)
        return;
    accent_color_ = color;
    update();
    emit colorsChanged();
}

void VisualOverlayItem::setHighlightColor(const QColor &color) {
    if (!color.isValid() || highlight_color_ == color)
        return;
    highlight_color_ = color;
    update();
    emit colorsChanged();
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
    painter->setRenderHint(QPainter::Antialiasing);
    const auto &outline = accent_color_;
    const auto &selected = highlight_color_;
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
    else if (tool == VisualToolsViewModel::Move) {
        const auto ends = moveHandles(*vm_, video, res);
        if (!finite(ends[0]) || !finite(ends[1]))
            return;
        const auto allowed = video.adjusted(-tool_hit_radius, -tool_hit_radius,
                                            tool_hit_radius, tool_hit_radius);
        const int hovered = pointer_ ? hitToolHandle(*pointer_, ends, allowed) : -1;
        const QLineF segment(ends[0], ends[1]);
        // Match dash spacing between the contrasting underlay and the accent stroke.
        for (bool underlay : {true, false}) {
            QPen pen(underlay ? QColor(Qt::black) : accent_color_, underlay ? 3.5 : 1.5);
            pen.setDashPattern({6 / pen.widthF(), 4 / pen.widthF()});
            pen.setCapStyle(Qt::FlatCap);
            painter->setPen(pen);
            painter->drawLine(segment);
        }
        if (allowed.contains(ends[1])) {
            const auto direction = unit(ends[1] - ends[0]);
            const QPointF normal(-direction.y(), direction.x());
            const auto tip = ends[1] - direction * 5;
            const auto back = tip - direction * 8;
            painter->setPen(QPen(Qt::black, 1.25));
            painter->setBrush(accent_color_);
            painter->drawPolygon(QPolygonF(QList<QPointF>{tip, back + normal * 4, back - normal * 4}));
        }
        for (int i = 0; i < 2; ++i) {
            if (!allowed.contains(ends[i]))
                continue;
            const auto color = hovered == i || drag_ == (i == 0 ? Drag::MoveStart : Drag::MoveEnd)
                ? highlight_color_ : accent_color_;
            painter->setPen(QPen(Qt::black, 3.5));
            painter->setBrush(vm_->hasExplicitMove() ? QBrush(color) : QBrush(Qt::black));
            const auto draw = [&] {
                if (i == 0)
                    painter->drawRect(around(ends[i], 4));
                else
                    painter->drawEllipse(around(ends[i], 4));
            };
            draw();
            painter->setPen(QPen(color, 1.5));
            draw();
        }
    }
    else if (tool == VisualToolsViewModel::RotateZ) {
        const auto origin = fromScript({vm_->orgX(), vm_->orgY()}, video, res);
        const double angle = vm_->rotZ();
        if (!finite(origin) || !std::isfinite(angle)
            || !video.intersects(around(origin, rotation_radius + tool_hit_radius)))
            return;
        const auto color = drag_ == Drag::Rotation || (pointer_ && hitRotation(*pointer_, origin))
            ? highlight_color_ : accent_color_;
        const auto ray = rotationRay(angle, video, res);
        painter->setBrush(Qt::NoBrush);
        for (bool underlay : {true, false}) {
            painter->setPen(QPen(underlay ? QColor(Qt::black) : color, underlay ? 3.5 : 1.5));
            painter->drawEllipse(around(origin, rotation_radius));
            painter->drawLine(QLineF(origin, origin + ray));
        }
        painter->setPen(QPen(Qt::black, 1.25));
        painter->setBrush(color);
        painter->drawEllipse(around(origin + ray, 4.5));
        if (!vm_->hasExplicitOrg())
            painter->setBrush(Qt::black);
        painter->setPen(QPen(color, 1.5));
        painter->drawEllipse(around(origin, handle_radius));
        drawLabel(painter, video, origin + QPointF(8, 8),
                  QString::number(angle, 'g', 5) + QChar(0x00b0), Qt::white);
    }
    else if (tool == VisualToolsViewModel::Scale) {
        const auto geometry = scaleGeometry(*vm_, video, res);
        if (!geometry)
            return;
        const auto centers = geometry->handles();
        const auto &box = geometry->box;
        const int hovered = pointer_ ? hitToolHandle(*pointer_, centers, video) : -1;
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(Qt::black, 3.5));
        painter->drawRect(box);
        painter->setPen(QPen(accent_color_, 1.5));
        painter->drawRect(box);
        const auto sx = QString::number(vm_->scaleX(), 'g', 5);
        const auto sy = QString::number(vm_->scaleY(), 'g', 5);
        const QPointF label(box.left() + 4, box.top() + 4);
        drawLabel(painter, video, label, QStringLiteral("X %1%  Y %2%").arg(sx, sy), Qt::white);
        const std::array<Drag, 3> parts{Drag::ScaleX, Drag::ScaleY, Drag::ScaleCorner};
        for (int i = 0; i < 3; ++i) {
            const auto &center = centers[i];
            painter->setPen(QPen(Qt::black, 1.5));
            painter->setBrush(hovered == i || drag_ == parts[i] ? highlight_color_ : accent_color_);
            if (i == 2)
                painter->drawPolygon(QPolygonF(QList<QPointF>{center + QPointF(0, -5), center + QPointF(5, 0),
                                                              center + QPointF(0, 5), center + QPointF(-5, 0)}));
            else
                painter->drawRect(around(center, 4));
        }
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
    QPointF rotation_origin;
    std::optional<ScaleGeometry> scale_geometry;
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
    else if (tool == VisualToolsViewModel::Move) {
        const auto ends = moveHandles(*vm_, video, res);
        const int endpoint = hitToolHandle(point, ends, video.adjusted(-tool_hit_radius, -tool_hit_radius,
                                                                       tool_hit_radius, tool_hit_radius));
        if (endpoint >= 0)
            drag = endpoint == 0 ? Drag::MoveStart : Drag::MoveEnd;
    }
    else if (tool == VisualToolsViewModel::RotateZ) {
        rotation_origin = QPointF(vm_->orgX(), vm_->orgY());
        if (in_video && std::isfinite(vm_->rotZ()) && hitRotation(point, fromScript(rotation_origin, video, res)))
            drag = Drag::Rotation;
    }
    else if (tool == VisualToolsViewModel::Scale) {
        scale_geometry = scaleGeometry(*vm_, video, res);
        if (scale_geometry) {
            const int part = hitToolHandle(point, scale_geometry->handles(), video);
            const std::array<Drag, 3> parts{Drag::ScaleX, Drag::ScaleY, Drag::ScaleCorner};
            if (part >= 0)
                drag = parts[part];
        }
    }
    else if (tool == VisualToolsViewModel::RectClip) {
        clip = scriptClip();
        handle = hitHandle(point, fromScript(clip, video, res), video);
        if (handle)
            drag = Drag::ResizeClip;
        else if (!in_video)
            return;
        // The full-frame guide for an unclipped line is not an existing clip to move.
        else if (vm_->hasRectClip() && clip.contains(coord))
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
    drag_start_screen_ = point;
    if (drag == Drag::MoveStart || drag == Drag::MoveEnd)
        move_start_ = moveEndpoints(*vm_);
    if (drag == Drag::Rotation) {
        rotation_origin_ = rotation_origin;
        rotation_last_ray_ = scriptAngle(coord - rotation_origin_);
        rotation_delta_ = 0;
        rotation_start_ = vm_->rotZ();
    }
    if (scale_geometry) {
        scale_start_ = QPointF(vm_->scaleX(), vm_->scaleY());
        scale_pixels_per_doubling_ = scale_geometry->pixels_per_doubling;
    }
    pos_start_ = pos;
    clip_start_ = clip;
    resize_x_ = handle ? handle->x : 0;
    resize_y_ = handle ? handle->y : 0;
    setKeepMouseGrab(true);
    event->accept();
    if (reposition)
        vm_->updatePos(pos.x(), pos.y());
}

void VisualOverlayItem::moveDrag(const QPointF &point, Qt::KeyboardModifiers modifiers) {
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

    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const QPointF screen_delta = point - drag_start_screen_;
    if (drag_ == Drag::MoveStart || drag_ == Drag::MoveEnd) {
        auto offset = delta;
        if (shift) {
            if (std::abs(screen_delta.x()) >= std::abs(screen_delta.y()))
                offset.setY(0);
            else
                offset.setX(0);
        }
        auto ends = move_start_;
        ends[drag_ == Drag::MoveStart ? 0 : 1] += offset;
        if (finite(ends[0]) && finite(ends[1]) && ends != moveEndpoints(*vm_))
            vm_->updateMove(ends[0].x(), ends[0].y(), ends[1].x(), ends[1].y());
        return;
    }
    if (drag_ == Drag::Rotation) {
        if (length(point - fromScript(rotation_origin_, drag_video_, drag_res_)) < rotation_dead_zone) {
            rotation_last_ray_.reset();
            return;
        }
        const double ray = scriptAngle(coord - rotation_origin_);
        if (rotation_last_ray_)
            rotation_delta_ += std::remainder(ray - *rotation_last_ray_, 360.0);
        rotation_last_ray_ = ray;
        double angle = rotation_start_ + rotation_delta_;
        if (shift && (!screen_delta.isNull() || rotation_delta_ != 0))
            angle = std::round(angle / 15) * 15;
        if (std::isfinite(angle) && angle != vm_->rotZ())
            vm_->updateRotationZ(angle);
        return;
    }
    if (drag_ == Drag::ScaleX || drag_ == Drag::ScaleY || drag_ == Drag::ScaleCorner) {
        const bool corner = drag_ == Drag::ScaleCorner;
        const bool proportional = corner || shift;
        const double travel = corner ? (screen_delta.x() + screen_delta.y()) / 2
                            : drag_ == Drag::ScaleX ? screen_delta.x() : screen_delta.y();
        const double exponent = std::clamp(travel / scale_pixels_per_doubling_, -100.0, 100.0);
        const double factor = std::exp2(exponent);
        // Scale from the values captured on press, not from a potentially clamped guide.
        const auto resized = [=](double initial, double limit) {
            if (initial == 0)
                return std::clamp(100 * (factor - 1), 0.0, limit);
            return std::clamp(initial * factor, 0.0, limit);
        };
        auto scale = scale_start_;
        if (proportional) {
            const double largest = std::max(scale.x(), scale.y());
            if (largest <= 0) {
                const double size = resized(0, scale_max_value);
                scale = QPointF(size, size);
            }
            else {
                // Keep one common factor at both limits for valid nonzero scales.
                const double upper = std::max(scale_max_value, largest) / largest;
                const double smallest = std::min(scale.x(), scale.y());
                const double lower = smallest > 0 ? std::min(1.0 / smallest, upper) : 0.0;
                scale *= std::clamp(factor, lower, upper);
            }
        }
        else if (drag_ == Drag::ScaleX) {
            scale.setX(resized(scale.x(), std::max(scale_max_value, scale.x())));
        }
        else {
            scale.setY(resized(scale.y(), std::max(scale_max_value, scale.y())));
        }
        if (finite(scale) && scale != QPointF(vm_->scaleX(), vm_->scaleY()))
            vm_->updateScale(scale.x(), scale.y());
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
    moveDrag(event->position(), event->modifiers());
}

void VisualOverlayItem::mouseReleaseEvent(QMouseEvent *event) {
    setPointerPosition(event->position());
    if (drag_ == Drag::None || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    event->accept();
    moveDrag(event->position(), event->modifiers());
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
