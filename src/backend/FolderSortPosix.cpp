// Natural file-name ordering used by CAgentFolder. This is the same algorithm
// as FileManager/PanelSort.cpp, kept here so the GUI does not need Panel.cpp.
#include "Common/MyString.h"

int CompareFileNames_ForFolderList(const wchar_t *s1, const wchar_t *s2)
{
  for (;;)
  {
    wchar_t c1 = *s1;
    wchar_t c2 = *s2;
    if ((c1 >= L'0' && c1 <= L'9') && (c2 >= L'0' && c2 <= L'9'))
    {
      for (; *s1 == L'0'; ++s1) {}
      for (; *s2 == L'0'; ++s2) {}
      size_t len1 = 0;
      size_t len2 = 0;
      for (; s1[len1] >= L'0' && s1[len1] <= L'9'; ++len1) {}
      for (; s2[len2] >= L'0' && s2[len2] <= L'9'; ++len2) {}
      if (len1 < len2) return -1;
      if (len1 > len2) return 1;
      for (; len1 > 0; ++s1, ++s2, --len1)
      {
        if (*s1 == *s2) continue;
        return (*s1 < *s2) ? -1 : 1;
      }
      c1 = *s1;
      c2 = *s2;
    }
    ++s1;
    ++s2;
    if (c1 != c2)
    {
      const wchar_t u1 = MyCharUpper(c1);
      const wchar_t u2 = MyCharUpper(c2);
      if (u1 < u2) return -1;
      if (u1 > u2) return 1;
    }
    if (c1 == 0) return 0;
  }
}
