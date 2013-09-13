;/*=========================================================================

   Program: ParaView
   Module:    FileDialogModel.cxx

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

#include "filedialogmodel.h"

#include <algorithm>

#include <QtCore/QObject>
#include <QStyle>
#include <QDir>
#include <QApplication>
#include <QMessageBox>


#include <vtkDirectory.h>
#include <vtkSmartPointer.h>

#include <google/protobuf/stubs/common.h>
#include <protocall/runtime/vtkcommunicatorchannel.h>
#include "RemoteFileSystemService.pb.h"

#include "vtkPVFileInformation.h"

using namespace ProtoCall::Runtime;
using namespace google::protobuf;


//////////////////////////////////////////////////////////////////////
// FileDialogModelFileInfo

class FileDialogModelFileInfo
{
public:
  FileDialogModelFileInfo():
    Type(vtkPVFileInformation::INVALID),
    Hidden(false)
  {
  }

  FileDialogModelFileInfo(const QString& l, const QString& filepath,
           vtkPVFileInformation::FileTypes t, const bool &h,
           const QList<FileDialogModelFileInfo>& g =
           QList<FileDialogModelFileInfo>()) :
    Label(l),
    FilePath(filepath),
    Type(t),
    Hidden(h),
    Group(g)
  {
  }

  const QString& label() const
  {
    return this->Label;
  }

  const QString& filePath() const
  {
    return this->FilePath;
  }

  vtkPVFileInformation::FileTypes type() const
  {
    return this->Type;
  }

  bool isGroup() const
  {
    return !this->Group.empty();
  }

  bool isHidden() const
  {
    return this->Hidden;
  }

  const QList<FileDialogModelFileInfo>& group() const
  {
    return this->Group;
  }

private:
  QString Label;
  QString FilePath;
  vtkPVFileInformation::FileTypes Type;
  bool Hidden;
  QList<FileDialogModelFileInfo> Group;
};

/////////////////////////////////////////////////////////////////////
// Icons

FileDialogModelIconProvider::FileDialogModelIconProvider()
{
  QStyle* style = QApplication::style();
  this->FolderLinkIcon = style->standardIcon(QStyle::SP_DirLinkIcon);
  this->FileLinkIcon = style->standardIcon(QStyle::SP_FileLinkIcon);
  this->DomainIcon.addPixmap(QPixmap(":/pqCore/Icons/pqDomain16.png"));
  this->NetworkIcon.addPixmap(QPixmap(":/pqCore/Icons/pqNetwork16.png"));
}

QIcon FileDialogModelIconProvider::icon(IconType t) const
{
  switch(t)
    {
    case Computer:
      return QFileIconProvider::icon(QFileIconProvider::Computer);
    case Drive:
      return QFileIconProvider::icon(QFileIconProvider::Drive);
    case Folder:
      return QFileIconProvider::icon(QFileIconProvider::Folder);
    case File:
      return QFileIconProvider::icon(QFileIconProvider::File);
    case FolderLink:
      return this->FolderLinkIcon;
    case FileLink:
      return this->FileLinkIcon;
    case NetworkFolder:
      return QFileIconProvider::icon(QFileIconProvider::Network);
    case NetworkRoot:
      return this->NetworkIcon;
    case NetworkDomain:
      return this->DomainIcon;
    }
  return QIcon();
}

QIcon FileDialogModelIconProvider::icon(vtkPVFileInformation::FileTypes f) const
{
  if(f == vtkPVFileInformation::DIRECTORY_LINK)
    {
    return icon(FileDialogModelIconProvider::FolderLink);
    }
  else if(f == vtkPVFileInformation::SINGLE_FILE_LINK)
    {
    return icon(FileDialogModelIconProvider::FileLink);
    }
  else if(f == vtkPVFileInformation::NETWORK_SHARE)
    {
    return icon(FileDialogModelIconProvider::NetworkFolder);
    }
  else if(f == vtkPVFileInformation::NETWORK_SERVER)
    {
    return icon(FileDialogModelIconProvider::Computer);
    }
  else if(f == vtkPVFileInformation::NETWORK_DOMAIN)
    {
    return icon(FileDialogModelIconProvider::NetworkDomain);
    }
  else if(f == vtkPVFileInformation::NETWORK_ROOT)
    {
    return icon(FileDialogModelIconProvider::NetworkRoot);
    }
  else if(f == vtkPVFileInformation::DIRECTORY)
    {
    return icon(FileDialogModelIconProvider::Folder);
    }
  return icon(FileDialogModelIconProvider::File);
}
QIcon FileDialogModelIconProvider::icon(const QFileInfo& info) const
{
  return QFileIconProvider::icon(info);
}
QIcon FileDialogModelIconProvider::icon(QFileIconProvider::IconType ico) const
{
  return QFileIconProvider::icon(ico);
}

Q_GLOBAL_STATIC(FileDialogModelIconProvider, Icons);

namespace {

///////////////////////////////////////////////////////////////////////
// CaseInsensitiveSort

bool CaseInsensitiveSort(const FileDialogModelFileInfo& A, const
  FileDialogModelFileInfo& B)
{
  // Sort alphabetically (but case-insensitively)
  return A.label().toLower() < B.label().toLower();
}

} // namespace

/////////////////////////////////////////////////////////////////////////
// FileDialogModel::Implementation

class FileDialogModel::pqImplementation
{
public:
  pqImplementation(FileDialogModel *model, ProtoCall::Runtime::vtkCommunicatorChannel *server) :
    m_model(model),
    m_separator(0),
    m_server(server)
  {
    RemoteFileSystemService::Proxy proxy(NULL);

    // Get the separator
    Separator *separator = new Separator();
    Closure *callback = NewCallback(this,
        &FileDialogModel::pqImplementation::handleSeparatorResponse,
        separator);

    proxy.separator(separator, callback);

    listCurrentWorkingDir();
  }

  ~pqImplementation()
  {
  }

  void listCurrentWorkingDir() {
    Path  dir;
    listDirectory(dir);
  }

  void listDirectory(const QString &path) {
    // Get the listing of the current working directory
    Listing *listing = new Listing();
    Path dir;
    dir.set_path(path.toStdString());

    listDirectory(dir);
  }

  void listDirectory(Path &dir) {

    RemoteFileSystemService::Proxy proxy(NULL);

    // Get the listing of the current working directory
    Listing *listing = new Listing();

    Closure *callback = NewCallback(this,
        &FileDialogModel::pqImplementation::handleListingResponse,
        listing);

    proxy.ls(&dir, listing, callback);
  }

  void handleSeparatorResponse(Separator *response)
  {
    this->m_separator = response->separator()[0];

    delete response;
  }

  void handleListingResponse(Listing *response)
  {
    this->CurrentPath = response->path().path().c_str();
    this->FileList.clear();

    QList<FileDialogModelFileInfo> dirs;
    QList<FileDialogModelFileInfo> files;

    for(int i=0; i< response->paths_size(); i++) {
      const Path &path = response->paths(i);

      const QString label = QString::fromStdString(path.path());

      FileDialogModelFileInfo info(label, label, static_cast<vtkPVFileInformation::FileTypes>(path.type()), false);

      if (path.type() == vtkPVFileInformation::DIRECTORY) {
        dirs.push_back(info);
      }
      else {
        files.push_back(info);
      }
    }

    qSort(dirs.begin(), dirs.end(), CaseInsensitiveSort);
    qSort(files.begin(), files.end(), CaseInsensitiveSort);

    for(int i = 0; i != dirs.size(); ++i)
      {
      this->FileList.push_back(dirs[i]);
      }
    for(int i = 0; i != files.size(); ++i)
      {
      this->FileList.push_back(files[i]);
      }

    m_model->reset();

    delete response;
  }

  /// Removes multiple-slashes, ".", and ".." from the given path string,
  /// and points slashes in the correct direction for the server
  const QString cleanPath(const QString& Path)
  {
    QString result = QDir::cleanPath(QDir::fromNativeSeparators(Path));
    return result.trimmed();
  }

  QStringList getFilePaths(const QModelIndex& Index)
    {
    QStringList results;

    QModelIndex p = Index.parent();
    if(p.isValid())
      {
      if(p.row() < this->FileList.size())
        {
        FileDialogModelFileInfo& file = this->FileList[p.row()];
        const QList<FileDialogModelFileInfo>& grp = file.group();
        if(Index.row() < grp.size())
          {
          results.push_back(grp[Index.row()].filePath());
          }
        }
      }
    else if(Index.row() < this->FileList.size())
      {
      FileDialogModelFileInfo& file = this->FileList[Index.row()];
      if (file.isGroup() && file.group().count()>0)
        {
        for (int i=0; i<file.group().count();i++)
          {
          results.push_back(file.group().at(i).filePath());
          }
        }
      else
        {
        results.push_back(file.filePath());
        }
      }

    return results;
    }

  bool isHidden(const QModelIndex& idx)
    {
    const FileDialogModelFileInfo* info = this->infoForIndex(idx);
    return info? info->isHidden() : false;
    }

  bool isDir(const QModelIndex& idx)
    {
    const FileDialogModelFileInfo* info = this->infoForIndex(idx);
    return info? vtkPVFileInformation::IsDirectory(info->type()) : false;
    }

  bool isRemote()
    {
    return this->m_server;
    }

  ProtoCall::Runtime::vtkCommunicatorChannel* getServer()
    {
      return this->m_server;
    }

  /// Path separator for the connected server's filesystem.
  char m_separator;

  /// Current path being displayed (server's filesystem).
  QString CurrentPath;
  /// Caches information about the set of files within the current path.
  QVector<FileDialogModelFileInfo> FileList;  // adjacent memory occupation for QModelIndex

  const FileDialogModelFileInfo* infoForIndex(const QModelIndex& idx) const
    {
    if(idx.isValid() &&
       NULL == idx.internalPointer() &&
       idx.row() >= 0 &&
       idx.row() < this->FileList.size())
      {
      return &this->FileList[idx.row()];
      }
    else if(idx.isValid() && idx.internalPointer())
      {
      FileDialogModelFileInfo* ptr = reinterpret_cast<FileDialogModelFileInfo*>(idx.internalPointer());
      const QList<FileDialogModelFileInfo>& grp = ptr->group();
      if(idx.row() >= 0 && idx.row() < grp.size())
        {
        return &grp[idx.row()];;
        }
      }
    return NULL;
    }

private:
  // server vs. local implementation private
  ProtoCall::Runtime::vtkCommunicatorChannel* m_server;
  FileDialogModel *m_model;
};

//////////////////////////////////////////////////////////////////////////
// FileDialogModel
FileDialogModel::FileDialogModel(
  ProtoCall::Runtime::vtkCommunicatorChannel* _server, QObject* Parent) :
  base(Parent),
  Implementation(new pqImplementation(this, _server))
{
}

FileDialogModel::~FileDialogModel()
{
  delete this->Implementation;
}

ProtoCall::Runtime::vtkCommunicatorChannel* FileDialogModel::server() const
{
  return this->Implementation->getServer();
}

void FileDialogModel::setCurrentPath(const QString& path)
{
  this->Implementation->listDirectory(path);
}

QString FileDialogModel::getCurrentPath()
{
  return this->Implementation->CurrentPath;
}



class AbsoluteFilePathRequest : public QObject
{
  Q_OBJECT
public:
  AbsoluteFilePathRequest(vtkCommunicatorChannel *server,
      const QString& path)
  : m_path(path), m_server(server)
  {
  }

  void handleAbsolutePathResponse(Path *response) {
    emit complete(QString::fromStdString(response->path()));
    delete response;
  }

 void execute() {

   if(m_path.isEmpty())
   {
      emit complete(QString());
   }

   RemoteFileSystemService::Proxy proxy(m_server);
   Path absolutePathRequest;
   absolutePathRequest.set_path(m_path.toStdString());
   Path *response = new Path();

   Closure *callback = NewCallback(this,
         &AbsoluteFilePathRequest::handleAbsolutePathResponse,
         response);

   proxy.absolutePath(&absolutePathRequest, response, callback);
 }

signals:
  void complete(const QString &path);

private:
  QString m_path;
  vtkCommunicatorChannel *m_server;
};



void FileDialogModel::absoluteFilePath(const QString& path,
    const QObject *requester, const char* resultSlot)
{

  AbsoluteFilePathRequest *request = new AbsoluteFilePathRequest(server(), path);

  connect(request, SIGNAL(AbsoluteFilePathRequest::complete(const QString &)),
    requester, resultSlot);
  connect(request, SIGNAL(AbsoluteFilePathRequest::complete(const QString &)),
      this, SLOT(cleanup()));

  request->execute();
}

QStringList FileDialogModel::getFilePaths(const QModelIndex& Index)
{
  if(Index.model() == this)
    {
    return this->Implementation->getFilePaths(Index);
    }
  return QStringList();
}

bool FileDialogModel::isHidden( const QModelIndex&  Index)
{
  if(Index.model() == this)
    return this->Implementation->isHidden(Index);

  return false;
}

bool FileDialogModel::isDir(const QModelIndex& Index)
{
  if(Index.model() == this)
    return this->Implementation->isDir(Index);

  return false;
}

void FileDialogModel::cleanup()
{
  delete sender();
}

class FileExistsRequest : public QObject
{
  Q_OBJECT
public:
  FileExistsRequest(vtkCommunicatorChannel *server,
      const QString& path)
  : m_path(path), m_server(server)
  {
  }

  void handleFileExists(Listing *listing)
  {
    if (listing->path().type() == vtkPVFileInformation::SINGLE_FILE) {
      emit complete(QString::fromStdString(listing->path().path()), true);
    }
    else if (!QString::fromStdString(listing->path().path()).endsWith(".lnk")) {
      execute(QString::fromStdString(listing->path().path() + ".lnk"));
    }
    else {
      emit complete(QString::fromStdString(listing->path().path()), false);
    }

    delete listing;
  }

 void execute() {
   execute(m_path);
 }

signals:
  void complete(const QString &path, bool exists);

private:
  QString m_path;
  vtkCommunicatorChannel *m_server;

  void execute(const QString &filePath) {
    if(filePath.isEmpty())
    {
       emit complete(QString(), false);
    }

    RemoteFileSystemService::Proxy proxy(m_server);
    Path path;
    path.set_path(filePath.toStdString());

    Listing *listing = new Listing();
    Closure *callback = NewCallback(this,
        &FileExistsRequest::handleFileExists,
        listing);

    proxy.ls(&path, listing, callback);
  }
};

void FileDialogModel::fileExists(const QString& file, const QObject *requester,
    const char *resultSlot)
{
  QString filePath = this->Implementation->cleanPath(file);

  FileExistsRequest *request = new FileExistsRequest(server(), file);

  connect(request, SIGNAL(FileExistsRequest::complete(const QString &, bool)),
    requester, resultSlot);
  connect(request, SIGNAL(AbsoluteFilePathRequest::complete(const QString &)),
      this, SLOT(cleanup()));

  request->execute();
}

class DirExistsRequest : public QObject
{
  Q_OBJECT
public:
  DirExistsRequest(vtkCommunicatorChannel *server,
      const QString& path)
  : m_path(path), m_server(server)
  {
  }

  void handleDirExists(Listing *listing)
  {
    if (vtkPVFileInformation::IsDirectory(listing->path().type())) {
      emit complete(QString::fromStdString(listing->path().path()), true);
    }
    else if (!QString::fromStdString(listing->path().path()).endsWith(".lnk")){
      execute(QString::fromStdString(listing->path().path() + ".lnk"));
    }
    else {
      emit complete(QString::fromStdString(listing->path().path()), false);
    }

    delete listing;
  }


  void execute() {
    execute(m_path);
  }

signals:
  void complete(const QString &path, bool exists);

private:
  QString m_path;
  vtkCommunicatorChannel *m_server;

  void execute(const QString &dirPath) {
    if(dirPath.isEmpty())
    {
       emit complete(QString(), false);
    }

    RemoteFileSystemService::Proxy proxy(m_server);
    Path path;
    path.set_path(dirPath.toStdString());

    Listing *listing = new Listing();
    Closure *callback = NewCallback(this,
        &DirExistsRequest::handleDirExists,
        listing);

    proxy.ls(&path, listing, callback);
  }
};

void FileDialogModel::dirExists(const QString& path, const QObject *requester,
    const char *resultSlot)
{
  QString dirPath = this->Implementation->cleanPath(path);

  DirExistsRequest *request = new DirExistsRequest(server(), dirPath);

  connect(request, SIGNAL(DirExistsRequest::complete(const QString &, bool)),
    requester, resultSlot);
  connect(request, SIGNAL(DirFilePathRequest::complete(const QString &)),
      this, SLOT(cleanup()));

  request->execute();
}

QChar FileDialogModel::separator() const
{
  return this->Implementation->m_separator;
}

int FileDialogModel::columnCount(const QModelIndex& idx) const
{
  const FileDialogModelFileInfo* file =
    this->Implementation->infoForIndex(idx);

  if(!file)
    {
    // should never get here anyway
    return 1;
    }

  return file->group().size() + 1;
}

QVariant FileDialogModel::data(const QModelIndex &idx, int role) const
{

  const FileDialogModelFileInfo *file;

  if(idx.column() == 0)
    {
    file = this->Implementation->infoForIndex(idx);
    }
  else
    {
    file = this->Implementation->infoForIndex(idx.parent());
    }

  if(!file)
    {
    // should never get here anyway
    return QVariant();
    }

  if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
    if (idx.column() == 0)
      {
      return file->label();
      }
    else if (idx.column() <= file->group().size())
      {
      return file->group().at(idx.column()-1).label();
      }
    }
  else if(role == Qt::UserRole)
    {
    if (idx.column() == 0)
      {
      return file->filePath();
      }
    else if (idx.column() <= file->group().size())
      {
      return file->group().at(idx.column()-1).filePath();
      }
    }
  else if(role == Qt::DecorationRole && idx.column() == 0)
    {
    return Icons()->icon(file->type());
    }

  return QVariant();
}

QModelIndex FileDialogModel::index(int row, int column,
                                     const QModelIndex& p) const
{
  if(!p.isValid())
    {
    return this->createIndex(row, column);
    }
  if(p.row() >= 0 &&
     p.row() < this->Implementation->FileList.size() &&
     NULL == p.internalPointer())
    {
    FileDialogModelFileInfo* fi = &this->Implementation->FileList[p.row()];
    return this->createIndex(row, column, fi);
    }

  return QModelIndex();
}

QModelIndex FileDialogModel::parent(const QModelIndex& idx) const
{
  if(!idx.isValid() || !idx.internalPointer())
    {
    return QModelIndex();
    }

  const FileDialogModelFileInfo* ptr = reinterpret_cast<FileDialogModelFileInfo*>(idx.internalPointer());
  int row = ptr - &this->Implementation->FileList.first();
  return this->createIndex(row, idx.column());
}

int FileDialogModel::rowCount(const QModelIndex& idx) const
{
  if(!idx.isValid())
    {
    return this->Implementation->FileList.size();
    }

  if(NULL == idx.internalPointer() &&
     idx.row() >= 0 &&
     idx.row() < this->Implementation->FileList.size())
    {
    return this->Implementation->FileList[idx.row()].group().size();
    }

  return 0;
}

bool FileDialogModel::hasChildren(const QModelIndex& idx) const
{
  if(!idx.isValid())
    return true;

  if(NULL == idx.internalPointer() &&
     idx.row() >= 0 &&
     idx.row() < this->Implementation->FileList.size())
    {
    return this->Implementation->FileList[idx.row()].isGroup();
    }

  return false;
}

QVariant FileDialogModel::headerData(int section,
                                       Qt::Orientation, int role) const
{
  switch(role)
    {
  case Qt::DisplayRole:
    switch(section)
      {
    case 0:
      return tr("Filename");
      }
    }

  return QVariant();
}

bool FileDialogModel::setData(const QModelIndex& idx, const QVariant& value, int role)
{
  //if(role != Qt::DisplayRole && role != Qt::EditRole)
  //  {
    return false;
  //  }

//  const FileDialogModelFileInfo* file =
//    this->Implementation->infoForIndex(idx);
//
//  if(!file)
//    {
//    return false;
//    }
//
//  QString name = value.toString();
//  return this->rename(file->filePath(), name);
}

Qt::ItemFlags FileDialogModel::flags(const QModelIndex& idx) const
{
  Qt::ItemFlags ret = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  const FileDialogModelFileInfo* file =
    this->Implementation->infoForIndex(idx);
  if(file && !file->isGroup())
    {
    ret |= Qt::ItemIsEditable;
    }
  return ret;
}

#include "filedialogmodel.moc"
