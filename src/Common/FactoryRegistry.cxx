// Copyright (C) 2012 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "Common/FactoryRegistry.hh"
#include "Common/FactoryBase.hh"
#include "Common/CFLog.hh"

//////////////////////////////////////////////////////////////////////////////

using namespace COOLFluiD::Common;

namespace COOLFluiD {
  namespace Common {

//////////////////////////////////////////////////////////////////////////////

FactoryRegistry::FactoryRegistry()
{
}

//////////////////////////////////////////////////////////////////////////////

FactoryRegistry::~FactoryRegistry()
{
}

//////////////////////////////////////////////////////////////////////////////

void FactoryRegistry::regist(FactoryBase* factory)
{
  const std::string type_name = factory->getTypeName();
  if ( ! m_store.checkEntry(type_name) )
  {
    m_store.addEntry(type_name, factory);
#ifdef CF_HAVE_LOG4CPP    
  CFtrace << "Factory [" + type_name + "] registered\n";
#endif
  }
  else
  {
#ifdef CF_HAVE_LOG4CPP    
   CFtrace << "Factory " + factory->getTypeName() + " already registered : skipping registration\n";
#endif
  }
}

//////////////////////////////////////////////////////////////////////////////

void FactoryRegistry::unregist(const std::string& type_name)
{
  if ( m_store.checkEntry(type_name) )
  {
    m_store.removeEntry(type_name);
#ifdef CF_HAVE_LOG4CPP
  CFtrace << "Factory [" + type_name + "] unregistered\n";
#endif
  }
  else
  {
#ifdef CF_HAVE_LOG4CPP
    CFtrace << "Factory [" + type_name + "] not registered : skipping removal\n";
#endif  
  }
}

//////////////////////////////////////////////////////////////////////////////

SafePtr<FactoryBase> FactoryRegistry::getFactory(const std::string& type_name)
{
  // CFtrace << "FactoryRegistry::getFactory(" << type_name << ") => start\n"; 

  if ( m_store.checkEntry(type_name) )
  {
    //     CFLog ( INFO, "Factory [" + type_name + "] found and returning\n" );
    return m_store.getEntry(type_name);
  }
  else
  {
    CFLogWarn("Factory [" + type_name + "] not registered : returning null pointer\n");
    return SafePtr<FactoryBase>(CFNULL);
  }
  // CFtrace << "FactoryRegistry::getFactory(" << type_name << ") => end\n";
}

//////////////////////////////////////////////////////////////////////////////

  } // namespace Common 
} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////
