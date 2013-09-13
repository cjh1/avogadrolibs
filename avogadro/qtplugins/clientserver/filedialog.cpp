/*=========================================================================

   Program: ParaView
   Module:    FileDialog.cxx

   Copyright (c) 2005-2008 Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/

#include "filedialog.h"
#include "filedialogmodel.h"
#include "filedialogfilter.h"
#include <protocall/runtime/vtkcommunicatorchannel.h>

#include <QCompleter>
#include <QDir>
#include <QMessageBox>
#include <QtDebug>
#include <QPoint>
#include <QAction>
#include <QMenu>
#include <QLineEdit>
#include <QAbstractButton>
#include <QComboBox>
#include <QAbstractItemView>
#include <QPointer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QtCore/QObject>

#include <string>
#include <vtksys/SystemTools.hxx>

using ProtoCall::Runtime::vtkCommunicatorChannel;

class FileComboBox : public QComboBox
{
public:
  FileComboBox(QWidget* p) : QComboBox(p) {}
  void showPopup()
    {
    QWidget* container = this->view()->parentWidget();
    container->setMaximumWidth(this->width());
    QComboBox::showPopup();
    }
};
#include "ui_FileDialog.h"

namespace {

  QStringList MakeFilterList(const QString &filter)
  {
    QString f(filter);

    if (f.isEmpty())
      {
      return QStringList();
      }

    QString sep(";;");
    int i = f.indexOf(sep, 0);
    if (i == -1)
      {
      if (f.indexOf("\n", 0) != -1)
        {
        sep = "\n";
        i = f.indexOf(sep, 0);
        }
      }
    return f.split(sep, QString::SkipEmptyParts);
  }


  QStringList GetWildCardsFromFilter(const QString& filter)
    {
    QString f = filter;
    // if we have (...) in our filter, strip everything out but the contents of ()
    int start, end;
    start = filter.indexOf('(');
    end = filter.lastIndexOf(')');
    if(start != -1 && end != -1)
      {
      f = f.mid(start+1, end-start-1);
      }
    else if(start != -1 || end != -1)
      {
      f = QString();  // hmm...  I'm confused
      }

    // separated by spaces or semi-colons
    QStringList fs = f.split(QRegExp("[\\s+;]"), QString::SkipEmptyParts);

    // add a *.ext.* for every *.ext we get to support file groups
    QStringList ret = fs;
    foreach(QString ext, fs)
      {
      ret.append(ext + ".*");
      }
    return ret;
    }
}

/////////////////////////////////////////////////////////////////////////////
// FileDialog::pqImplementation

class FileDialog::pqImplementation : public QObject
{
public:
  FileDialogModel* const Model;
  FileDialogFilter FileFilter;
  QStringList FileNames; //list of file names in the FileName ui text edit
  QCompleter *Completer;
  FileDialog::FileMode Mode;
  Ui::FileDialog Ui;
  QList<QStringList> SelectedFiles;
  QStringList Filters;
  bool SupressOverwriteWarning;
  bool ShowMultipleFileHelp;
  QString FileNamesSeperator;

  // remember the last locations we browsed
  static QMap<vtkCommunicatorChannel *, QString> ServerFilePaths;
  static QString LocalFilePath;

  pqImplementation(FileDialog* p, vtkCommunicatorChannel *server) :
    QObject(p),
    Model(new FileDialogModel(server, NULL)),
    FileFilter(this->Model),
    Completer(new QCompleter(&this->FileFilter, NULL)),
    Mode(ExistingFile),
    SupressOverwriteWarning(false),
    ShowMultipleFileHelp(false),
    FileNamesSeperator(";")
  {

  }

  ~pqImplementation()
  {
    delete this->Model;
    delete this->Completer;
  }

  bool eventFilter(QObject *obj, QEvent *anEvent )
    {
    if ( obj == this->Ui.Files )
      {
      if ( anEvent->type() == QEvent::KeyPress )
        {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(anEvent);
        if (keyEvent->key() == Qt::Key_Backspace ||
          keyEvent->key() == Qt::Key_Delete )
          {
          this->Ui.FileName->setFocus(Qt::OtherFocusReason);
          //send out a backspace event to the file name now
          QKeyEvent replicateDelete(keyEvent->type(), keyEvent->key(), keyEvent->modifiers());
          QApplication::sendEvent( this->Ui.FileName, &replicateDelete);
          return true;
          }
        }
      return false;
      }
    return QObject::eventFilter(obj, anEvent);
    }

  QString getStartPath()
    {
    vtkCommunicatorChannel *s = this->Model->server();
    if(s)
      {
      QMap<vtkCommunicatorChannel *,QString>::iterator iter;
      iter = this->ServerFilePaths.find(this->Model->server());
      if(iter != this->ServerFilePaths.end())
        {
        return *iter;
        }
      }
    else if(!this->LocalFilePath.isEmpty())
      {
      return this->LocalFilePath;
      }
    return this->Model->getCurrentPath();
    }

  void setCurrentPath(const QString& p)
    {
    this->Model->setCurrentPath(p);
    vtkCommunicatorChannel *s = this->Model->server();
    if(s)
      {
      this->ServerFilePaths[s] = p;
      }
    else
      {
      this->LocalFilePath = p;
      }
    this->Ui.Files->setFocus(Qt::OtherFocusReason);
    }

  void addHistory(const QString& p)
    {
    this->BackHistory.append(p);
    this->ForwardHistory.clear();
    if(this->BackHistory.size() > 1)
      {
      this->Ui.NavigateBack->setEnabled(true);
      }
    else
      {
      this->Ui.NavigateBack->setEnabled(false);
      }
    this->Ui.NavigateForward->setEnabled(false);
    }
  QString backHistory()
    {
    QString path = this->BackHistory.takeLast();
    this->ForwardHistory.append(this->Model->getCurrentPath());
    this->Ui.NavigateForward->setEnabled(true);
    if(this->BackHistory.size() == 1)
      {
      this->Ui.NavigateBack->setEnabled(false);
      }
    return path;
    }
  QString forwardHistory()
    {
    QString path = this->ForwardHistory.takeLast();
    this->BackHistory.append(this->Model->getCurrentPath());
    this->Ui.NavigateBack->setEnabled(true);
    if(this->ForwardHistory.size() == 0)
      {
      this->Ui.NavigateForward->setEnabled(false);
      }
    return path;
    }

protected:
  QStringList BackHistory;
  QStringList ForwardHistory;

};

QMap<vtkCommunicatorChannel *, QString> FileDialog::pqImplementation::ServerFilePaths;
QString FileDialog::pqImplementation::LocalFilePath;

/////////////////////////////////////////////////////////////////////////////
// FileDialog

FileDialog::FileDialog(
    vtkCommunicatorChannel* server,
    QWidget* p,
    const QString& title,
    const QString& startDirectory,
    const QString& nameFilter) :
  Superclass(p),
  Implementation(new pqImplementation(this, server))
{
  this->Implementation->Ui.setupUi(this);
  // ensures that the favorites and the browser component are sized
  // proportionately.
  this->Implementation->Ui.mainSplitter->setStretchFactor(0, 1);
  this->Implementation->Ui.mainSplitter->setStretchFactor(1, 4);
  this->setWindowTitle(title);

  this->Implementation->Ui.Files->setEditTriggers(QAbstractItemView::EditKeyPressed);

  //install the event filter
  this->Implementation->Ui.Files->installEventFilter(this->Implementation);

  //install the autocompleter
  this->Implementation->Ui.FileName->setCompleter(this->Implementation->Completer);


  QPixmap back = style()->standardPixmap(QStyle::SP_FileDialogBack);
  this->Implementation->Ui.NavigateBack->setIcon(back);
  this->Implementation->Ui.NavigateBack->setEnabled(false);
  QObject::connect(this->Implementation->Ui.NavigateBack,
                   SIGNAL(clicked(bool)),
                   this, SLOT(onNavigateBack()));
  // just flip the back image to make a forward image
  QPixmap forward = QPixmap::fromImage(back.toImage().mirrored(true, false));
  this->Implementation->Ui.NavigateForward->setIcon(forward);
  this->Implementation->Ui.NavigateForward->setDisabled( true );
  QObject::connect(this->Implementation->Ui.NavigateForward,
                   SIGNAL(clicked(bool)),
                   this, SLOT(onNavigateForward()));
  this->Implementation->Ui.NavigateUp->setIcon(style()->
      standardPixmap(QStyle::SP_FileDialogToParent));
  this->Implementation->Ui.CreateFolder->setIcon(style()->
      standardPixmap(QStyle::SP_FileDialogNewFolder));
  this->Implementation->Ui.CreateFolder->setDisabled( true );

  this->Implementation->Ui.Files->setModel(&this->Implementation->FileFilter);
  this->Implementation->Ui.Files->setSelectionBehavior(QAbstractItemView::SelectRows);

  this->Implementation->Ui.Files->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(this->Implementation->Ui.Files,
                  SIGNAL(customContextMenuRequested(const QPoint &)),
                  this, SLOT(onContextMenuRequested(const QPoint &)));
  this->Implementation->Ui.CreateFolder->setEnabled( true );

  this->setFileMode(ExistingFile);

  QObject::connect(this->Implementation->Model,
                   SIGNAL(modelReset()),
                   this,
                   SLOT(onModelReset()));

  QObject::connect(this->Implementation->Ui.NavigateUp,
                   SIGNAL(clicked()),
                   this,
                   SLOT(onNavigateUp()));

  QObject::connect(this->Implementation->Ui.CreateFolder,
                   SIGNAL(clicked()),
                   this,
                   SLOT(onCreateNewFolder()));

  QObject::connect(this->Implementation->Ui.Parents,
                   SIGNAL(activated(const QString&)),
                   this,
                   SLOT(onNavigate(const QString&)));

  QObject::connect(this->Implementation->Ui.FileType,
                   SIGNAL(currentIndexChanged(const QString&)),
                   this,
                   SLOT(onFilterChange(const QString&)));

  QObject::connect(this->Implementation->Ui.Files->selectionModel(),
                  SIGNAL(
                    selectionChanged(const QItemSelection&, const QItemSelection&)),
                  this,
                  SLOT(fileSelectionChanged()));

    QObject::connect(this->Implementation->Ui.Files,
                   SIGNAL(doubleClicked(const QModelIndex&)),
                   this,
                   SLOT(onDoubleClickFile(const QModelIndex&)));

  QObject::connect(this->Implementation->Ui.FileName,
                   SIGNAL(textChanged(const QString&)),
                   this,
                   SLOT(onTextEdited(const QString&)));

  QStringList filterList = MakeFilterList(nameFilter);
  if(filterList.empty())
    {
    this->Implementation->Ui.FileType->addItem("All Files (*)");
    this->Implementation->Filters << "All Files (*)";
    }
  else
    {
    this->Implementation->Ui.FileType->addItems(filterList);
    this->Implementation->Filters = filterList;
    }
  this->onFilterChange(this->Implementation->Ui.FileType->currentText());

  QString startPath = startDirectory;
  if(startPath.isEmpty())
    {
    startPath = this->Implementation->getStartPath();
    }
  this->Implementation->addHistory(startPath);
  this->Implementation->setCurrentPath(startPath);
}

//-----------------------------------------------------------------------------
FileDialog::~FileDialog()
{
}


//-----------------------------------------------------------------------------
void FileDialog::onContextMenuRequested(const QPoint &menuPos)
{
  QMenu menu;
  menu.setObjectName("FileDialogContextMenu");

  // Only display new dir option if we're saving, not opening
  if (this->Implementation->Mode == FileDialog::AnyFile)
    {
    QAction *actionNewDir = new QAction("Create New Folder",this);
      QObject::connect(actionNewDir, SIGNAL(triggered()),
          this, SLOT(onCreateNewFolder()));
    menu.addAction(actionNewDir);
    }

  QAction *actionHiddenFiles = new QAction("Show Hidden Files",this);
  actionHiddenFiles->setCheckable( true );
  actionHiddenFiles->setChecked( this->Implementation->FileFilter.getShowHidden());
  QObject::connect(actionHiddenFiles, SIGNAL(triggered(bool)),
    this, SLOT(onShowHiddenFiles(bool)));
  menu.addAction(actionHiddenFiles);

  menu.exec(this->Implementation->Ui.Files->mapToGlobal(menuPos));
}

//-----------------------------------------------------------------------------
void FileDialog::setFileMode(FileDialog::FileMode mode)
{
  //this code is only needed for the 3.10 release as
  //after that the user should know that the dialog support multiple file open
  bool setupMutlipleFileHelp = false;
  this->Implementation->Mode = mode;
  QAbstractItemView::SelectionMode selectionMode;
  switch(this->Implementation->Mode)
    {
    case AnyFile:
    case ExistingFile:
    case Directory:
    default:
      selectionMode=QAbstractItemView::SingleSelection;
      break;
    case ExistingFiles:
      setupMutlipleFileHelp = (this->Implementation->ShowMultipleFileHelp != true);
      selectionMode=QAbstractItemView::ExtendedSelection;
      break;
    }
  if (setupMutlipleFileHelp)
    {
    //only set the tooltip and window title the first time through
    this->Implementation->ShowMultipleFileHelp = true;
    this->setWindowTitle(this->windowTitle() + "  (open multiple files with <ctrl> key.)");
    this->setToolTip("open multiple files with <ctrl> key.");
    }
  this->Implementation->Ui.Files->setSelectionMode(selectionMode);
}

//-----------------------------------------------------------------------------
void FileDialog::setRecentlyUsedExtension(const QString& fileExtension)
{
  if ( fileExtension == QString() )
    {
    // upon the initial use of any kind (e.g., data or screenshot) of dialog
    // 'fileExtension' is equal /set to an empty string.
    // In this case, no any user preferences are considered
    this->Implementation->Ui.FileType->setCurrentIndex(0);
    }
  else
    {
    int index = this->Implementation->Ui.FileType->findText(fileExtension,
      Qt::MatchContains);
    // just in case the provided extension is not in the combobox list
    index = (index == -1) ? 0 : index;
    this->Implementation->Ui.FileType->setCurrentIndex(index);
    }
}

//-----------------------------------------------------------------------------
void FileDialog::addToFilesSelected(const QStringList& files)
{
  // Ensure that we are hidden before broadcasting the selection,
  // so we don't get caught by screen-captures
  this->setVisible(false);
  this->Implementation->SelectedFiles.append(files);
  }

//-----------------------------------------------------------------------------
void FileDialog::emitFilesSelectionDone( )
{
  emit filesSelected(this->Implementation->SelectedFiles);
  if (this->Implementation->Mode != this->ExistingFiles
    && this->Implementation->SelectedFiles.size() > 0)
    {
    emit filesSelected(this->Implementation->SelectedFiles[0]);
    }
  this->done(QDialog::Accepted);
}

//-----------------------------------------------------------------------------
QList<QStringList> FileDialog::getAllSelectedFiles()
{
  return this->Implementation->SelectedFiles;
}

//-----------------------------------------------------------------------------
QStringList FileDialog::getSelectedFiles(int index)
{
  if ( index < 0 || index >= this->Implementation->SelectedFiles.size())
    {
    return QStringList();
    }
  return this->Implementation->SelectedFiles[index];
}

//-----------------------------------------------------------------------------
void FileDialog::accept()
{
  switch(this->Implementation->Mode)
  {
  case AnyFile:
  case Directory:
    this->acceptDefault();
    break;
  case ExistingFiles:
  case ExistingFile:
    this->acceptExistingFiles();
    break;
  }
}

class AcceptAccumulator: public QObject
{
  Q_OBJECT
public:
  AcceptAccumulator(int expectedCount)
  : m_expectedCount(expectedCount)
  {};

public slots:
  void accumulate(bool accept) {
    m_expectedCount--;
    m_accept |= accept;

    if (m_expectedCount == 0)
      emit finished(m_accept);
  }

signals:
  void finished(bool accept);

private:
  int m_expectedCount;
  bool m_accept;

};

class FileAccumulator: public QObject
{
  Q_OBJECT
public:
  FileAccumulator(int expectedCount)
  : m_expectedCount(expectedCount) {};
  QStringList files() { return m_files; };


public slots:
  void accumulate(const QString &path) {
    m_expectedCount--;
    m_files << path;

    if (m_expectedCount == 0)
      emit finished();
  }

signals:
  void finished();

private:
  int m_expectedCount;
  QStringList m_files;

};


class AcceptRequest: public QObject
{
  Q_OBJECT
public:
  AcceptRequest(FileDialog *dialog, const QStringList selectedFiles, bool doubleClicked)
    : m_dialog(dialog), m_selectedFiles(selectedFiles), m_doubleClicked(doubleClicked)
  {}

public slots:
  void accept() {

    // Connect up cleanup slot
    connect(this, SIGNAL(finished(bool)),
        this, SLOT(cleanup()));

    if(m_selectedFiles.empty()) {
      emit finished(false);
      return;
    }

    QString file = m_selectedFiles[0];

    // User chose an existing directory
    m_dialog->Implementation->Model->dirExists(file, this,
        SLOT(onDirExists(const QString &dir, bool exits)));
  }

  void onDirExists(const QString &path, bool exists) {

    if (exists) {
      acceptExistingDirectory(path);
    }
    else {
      acceptFile(path);
    }
  }

  void acceptExistingDirectory(const QString &file) {
    switch(m_dialog->Implementation->Mode)
      {
      case FileDialog::FileMode::Directory:
        if (!m_doubleClicked)
          {
          m_dialog->addToFilesSelected(QStringList(file));
          m_dialog->onNavigate(file);
          emit finished(true);
          return;
          }
      case FileDialog::FileMode::ExistingFile:
      case FileDialog::FileMode::ExistingFiles:
      case FileDialog::FileMode::AnyFile:
        m_dialog->onNavigate(file);
        m_dialog->Implementation->Ui.FileName->clear();
        break;
      }

    emit finished(false);
  }

  void acceptFile(const QString &file) {
    if (m_dialog->Implementation->Mode == FileDialog::FileMode::AnyFile) {
     // If mode is a "save" dialog, we fix the extension first.
     QString fixedFile = m_dialog->fixFileExtension(file,
       m_dialog->Implementation->Ui.FileType->currentText());

     // It is very possible that after fixing the extension,
     // the new filename is an already present directory,
     // hence we handle that case:
     m_dialog->Implementation->Model->dirExists(fixedFile, this,
         SLOT(acceptOnDirExists(const QString &dir, bool exits)));
    }
    else {
      m_dialog->Implementation->Model->fileExists(m_selectedFiles[0], this,
         SLOT(onFileExistsComplete));
    }
  }

  void acceptOnDirExists(const QString &dir, bool exists) {
    if (exists) {
      m_dialog->onNavigate(dir);
      m_dialog->Implementation->Ui.FileName->clear();
      emit finished(false);
    }
    else {
      m_dialog->Implementation->Model->fileExists(m_selectedFiles[0], this,
          SLOT(onFileExistsComplete));
    }
  }

  void onFileExistsComplete(const QString &file, bool exists) {
    if (exists) {
      switch(m_dialog->Implementation->Mode)
      {
      case FileDialog::FileMode::Directory:
        // User chose a file in directory mode, do nothing
        m_dialog->Implementation->Ui.FileName->clear();
        break;
      case FileDialog::FileMode::ExistingFile:
      case FileDialog::FileMode::ExistingFiles:
        m_dialog->addToFilesSelected(m_selectedFiles);
        break;
      case FileDialog::FileMode::AnyFile:
        // User chose an existing file, prompt before overwrite
        if(!m_dialog->Implementation->SupressOverwriteWarning)
          {
          if(QMessageBox::No == QMessageBox::warning(
            m_dialog,
            m_dialog->windowTitle(),
            QString(tr("%1 already exists.\nDo you want to replace it?")).arg(file),
            QMessageBox::Yes,
            QMessageBox::No))
            {
              emit finished(false);
              return;
            }
          }
        m_dialog->addToFilesSelected(QStringList(file));
        break;
      }
      emit finished(true);
      return;
    }
    else {
      switch (m_dialog->Implementation->Mode)
        {
      case FileDialog::FileMode::Directory:
      case FileDialog::FileMode::ExistingFile:
      case FileDialog::FileMode::ExistingFiles:
        m_dialog->Implementation->Ui.FileName->selectAll();
        emit finished(false);
        return;

      case FileDialog::FileMode::AnyFile:
        m_dialog->addToFilesSelected(QStringList(file));
        emit finished(true);
        }
    }

    emit finished(false);
  }

  void cleanup()
  {
    AcceptRequest *request = qobject_cast<AcceptRequest *>(this->sender());

    if (request)
      delete request;
  }

signals:
  void finished(bool accept);

private:
  FileDialog *m_dialog;
  QStringList m_selectedFiles;
  bool m_doubleClicked;

};

//-----------------------------------------------------------------------------
void FileDialog::acceptExistingFiles()
{
  QString filename;
  if(this->Implementation->FileNames.size() == 0)
    {
    //when we have nothing selected in the current selection model, we will
    //attempt to use the default way
    this->acceptDefault();
    }

  AcceptAccumulator *accumulator
    = new AcceptAccumulator(this->Implementation->FileNames.size());

  connect(accumulator, SIGNAL(finished(bool)), this,
      SIGNAL(onAccumulatorFinished(bool)));

  foreach(filename,this->Implementation->FileNames)
    {
    filename = filename.trimmed();
    this->Implementation->Model->absoluteFilePath(filename, this,
        SIGNAL(fileAccepted(const QString&)));

    QStringList files;
    files << filename;

    AcceptRequest *request = new AcceptRequest(this, files,false);
    connect(request, SIGNAL(finished(bool)), accumulator, SLOT(accumulate(bool)));

    request->accept();
    }
}

void FileDialog::onAccumulatorFinished(bool accept)
{
  AcceptAccumulator *accumulator = qobject_cast<AcceptAccumulator*>(sender());
  if (accumulator)
    delete accumulator;

  acceptRequestFinished(accept);
}

void FileDialog::acceptRequestFinished(bool accept)
{
  if (accept)
    emit this->emitFilesSelectionDone();
}


//-----------------------------------------------------------------------------
void FileDialog::acceptDefault()
{
  QString filename = this->Implementation->Ui.FileName->text();
  filename = filename.trimmed();

  this->Implementation->Model->absoluteFilePath(filename,
      this, SLOT(acceptDefaultContinued(const QString &)));
}

void FileDialog::acceptDefaultContinued(const QString &fullFilePath)
{
  emit this->fileAccepted(fullFilePath);

  QStringList files = QStringList(fullFilePath);

  AcceptRequest *request = new AcceptRequest(this, files, false);

  connect(request, SIGNAL(finished(bool)), this,
      SLOT(acceptRequestFinished(bool)));

  request->accept();
}

//-----------------------------------------------------------------------------
void FileDialog::onModelReset()
{
  this->Implementation->Ui.Parents->clear();

  QString currentPath = this->Implementation->Model->getCurrentPath();
  //clean the path to always look like a unix path
  currentPath = QDir::cleanPath( currentPath );

  //the separator is always the unix separator
  QChar separator = '/';

  QStringList parents = currentPath.split(separator, QString::SkipEmptyParts);

  // put our root back in
  if(parents.count())
    {
    int idx = currentPath.indexOf(parents[0]);
    if(idx != 0 && idx != -1)
      {
      parents.prepend(currentPath.left(idx));
      }
    }
  else
    {
    parents.prepend(separator);
    }

  for(int i = 0; i != parents.size(); ++i)
    {
    QString str;
    for(int j=0; j<=i; j++)
      {
      str += parents[j];
      if(!str.endsWith(separator))
        {
        str += separator;
        }
      }
    this->Implementation->Ui.Parents->addItem(str);
    }
   this->Implementation->Ui.Parents->setCurrentIndex(parents.size() - 1);

}

//-----------------------------------------------------------------------------
void FileDialog::onNavigate(const QString& Path)
{
  this->Implementation->addHistory(this->Implementation->Model->getCurrentPath());
  this->Implementation->setCurrentPath(Path);
}

//-----------------------------------------------------------------------------
void FileDialog::onNavigateUp()
{
  this->Implementation->addHistory(this->Implementation->Model->getCurrentPath());
  QFileInfo info(this->Implementation->Model->getCurrentPath());
  this->Implementation->setCurrentPath(info.path());
}

//-----------------------------------------------------------------------------
void FileDialog::onNavigateDown(const QModelIndex& idx)
{
  if(!this->Implementation->Model->isDir(idx))
    return;

  const QStringList paths = this->Implementation->Model->getFilePaths(idx);

  if(1 != paths.size())
    return;

  this->Implementation->addHistory(this->Implementation->Model->getCurrentPath());
  this->Implementation->setCurrentPath(paths[0]);
}

//-----------------------------------------------------------------------------
void FileDialog::onNavigateBack()
{
  QString path = this->Implementation->backHistory();
  this->Implementation->setCurrentPath(path);
}

//-----------------------------------------------------------------------------
void FileDialog::onNavigateForward()
{
  QString path = this->Implementation->forwardHistory();
  this->Implementation->setCurrentPath(path);
}

//-----------------------------------------------------------------------------
void FileDialog::onFilterChange(const QString& filter)
{
  // set filter on proxy
  this->Implementation->FileFilter.setFilter(filter);

  // update view
  this->Implementation->FileFilter.clear();
}

//-----------------------------------------------------------------------------
void FileDialog::onDoubleClickFile(const QModelIndex& index)
{
  if ( this->Implementation->Mode == Directory)
    {
    QModelIndex actual_index = index;
    if(actual_index.model() == &this->Implementation->FileFilter)
      actual_index = this->Implementation->FileFilter.mapToSource(actual_index);

    QStringList selected_files;
    QStringList paths;
    QString path;

    paths = this->Implementation->Model->getFilePaths(actual_index);

    FileAccumulator *accumulator = new FileAccumulator(paths.size());
    connect(accumulator, SIGNAL(finished()), this,
        SLOT(onDoubleClickFileContinue()));

    foreach(path, paths)
      {
        this->Implementation->Model->absoluteFilePath(path, accumulator,
            SLOT(FileAccumulator::accumulate(const QString&)));
      }
    }
  else
    {
    this->accept();
    }
}

// TODO should be a slot
void FileDialog::onDoubleClickFileContinue()
{
  FileAccumulator *accumulator = qobject_cast<FileAccumulator*>(sender());
  if (!accumulator)
    return;

  AcceptRequest * request = new AcceptRequest(this, accumulator->files(), true);
  request->accept();

  delete accumulator;
}

void FileDialog::cleanUpAccumulator()
{
  FileAccumulator *accumulator = qobject_cast<FileAccumulator*>(sender());
  if (accumulator)
    delete accumulator;
}

//-----------------------------------------------------------------------------
void FileDialog::onShowHiddenFiles( const bool &hidden )
{
  this->Implementation->FileFilter.setShowHidden(hidden);
}


//-----------------------------------------------------------------------------
void FileDialog::setShowHidden( const bool &hidden )
{
  this->onShowHiddenFiles(hidden);
}

//-----------------------------------------------------------------------------
bool FileDialog::getShowHidden()
{
  return this->Implementation->FileFilter.getShowHidden();
}

//-----------------------------------------------------------------------------
void FileDialog::onTextEdited(const QString &str)
{
  //really important to block signals so that the clearSelection
  //doesn't cause a signal to be fired that calls fileSelectionChanged
  this->Implementation->Ui.Files->blockSignals(true);
  this->Implementation->Ui.Files->clearSelection();
  if (str.size() > 0 )
    {
    //convert the typed information to be this->Implementation->FileNames
    this->Implementation->FileNames =
      str.split(this->Implementation->FileNamesSeperator,QString::SkipEmptyParts);
    }
  else
    {
    this->Implementation->FileNames.clear();
    }
  this->Implementation->Ui.Files->blockSignals(false);
}

//-----------------------------------------------------------------------------
QString FileDialog::fixFileExtension(
  const QString& filename, const QString& filter)
{
  // Add missing extension if necessary
  QFileInfo fileInfo(filename);
  QString ext = fileInfo.completeSuffix();
  QString extensionWildcard = GetWildCardsFromFilter(filter).first();
  QString wantedExtension =
    extensionWildcard.mid(extensionWildcard.indexOf('.')+1);


  if (!ext.isEmpty())
    {
    // Ensure that the extension the user added is indeed of one the supported
    // types. (BUG #7634).
    QStringList wildCards;
    foreach (QString curfilter, this->Implementation->Filters)
      {
      wildCards += ::GetWildCardsFromFilter(curfilter);
      }
    bool pass = false;
    foreach (QString wildcard, wildCards)
      {
      if (wildcard.indexOf('.') != -1)
        {
        // we only need to validate the extension, not the filename.
        wildcard = QString("*.%1").arg(wildcard.mid(wildcard.indexOf('.')+1));
        QRegExp regEx = QRegExp(wildcard, Qt::CaseInsensitive, QRegExp::Wildcard);
        if (regEx.exactMatch(fileInfo.fileName()))
          {
          pass = true;
          break;
          }
        }
      else
        {
        // we have a filter which does not specify any rule for the extension.
        // In that case, just assume the extension matched.
        pass = true;
        break;
        }
      }
    if (!pass)
      {
      // force adding of the wantedExtension.
      ext = QString();
      }
    }

  QString fixedFilename = filename;
  if(ext.isEmpty() && !wantedExtension.isEmpty() &&
    wantedExtension != "*")
    {
    if(fixedFilename.at(fixedFilename.size() - 1) != '.')
      {
      fixedFilename += ".";
      }
    fixedFilename += wantedExtension;
    }
  return fixedFilename;
}

//-----------------------------------------------------------------------------
void FileDialog::fileSelectionChanged()
{
  // Selection changed, update the FileName entry box
  // to reflect the current selection.
  QString fileString;
  const QModelIndexList indices =
    this->Implementation->Ui.Files->selectionModel()->selectedIndexes();
  if(indices.isEmpty())
    {
    // do not change the FileName text if no selections
    return;
    }
  QStringList fileNames;

  QString name;
  for(int i = 0; i != indices.size(); ++i)
    {
    QModelIndex index = indices[i];
    if(index.column() != 0)
      {
      continue;
      }
    if(index.model() == &this->Implementation->FileFilter)
      {
      name = this->Implementation->FileFilter.data(index).toString();
      fileString += name;
      if ( i != indices.size()-1 )
        {
        fileString += this->Implementation->FileNamesSeperator;
        }
      fileNames.append(name);
      }
    }
  //if we are in directory mode we have to enable / disable the OK button
  //based on if the user has selected a file.
  if ( this->Implementation->Mode == FileDialog::Directory &&
    indices[0].model() == &this->Implementation->FileFilter)
    {
    QModelIndex idx = this->Implementation->FileFilter.mapToSource(indices[0]);
    bool enabled = this->Implementation->Model->isDir(idx);
    this->Implementation->Ui.OK->setEnabled( enabled );
    if ( enabled )
      {
      this->Implementation->Ui.FileName->setText(fileString);
      }
    else
      {
      this->Implementation->Ui.FileName->clear();
      }
    return;
    }

  //user is currently editing a name, don't change the text
  this->Implementation->Ui.FileName->blockSignals(true);
  this->Implementation->Ui.FileName->setText(fileString);
  this->Implementation->Ui.FileName->blockSignals(false);

  this->Implementation->FileNames = fileNames;
}

//-----------------------------------------------------------------------------
bool FileDialog::selectFile(const QString& f)
{
  // We don't use QFileInfo here since it messes the paths up if the client and
  // the server are heterogeneous systems.
  std::string unix_path = f.toAscii().data();
  vtksys::SystemTools::ConvertToUnixSlashes(unix_path);

  std::string filename, dirname;
  std::string::size_type slashPos = unix_path.rfind("/");
  if (slashPos != std::string::npos)
    {
    filename = unix_path.substr(slashPos+1);
    dirname = unix_path.substr(0, slashPos);
    }
  else
    {
    filename = unix_path;
    dirname = "";
    }

  QPointer<QDialog> diag = this;
  this->Implementation->Model->setCurrentPath(dirname.c_str());
  this->Implementation->Ui.FileName->setText(filename.c_str());
  this->Implementation->SupressOverwriteWarning = true;
  this->accept();
  if(diag && diag->result() != QDialog::Accepted)
    {
    return false;
    }
  return true;
}

//-----------------------------------------------------------------------------
void FileDialog::showEvent(QShowEvent *_showEvent )
{
  QDialog::showEvent(_showEvent);
  //Qt sets the default keyboard focus to the last item in the tab order
  //which is determined by the creation order. This means that we have
  //to explicitly state that the line edit has the focus on showing no
  //matter the tab order
  this->Implementation->Ui.FileName->setFocus(Qt::OtherFocusReason);
}

#include "filedialog.moc"
