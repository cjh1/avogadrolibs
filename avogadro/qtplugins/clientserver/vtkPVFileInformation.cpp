/*=========================================================================

  Program:   ParaView
  Module:    vtkPVFileInformation.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkPVFileInformation.h"

#include "vtkCollection.h"
#include "vtkCollectionIterator.h"

#include "vtkSmartPointer.h"

#if defined(_WIN32)
# define _WIN32_IE 0x0400  // special folder support
# define _WIN32_WINNT 0x0400  // shared folder support
# include <windows.h>   // FindFirstFile, FindNextFile, FindClose, ...
# include <direct.h>    // _getcwd
# include <shlobj.h>    // SHGetFolderPath
# include <sys/stat.h>  // stat
# include <string.h>   // for strcasecmp
# define vtkPVServerFileListingGetCWD _getcwd
#else
# include <sys/types.h> // DIR, struct dirent, struct stat
# include <sys/stat.h>  // stat
# include <dirent.h>    // opendir, readdir, closedir
# include <unistd.h>    // access, getcwd
# include <errno.h>     // errno
# include <string.h>    // strerror
# include <stdlib.h>    // getenv
# define vtkPVServerFileListingGetCWD getcwd
#endif
#if defined (__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <vector>
#endif

#include <vtksys/SystemTools.hxx>
#include <vtksys/RegularExpression.hxx>
#include <set>
#include <string>

bool vtkPVFileInformation::IsDirectory(int t)
{
  return t == DIRECTORY || t == DIRECTORY_LINK ||
         t == DRIVE || t == NETWORK_ROOT ||
         t == NETWORK_DOMAIN || t == NETWORK_SERVER ||
         t == NETWORK_SHARE;
}
