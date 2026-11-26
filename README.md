# 极达五笔(JiDaTSF) —— 极点五笔 TSF 移植

把极点五笔的操作习惯搬到 Windows TSF（Text Services Framework）框架，支持 Per-Monitor V2 高分辨率（高分屏 / 多屏不同缩放）显示。

- 输入法类型：TSF 文本服务（DLL，COM 注册）
- 目标系统：Windows 10 1607+ / Windows 11
- 目标平台：x64 / x86
- 语言：C++17
- 构建工具：Visual Studio 2022 + Windows SDK 10

---

## 一、特性

### 输入行为

| 功能 | 说明 |
|---|---|
| 86 版五笔码表 | 支持一级 / 二级 / 三级 / 四级 / 词组 |
| 空格首选 | 空格上屏第一条候选 |
| 数字选词 | 1–9 选择对应候选 |
| 分号选第二重码 | 编码非空时按 `;` 上屏第 2 条候选 |
| 分号日期 | `;2024.5.20;` → `2024年5月20日` |
| 分号金额 | `;123.45;` → `￥123.45` |
| 标点顶字上屏 | 有编码时输入标点，先上屏首选再上屏标点 |
| 四码唯一自动上屏 | 可配置关闭 |
| 四码后继续输入 | 首选自动顶出，新字母开新组合，不丢字 |
| Z 键重复 | 空编码按 `Z` 重复上次上屏内容 |
| Z 键通配 | 编码内 `Z` 展开为 a–y 逐一匹配 |
| 回车编码上屏 | 有编码时回车把编码原文上屏 |
| 翻页 | `-` / `=` 或 PageUp / PageDown |
| 鼠标选词 | 候选窗口点击 / 滚轮翻页 |
| Shift + 字母 | 直接输出大写英文字母，走系统输入 |
| 逐键提示 | 前缀扩展，短码优先 |

### 显示

- Per-Monitor V2 DPI 感知（候选窗口随光标所在显示器缩放）
- 候选窗口跟随光标，自动避开屏幕边缘（在下方放不下时向上弹出）
- 候选窗口不抢焦点（`WS_EX_NOACTIVATE`）
- 候选窗口随宿主窗口焦点切换自动隐藏

---

## 二、安装 / 卸载

### 安装

```powershell
# 需要管理员权限
powershell -ExecutionPolicy Bypass -File scripts\register.ps1
```

`register.ps1` 会：

1. 调用 `regsvr32 /s` 注册 DLL
2. 递归复制 `data\` 下的词库文件，其中包括 `86海峰词库\freeime.mb` 和可选的 `attach.mb`

注册完成后：

1. 打开「设置 → 时间和语言 → 语言和区域 → 中文（简体）→ 选项 → 键盘 → 添加键盘」
2. 找到「极达五笔」并添加
3. 在任务栏语言栏切换到该输入法

### 卸载

```powershell
# 需要管理员权限
powershell -ExecutionPolicy Bypass -File scripts\unregister.ps1
```

---

## 三、码表

### 运行时词库

```
JiDaTSF/bin/x64/Release/data/86海峰词库/freeime.mb
```

输入法在激活时直接读取极点 `.mb` 二进制词库，不再运行时解析文本词库。将主词库放在上述路径；同目录下的 `attach.mb` 存在时也会一并读取。更新词库后重新运行安装脚本以复制新文件。

### 完整极点词库

1. 从极点五笔或兼容词库工具导出 `.mb` 主词库
2. 将其保存为 `data\86海峰词库\freeime.mb`；需要附加词库时同时放入 `attach.mb`
3. 重新运行安装脚本并重启输入法

文本导出文件可用于编辑或迁移，但不会被输入法作为运行时词库加载。

---

## 四、已实现的极点习惯对照表

| 极点习惯 | 实现位置 | 说明 |
|---|---|---|
| 空格选首选 | `JiDaEngine::OnSpace` | |
| 数字键选词 | `JiDaEngine::OnDigit` | 1–9 |
| `;` 选第 2 重码 | `JiDaEngine::OnSemicolon` | 编码非空时 |
| `;` 日期 | `JiDaEngine::OnSemicolonTail` | `;YYYY.M.D;` |
| `;` 金额 | `JiDaEngine::OnSemicolonTail` | `;金额;` |
| 标点顶字上屏 | `JiDaEngine::OnPunctuation` | 分两步提交 |
| 四码唯一自动上屏 | `JiDaConfig::autoCommitUnique` | 可关闭 |
| 四码后继续输入 | `EngineAction::TopCommitAndContinue` | 顶字不丢字 |
| Z 重复 | `JiDaEngine::OnLetter` | 空编码 Z |
| Z 通配 | `JiDaEngine::Query` | 展开 a–y |
| 回车编码上屏 | `JiDaEngine::OnEnter` | |
| `-` / `=` 翻页 | `JiDaTextService::OnKeyDown` | 也支持 PageUp/PageDown |
| 鼠标选词 | `CCandidateWindow::_OnLButtonUp` | 回调 `OnPick` |
| Shift 临时英文 | `JiDaTextService::OnKeyDown` | 交给系统 |
| 逐键提示 | `JiDaEngine::Query` | 前缀扩展，短码优先 |
| Per-Monitor V2 | `DpiHelper::EnablePerMonitorV2` | 清单 + 运行时 |
| 候选窗口跨屏 DPI | `CCandidateWindow::WM_DPICHANGED` | |

---

## 五、DPI / 高分辨率

工程通过三处协同实现高分辨率正确显示：

1. **进程清单**：`JiDaTSF.manifest` 声明 `PerMonitorV2`
2. **运行时**：`DllMain` 调用 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
3. **候选窗口**：
   - `Show()` 前通过 `MonitorFromRect(caret)` 获取光标所在显示器
   - `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` 得到该显示器 DPI
   - 字体、行高、内边距都按 DPI 重新计算
   - 响应 `WM_DPICHANGED` 动态重建字体

在 100% / 125% / 150% / 200% 多屏混合缩放的场景下，候选窗口会随光标所在显示器正确缩放。

---

## 六、配置

配置位于 `Globals.h` 中的 `JiDaConfig` 结构体：

```cpp
struct JiDaConfig {
  bool zRepeat = true;                             // Z 重复上次上屏
  bool zWildcard = true;                           // Z 通配
  bool semicolonSecond = true;                     // 分号选第二重码
  bool punctuationTop = true;                      // 标点顶屏
  bool perKeyHint = true;                          // 逐键提示
  bool gbkExt = true;                              // GBK 生僻字
  wchar_t wildcardChar = L'z';                     // 通配符字符
  bool inlinePreedit = false;                      // 嵌入预编辑
  bool showStatusBar = true;                       // 是否显示状态栏
  int candPageSize = 9;                            // 候选框中每页的条目数
  int candFontSize = 14;                           // 候选框中的字体大小
  CandLayout candLayout = CandLayout::Horizontal;  // 候选框选项排列方式
};
```

当前版本通过修改源码 + 重新编译生效。后续可在 `%APPDATA%\JiDaTSF\config.ini` 中持久化。

---

## 七、已知限制

- **词库**：默认只带示例词条，需自行导入完整极点词库
- **简繁转换**：未实现
- **皮肤 / 透明度**：未实现
- **用户词频学习**：未实现（留了插入点）
- **全半角 / CapsLock 状态联动**：未实现
- **竖排候选**：当前只有横排
- **候选数量**：固定每页 9 个
- **TSF 注册**：`Register.cpp` 只注册了键盘类 TIP 和 UIElementEnabled 类目，如需语言栏图标、输入法设置界面需要扩展
- **x86 构建**：工程已配好，但默认脚本只构建 x64

---

## 八、开发说明

### 输入处理流程

```
按键
  ↓
CJiDaTextService::OnKeyDown
  ↓
JiDaEngine::OnXxx  →  返回 EngineAction
  ↓
CJiDaTextService::_HandleLetterResult
  ↓
CStartCompositionSession   （首次按键）
CUpdateCompositionSession  （后续按键）
CCommitSession             （上屏）
  ↓
CCandidateWindow::Show
```

### 关键文件职责

- **`JiDaEngine`**：纯输入逻辑，不依赖 TSF，可单元测试
- **`EditSessions.h`**：所有 TSF 编辑操作都通过 `ITfEditSession` 完成
- **`CJiDaTextService`**：按键路由 + 协调引擎与 TSF
- **`CCandidateWindow`**：独立的弹出窗口，不做编辑

### 扩展点

| 需求 | 扩展位置 |
|---|---|
| 用户词频 | `JiDaEngine::Query` 排序后 + `_CommitAndEnd` 回写 |
| 简繁转换 | `_CommitAndEnd` 提交前做字符映射 |
| 皮肤 | `CCandidateWindow::_OnPaint` 自绘 + `SetLayeredWindowAttributes` |
| 竖排候选 | `CCandidateWindow::_UpdateSize` + `_OnPaint` 布局 |
| 配置持久化 | 在 `DllMain` 中读取 `%APPDATA%\JiDaTSF\config.ini` 覆盖 `g_config` |
| 更多分号规则 | `JiDaEngine::OnSemicolonTail` 中增加分支 |

---

## 九、调试

### 查看注册状态

```powershell
# 查看 CLSID 是否注册
Get-ItemProperty "HKLM:\SOFTWARE\Classes\CLSID\{7A3E8F1C-4B2D-4E5F-9C8A-1B6D3E7F2A5C}"

# 查看语言 profile（需要遍历，建议用 TSF 官方工具）
```
