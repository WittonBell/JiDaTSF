#pragma once
// 配置文件路径及全局配置的加载、保存接口。
#include <string>

std::wstring GetJiDaConfigDir();
std::wstring GetJiDaConfigPath();
void LoadJiDaConfig();
void SaveJiDaConfig();