#pragma once

#include <QItemSelection>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QTableView>

class FilePanel;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

// QTableView with the drag-and-drop and selection behaviour required by a
// dual-pane file manager.  Starting a drag never mutates the source selection
// (Qt's default startDrag() clears the selection for MoveAction).
class FileView : public QTableView
{
  Q_OBJECT
public:
  explicit FileView(FilePanel *panel, QWidget *parent = nullptr);

  static bool internalDropHandled();

protected:
  void startDrag(Qt::DropActions supportedActions) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  Qt::DropAction resolvedDropAction(const QDropEvent *event) const;
  void updateDropTarget(const QPoint &viewportPos);
  void clearDropTarget();

  FilePanel *m_panel = nullptr;
  QPoint m_pressPosition;
  QPersistentModelIndex m_pressIndex;
  QItemSelection m_pressSelection;
  bool m_pressOnSelectedRow = false;
  int m_dropRow = -1;
};
