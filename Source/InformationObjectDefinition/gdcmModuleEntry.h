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
#ifndef __gdcmModuleEntry_h
#define __gdcmModuleEntry_h

#include "gdcmTypes.h"
#include "gdcmType.h"

#include <string>

namespace gdcm
{
/**
 * \brief Class for representing a ModuleEntry
 * \note bla
 * \sa DictEntry
 */
class GDCM_EXPORT ModuleEntry
{
public:
  ModuleEntry(const char *name, const char *type, const char *description):Name(name)/*,Type(type)*/,DescriptionField(description) {
	  DataElementType = Type::GetTypeType(type);
  }
  friend std::ostream& operator<<(std::ostream& _os, const ModuleEntry &_val);

  const char *GetName() const { return Name.c_str(); }

  const Type &GetType() const { return DataElementType; }

  typedef std::string Description;
  const Description & GetDescription() const { return DescriptionField; }

private:
  // PS 3.3 repeats the name of an attribute, but often contains typos
  // for now we will not use this info, but instead access the DataDict instead
  std::string Name;

  // An attribute, encoded as a Data Element, may or may not be required in a 
  // Data Set, depending on that Attribute's Data Element Type.
  Type DataElementType;

  // TODO: for now contains the raw description (with enumerated values, defined terms...)
  Description DescriptionField;
};
//-----------------------------------------------------------------------------
inline std::ostream& operator<<(std::ostream& _os, const ModuleEntry &_val)
{
  _os << _val.DataElementType << "\t" << _val.DescriptionField;
  return _os;
}


} // end namespace gdcm

#endif //__gdcmModuleEntry_h
