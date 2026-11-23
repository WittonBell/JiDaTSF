#include "Converter.h"
#include <windows.h>

static std::wstring LCMap(const std::wstring& src, DWORD flags) {
  if (src.empty())
    return src;
  int need = LCMapStringEx(L"zh-CN", flags, src.c_str(), (int)src.size(),
                           nullptr, 0, nullptr, nullptr, 0);
  if (need <= 0)
    return src;
  std::wstring out(need, L'\0');
  int got = LCMapStringEx(L"zh-CN", flags, src.c_str(), (int)src.size(),
                          &out[0], need, nullptr, nullptr, 0);
  if (got <= 0)
    return src;
  out.resize(got);
  return out;
}

std::wstring ConvertText(const std::wstring& src,
                         bool toTraditional,
                         bool toFullWidth) {
  std::wstring s = src;
  if (toTraditional)
    s = LCMap(s, LCMAP_TRADITIONAL_CHINESE);
  if (toFullWidth)
    s = LCMap(s, LCMAP_FULLWIDTH);
  return s;
}