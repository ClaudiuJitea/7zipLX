#include "FileView.h"

#include "FilePanel.h"
#include "PanelTransfer.h"
#include "models/ArchiveFolderModel.h"
#include "models/LocalFsModel.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSet>
#include <QUrl>

#include "backend/MessageDialog.h"

namespace {
bool s_internalDropHandled = false;

QStringList urlsToLocalPaths(const QList<QUrl> &urls)
{
  QStringList paths;
  for (const QUrl &url : urls)
  {
    if (url.isLocalFile())
      paths << url.toLocalFile();
  }
  paths.removeDuplicates();
  return paths;
}

bool isDropMime(const QMimeData *mime)
{
  return mime && (mime->hasFormat(QString::fromLatin1(kPanelTransferMime)) || mime->hasUrls());
}
} // namespace

FileView::FileView(FilePanel *panel, QWidget *parent) : QTableView(parent), m_panel(panel)
{
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setDragEnabled(true);
  setAcceptDrops(true);
  viewport()->setAcceptDrops(true);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDefaultDropAction(Qt::CopyAction);
  setDropIndicatorShown(false);
  setDragDropOverwriteMode(false);
}

bool FileView::internalDropHandled()
{
  return s_internalDropHandled;
}

void FileView::startDrag(Qt::DropActions supportedActions)
{
  Q_UNUSED(supportedActions)
  if (!m_panel || !selectionModel())
    return;

  // If the press happened on an already selected row, Qt theoretically leaves
  // the selection intact.  Restore the snapshot anyway: some styles/paths
  // collapse it while the mouse is being dragged, which made multi-selection
  // drags only carry the clicked item.
  if (m_pressOnSelectedRow && !m_pressSelection.isEmpty() &&
      !(QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
  {
    const int selectedRows = selectionModel()->selectedRows(0).size();
    if (selectedRows < 2)
      selectionModel()->select(m_pressSelection,
                               QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  }

  PanelTransfer transfer = m_panel->selectionTransfer();
  if (!transfer.isValid())
    return;

  auto *mime = new QMimeData;
  mime->setData(QString::fromLatin1(kPanelTransferMime), transfer.encode());
  mime->setText(transfer.names.join(QLatin1Char('\n')));

  QStringList dragPaths;
  if (transfer.kind == PanelTransfer::Local)
  {
    dragPaths = transfer.localPaths;
  }
  else
  {
    // Desktop file managers only understand URL drops.  Archive items are
    // materialized here with the normal progress dialog; the copies remain in
    // the capped external-temp cache and are pruned later.
    QString error;
    dragPaths = m_panel->materializeForDrag(transfer, &error);
    if (dragPaths.isEmpty())
    {
      if (!error.isEmpty())
        MessageDialog::show(window(), FileView::tr("Drag"),
                            FileView::tr("Cannot prepare the archive items"),
                            error, MessageDialog::Kind::Error);
      return;
    }
  }
  if (!dragPaths.isEmpty())
  {
    QList<QUrl> urls;
    for (const QString &path : dragPaths)
      urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
  }

  auto *drag = new QDrag(this);
  drag->setMimeData(mime);

  const int count = transfer.count();
  const QString label = count == 1
      ? transfer.names.value(0, FileView::tr("1 item"))
      : FileView::tr("%1 items").arg(count);
  QFont font = QApplication::font();
  font.setBold(true);
  const QFontMetrics metrics(font);
  const int width = qMin(320, metrics.horizontalAdvance(label) + 28);
  QPixmap pixmap(qMax(80, width), 32);
  pixmap.fill(Qt::transparent);
  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(43, 75, 99, 235));
    painter.setPen(QPen(QColor(79, 140, 255), 1.5));
    painter.drawRoundedRect(QRectF(0.75, 0.75, pixmap.width() - 1.5, pixmap.height() - 1.5), 7, 7);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(QRect(12, 0, pixmap.width() - 24, pixmap.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(label, Qt::ElideMiddle, pixmap.width() - 24));
  }
  drag->setPixmap(pixmap);
  drag->setHotSpot(QPoint(12, pixmap.height() / 2));

  const Qt::DropAction preferred =
      (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction;
  s_internalDropHandled = false;
  const Qt::DropAction result = drag->exec(Qt::CopyAction | Qt::MoveAction, preferred);
  if (transfer.kind == PanelTransfer::Archive)
    m_panel->releaseDrag(dragPaths);

  // External file managers do not know about our archive sessions.  For local
  // sources we follow the normal desktop convention and remove the source
  // after a successful external move.
  if (result == Qt::MoveAction && !s_internalDropHandled &&
      transfer.kind == PanelTransfer::Local)
  {
    for (const QString &path : transfer.localPaths)
    {
      const QFileInfo info(path);
      if (info.isSymLink() || !info.isDir())
        QFile::remove(path);
      else
        QDir(path).removeRecursively();
    }
    emit m_panel->filesMovedExternally(transfer.localPaths);
  }
  s_internalDropHandled = false;
  m_pressOnSelectedRow = false;
}

void FileView::mousePressEvent(QMouseEvent *event)
{
  m_pressOnSelectedRow = false;
  m_pressSelection = QItemSelection();
  m_pressIndex = QModelIndex();
  if (event->button() == Qt::LeftButton)
  {
    m_pressPosition = event->pos();
    m_pressIndex = indexAt(event->pos());
    if (m_pressIndex.isValid() && selectionModel())
    {
      m_pressOnSelectedRow = selectionModel()->isRowSelected(m_pressIndex.row(), QModelIndex());
      if (m_pressOnSelectedRow && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        m_pressSelection = selectionModel()->selection();
    }
  }
  QTableView::mousePressEvent(event);
}

void FileView::mouseMoveEvent(QMouseEvent *event)
{
  // Restore a multi-row selection before the base class decides to start the
  // drag.  This keeps Ctrl+A selections intact no matter which selected row is
  // used as the drag handle.
  if (m_pressOnSelectedRow && (event->buttons() & Qt::LeftButton) &&
      !(QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
      (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
  {
    if (selectionModel() && selectionModel()->selectedRows(0).size() < 2)
      selectionModel()->select(m_pressSelection,
                               QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  }
  QTableView::mouseMoveEvent(event);
}

void FileView::mouseReleaseEvent(QMouseEvent *event)
{
  m_pressOnSelectedRow = false;
  m_pressSelection = QItemSelection();
  QTableView::mouseReleaseEvent(event);
}

Qt::DropAction FileView::resolvedDropAction(const QDropEvent *event) const
{
  Qt::DropAction action = event->proposedAction();
  if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
    action = Qt::MoveAction;
  if (action != Qt::MoveAction && action != Qt::CopyAction)
    action = Qt::CopyAction;
  if (!(event->possibleActions() & action))
    action = Qt::CopyAction;
  return action;
}

void FileView::updateDropTarget(const QPoint &viewportPos)
{
  int row = -1;
  if (m_panel)
  {
    const QModelIndex index = indexAt(viewportPos);
    if (index.isValid())
    {
      if (m_panel->mode() == FilePanel::LocalMode)
      {
        const QFileInfo info = m_panel->localModel()->fileInfo(index.row());
        if (info.isDir())
          row = index.row();
      }
      else if (m_panel->mode() == FilePanel::ArchiveMode)
      {
        if (m_panel->archiveModel()->entry(index.row()).isDir)
          row = index.row();
      }
    }
  }
  if (row != m_dropRow)
  {
    m_dropRow = row;
    viewport()->update();
  }
  if (m_panel)
    m_panel->setDropHighlight(true);
}

void FileView::clearDropTarget()
{
  m_dropRow = -1;
  if (m_panel)
    m_panel->setDropHighlight(false);
  viewport()->update();
}

void FileView::dragEnterEvent(QDragEnterEvent *event)
{
  if (!m_panel || m_panel->mode() == FilePanel::RootMode ||
      !isDropMime(event->mimeData()))
  {
    event->ignore();
    return;
  }
  emit m_panel->activeRequested();
  updateDropTarget(event->position().toPoint());
  event->setDropAction(resolvedDropAction(event));
  event->accept();
}

void FileView::dragMoveEvent(QDragMoveEvent *event)
{
  if (!m_panel || m_panel->mode() == FilePanel::RootMode ||
      !isDropMime(event->mimeData()))
  {
    event->ignore();
    return;
  }
  const QPoint pos = event->position().toPoint();
  updateDropTarget(pos);

  // Simple edge auto-scroll while dragging inside a large listing.
  const int margin = 24;
  if (pos.y() < margin)
    verticalScrollBar()->setValue(verticalScrollBar()->value() - 3);
  else if (pos.y() > viewport()->height() - margin)
    verticalScrollBar()->setValue(verticalScrollBar()->value() + 3);

  event->setDropAction(resolvedDropAction(event));
  event->accept();
}

void FileView::dragLeaveEvent(QDragLeaveEvent *event)
{
  clearDropTarget();
  event->accept();
}

void FileView::dropEvent(QDropEvent *event)
{
  if (!m_panel || m_panel->mode() == FilePanel::RootMode ||
      !isDropMime(event->mimeData()))
  {
    event->ignore();
    return;
  }

  const QMimeData *mime = event->mimeData();
  PanelTransfer transfer;
  if (mime->hasFormat(QString::fromLatin1(kPanelTransferMime)))
    transfer = PanelTransfer::decode(mime->data(QString::fromLatin1(kPanelTransferMime)));
  if (!transfer.isValid() && mime->hasUrls())
    transfer = PanelTransfer::fromUrls(urlsToLocalPaths(mime->urls()), panelTransferIsCut(mime));
  if (!transfer.isValid())
  {
    event->ignore();
    return;
  }

  const PanelDropTarget target = m_panel->dropTargetForPosition(event->position().toPoint());
  if (!target.isValid())
  {
    event->ignore();
    return;
  }

  const Qt::DropAction action = resolvedDropAction(event);
  if (transfer.sourcePanelId >= 0)
    s_internalDropHandled = true;

  emit m_panel->activeRequested();
  emit m_panel->transferRequested(transfer, target, action);
  clearDropTarget();
  event->setDropAction(action);
  event->accept();
}

void FileView::paintEvent(QPaintEvent *event)
{
  QTableView::paintEvent(event);
  if (m_dropRow < 0 || !model())
    return;
  const QModelIndex index = model()->index(m_dropRow, 0);
  if (!index.isValid())
    return;
  QRect rect = visualRect(index);
  if (!rect.isValid())
    return;
  rect.setRight(viewport()->width() - 1);
  rect.adjust(1, 1, -1, -1);
  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(QColor(79, 140, 255, 38));
  painter.setPen(QPen(QColor(79, 140, 255), 1.5));
  painter.drawRoundedRect(rect, 4, 4);
}

void FileView::keyPressEvent(QKeyEvent *event)
{
  if (!m_panel)
    return QTableView::keyPressEvent(event);

  if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier))
  {
    m_panel->selectAllRows();
    return;
  }
  if (event->key() == Qt::Key_I && (event->modifiers() & Qt::ControlModifier))
  {
    m_panel->invertSelection();
    return;
  }
  if (event->key() == Qt::Key_Backspace)
  {
    m_panel->goUp();
    return;
  }
  if (event->key() == Qt::Key_L && (event->modifiers() & Qt::ControlModifier))
  {
    m_panel->focusPathBar();
    return;
  }
  if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)
  {
    emit m_panel->focusOtherPanelRequested();
    return;
  }
  QTableView::keyPressEvent(event);
}
