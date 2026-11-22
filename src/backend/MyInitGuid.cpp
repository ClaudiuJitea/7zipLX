// Define the COM-style IIDs used by the reused 7-Zip code exactly once.
#include "Common/MyInitGuid.h"

#include "7zip/IProgress.h"
#include "7zip/IPassword.h"
#include "7zip/IStream.h"
#include "7zip/ICoder.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/UI/Common/ArchiveOpenCallback.h"
#include "7zip/UI/Common/IFileExtractCallback.h"
#include "7zip/UI/Common/Update.h"
#include "7zip/UI/FileManager/IFolder.h"
#include "7zip/UI/Agent/IFolderArchive.h"
#include "7zip/UI/Agent/Agent.h"
