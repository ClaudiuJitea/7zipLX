// POSIX adapter for the small part of ZipRegistry used by WorkDir.cpp.
// The real UI settings live in SettingsStore (Qt/QSettings); 7-Zip only needs
// a temp-directory policy when AgentOut rewrites an archive.
#include "7zip/UI/Common/ZipRegistry.h"

namespace NWorkDir {

void CInfo::Save() const
{
  // The Qt settings layer owns persistence. Nothing to do here.
}

void CInfo::Load()
{
  SetDefault();
}

} // namespace NWorkDir
