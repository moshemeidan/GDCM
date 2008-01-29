/*=========================================================================

  Program: GDCM (Grass Root DICOM). A DICOM library
  Module:  $URL$

  Copyright (c) 2006-2008 Mathieu Malaterre
  All rights reserved.
  See Copyright.txt or http://gdcm.sourceforge.net/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "gdcmXMLDictReader.h"

int TestXMLDictReader(int argc, char *argv[])
{
  const char *filename = argv[1];
  gdcm::XMLDictReader tr;
  tr.SetFilename(filename);
  tr.Read();

  std::cout << tr.GetDict() << std::endl;

  return 0;
}