#pragma once

#include <libaegisub/signal.h>
#include <QObject>
#include <QString>
#include <string>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;
class VideoViewModel;

class VisualToolsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(int playResX READ playResX NOTIFY visualStateChanged)
    Q_PROPERTY(int playResY READ playResY NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasActiveLine READ hasActiveLine NOTIFY visualStateChanged)
    Q_PROPERTY(double posX READ posX NOTIFY visualStateChanged)
    Q_PROPERTY(double posY READ posY NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasExplicitPos READ hasExplicitPos NOTIFY visualStateChanged)
    Q_PROPERTY(double moveX1 READ moveX1 NOTIFY visualStateChanged)
    Q_PROPERTY(double moveY1 READ moveY1 NOTIFY visualStateChanged)
    Q_PROPERTY(double moveX2 READ moveX2 NOTIFY visualStateChanged)
    Q_PROPERTY(double moveY2 READ moveY2 NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasExplicitMove READ hasExplicitMove NOTIFY visualStateChanged)
    Q_PROPERTY(double rotZ READ rotZ NOTIFY visualStateChanged)
    Q_PROPERTY(double orgX READ orgX NOTIFY visualStateChanged)
    Q_PROPERTY(double orgY READ orgY NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasExplicitOrg READ hasExplicitOrg NOTIFY visualStateChanged)
    Q_PROPERTY(double scaleX READ scaleX NOTIFY visualStateChanged)
    Q_PROPERTY(double scaleY READ scaleY NOTIFY visualStateChanged)
    Q_PROPERTY(int clipX1 READ clipX1 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipY1 READ clipY1 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipX2 READ clipX2 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipY2 READ clipY2 NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasRectClip READ hasRectClip NOTIFY visualStateChanged)
    Q_PROPERTY(QString hoverCoordText READ hoverCoordText NOTIFY hoverCoordTextChanged)

public:
    enum Tool { Standard = 0, DragPos = 1, Move = 2, RotateZ = 3, Scale = 4, RectClip = 5 };
    Q_ENUM(Tool)

    VisualToolsViewModel(AssFile *file, SubtitleGridModel *gridModel,
                         VideoViewModel *videoVM, UndoManager *undoManager);

    int activeTool() const { return active_tool_; }
    void setActiveTool(int tool);
    int playResX() const { return play_res_x_; }
    int playResY() const { return play_res_y_; }
    bool hasActiveLine() const { return has_active_line_; }
    double posX() const { return pos_x_; }
    double posY() const { return pos_y_; }
    bool hasExplicitPos() const { return has_explicit_pos_; }
    double moveX1() const { return move_x1_; }
    double moveY1() const { return move_y1_; }
    double moveX2() const { return move_x2_; }
    double moveY2() const { return move_y2_; }
    bool hasExplicitMove() const { return has_explicit_move_; }
    double rotZ() const { return rot_z_; }
    double orgX() const { return org_x_; }
    double orgY() const { return org_y_; }
    bool hasExplicitOrg() const { return has_explicit_org_; }
    double scaleX() const { return scale_x_; }
    double scaleY() const { return scale_y_; }
    int clipX1() const { return clip_x1_; }
    int clipY1() const { return clip_y1_; }
    int clipX2() const { return clip_x2_; }
    int clipY2() const { return clip_y2_; }
    bool hasRectClip() const { return has_rect_clip_; }
    QString hoverCoordText() const { return hover_coord_text_; }

    bool beginDrag();
    void updatePos(double sx, double sy);
    void updateMove(double x1, double y1, double x2, double y2);
    void updateRotationZ(double angleDeg);
    void updateScale(double sx, double sy);
    void updateRectClip(int x1, int y1, int x2, int y2);
    void endDrag();
    void setHoverCoord(double sx, double sy);
    void clearHoverCoord();
    void handleDocumentReload();
    Q_INVOKABLE void clearRectClip();

signals:
    void activeToolChanged();
    void visualStateChanged();
    void hoverCoordTextChanged();
    void dragEnded();

private:
    AssDialogue *activeLine() const;
    void syncSelection();
    void refresh();
    void onFileCommitted(int type, const AssDialogue *line);
    void commitText(AssDialogue *line, std::string text, const char *description, bool amend);

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    VideoViewModel *video_vm_;
    agi::signal::Connection commit_connection_;
    int active_tool_ = Standard;
    int play_res_x_ = 384, play_res_y_ = 288;
    bool has_active_line_ = false;
    double pos_x_ = 0, pos_y_ = 0;
    bool has_explicit_pos_ = false;
    double move_x1_ = 0, move_y1_ = 0, move_x2_ = 0, move_y2_ = 0;
    bool has_explicit_move_ = false;
    double rot_z_ = 0, org_x_ = 0, org_y_ = 0;
    bool has_explicit_org_ = false;
    double scale_x_ = 100, scale_y_ = 100;
    int clip_x1_ = 0, clip_y1_ = 0, clip_x2_ = 384, clip_y2_ = 288;
    bool has_rect_clip_ = false;
    QString hover_coord_text_;
    bool dragging_ = false;
    bool committing_ = false;
    int drag_commit_id_ = -1;
};
