/*=========================================================================

  Program:   ParaView
  Module:    vtkPVFileInformation.h

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkPVFileInformation - Information object that can
// be used to obtain information about a file/directory.
// .SECTION Description
// vtkPVFileInformation can be used to collect information about file
// or directory. vtkPVFileInformation can collect information
// from a vtkPVFileInformationHelper object alone.
// .SECTION See Also
// vtkPVFileInformationHelper

#ifndef __vtkPVFileInformation_h
#define __vtkPVFileInformation_h

class vtkPVFileInformation
{
public:


  // Description:
  // Transfer information about a single object into this object.
  // The object must be a vtkPVFileInformationHelper.
//  virtual void CopyFromObject(vtkObject* object);

  //BTX
  // Description:
  // Manage a serialized version of the information.
//  virtual void CopyToStream(vtkClientServerStream*);
//  virtual void CopyFromStream(const vtkClientServerStream*);

  enum FileTypes
    {
    INVALID=0,
    SINGLE_FILE,
    SINGLE_FILE_LINK,
    DIRECTORY,
    DIRECTORY_LINK,
    FILE_GROUP,
    DRIVE,
    NETWORK_ROOT,
    NETWORK_DOMAIN,
    NETWORK_SERVER,
    NETWORK_SHARE
    };

  // Description:
  // Helper that returns whether a FileType is a
  // directory (DIRECTORY, DRIVE, NETWORK_ROOT, etc...)
  // Or in other words, a type that we can do a DirectoryListing on.
  static bool IsDirectory(int t);

};

#endif
