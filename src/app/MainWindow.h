#pragma once

#include <QMainWindow>
#include <functional>
#include <QPoint>
#include <memory>

#include "backend/ArchiveSession.h"
#include "ui/widgets/PanelTransfer.h"

class FilePanel;
class PromptDispatcher;
class ProgressState;
class OperationRunner;
class ExternalToolController;
class HashController;
class TransferController;
class QSplitter;
class QLabel;
class QAction;

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  void openPath(const QString &path);
  void setTwoPanels(bool show);
  bool twoPanels() const { return m_twoPanels; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private slots:
  void openArchiveDialog();
  void extractArchive();
  void testArchive();
  void addToArchive();
  void goUp();
  void refreshActive();
  void closeArchive();
  void selectAll();
  void showRoot();
  void calculateHash();
  void renameSelected();
  void deleteSelected();
  void copySelected();
  void moveSelected();
  void createFolder();
  void createFile();
  void createLink();
  void editComment();
  void openExternalView();
  void openExternalEdit();
  void diffSelected();
  void showProperties();
  void addFavorite();
  void clearFavorites();
  void showOptions();
  void showBenchmark();
  void clipboardCopy();
  void clipboardCut();
  void clipboardPaste();
  void copyToOtherPanel();
  void moveToOtherPanel();
  void splitFile();
  void combineFiles();
  void showAbout();
  void openArchiveRequested(const QString &path);
  void openNestedArchive(int row);
  void showPanelContextMenu(const QPoint &globalPos);
  void handleTransfer(const PanelTransfer &transfer, const PanelDropTarget &target,
                      Qt::DropAction action);
  void focusOtherPanel();

private:
  using OperationFn = std::function<OperationResult(ProgressState *, PromptDispatcher *)>;
  OperationResult runOperation(const QString &title, const OperationFn &fn);
  FilePanel *activePanel() const { return m_active; }
  FilePanel *otherPanel() const { return m_active == m_left ? m_right : m_left; }
  void transferToOtherPanel(bool move);
  bool ensureWritableArchive(const QString &action);
  bool commitNestedArchive(FilePanel *panel);
  FilePanel *panelById(int id) const;
  void setClipboardFromSelection(bool cut);
  void setActivePanel(FilePanel *panel);
  void updateActions();
  void updateThemeIcons();
  void updateWindowTitle();
  bool revealSecondPanelForTransfer();
  void openCurrentItem();
  void openInside(const QString &formatHint);
  void openCurrentArchiveInside(const QString &formatHint);
  void openOutside();
  enum class LocalExtract { Here, ToFolders, Dialog };
  void extractHere(bool toFolder = false);
  void extractLocalArchives(LocalExtract mode);
  void refreshPanelsShowing(const QString &dir);
  void testLocalArchives();
  void compressSelectionQuick(const QString &extension);
  void showCreatedArchive(const QString &archivePath);
  void hashSelection(const QString &method);
  QStringList selectedLocalArchiveFiles() const;
  void saveSettings();
  void loadSettings();

  FilePanel *m_left = nullptr;
  FilePanel *m_right = nullptr;
  FilePanel *m_active = nullptr;
  QSplitter *m_splitter = nullptr;
  QLabel *m_status = nullptr;
  PromptDispatcher *m_prompts = nullptr;
  QAction *m_extractAction = nullptr;
  QAction *m_testAction = nullptr;
  QAction *m_addAction = nullptr;
  QAction *m_closeArchiveAction = nullptr;
  QAction *m_showHiddenAction = nullptr;
  QAction *m_showDotsAction = nullptr;
  QAction *m_flatViewAction = nullptr;
  QAction *m_twoPanelsAction = nullptr;
  bool m_twoPanels = false;
  QAction *m_updateEditsAction = nullptr;
  QAction *m_openAction = nullptr;
  QAction *m_upAction = nullptr;
  QAction *m_refreshAction = nullptr;
  class QLineEdit *m_filter = nullptr;
  std::unique_ptr<OperationRunner> m_runner;
  std::unique_ptr<ExternalToolController> m_externalTools;
  std::unique_ptr<HashController> m_hash;
  std::unique_ptr<TransferController> m_transfer;
};
