#pragma once
// 文本简繁转换与半角转全角接口。
#include <string>

// toTraditional：简体 → 繁体
// toFullWidth  ：ASCII 半角 → 全角（汉字不受影响）
std::wstring ConvertText(const std::wstring& src,
                         bool toTraditional,
                         bool toFullWidth);