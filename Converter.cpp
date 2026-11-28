#include "Converter.h"
// 文本转换实现：使用系统接口转换繁简字，并手动映射半角到全角。
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

// 手动半角→全角映射，不依赖 locale：
//   0x20 (空格)  → 0x3000 (全角空格)
//   0x21 ~ 0x7E  → +0xFEE0 即为对应全角字符
static std::wstring ToFullWidthManual(const std::wstring& src) {
  std::wstring out;
  out.reserve(src.size());
  for (wchar_t c : src) {
    if (c == L' ') {
      out += (wchar_t)0x3000;
    } else if (c >= 0x21 && c <= 0x7E) {
      out += (wchar_t)(c + 0xFEE0);
    } else {
      out += c;
    }
  }
  return out;
}

std::wstring ConvertText(const std::wstring& src,
                         bool toTraditional,
                         bool toFullWidth) {
  std::wstring s = src;
  if (toTraditional)
    s = LCMap(s, LCMAP_TRADITIONAL_CHINESE);
  if (toFullWidth)
    s = ToFullWidthManual(s);
  return s;
}