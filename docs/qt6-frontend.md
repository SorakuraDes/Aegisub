# Aegisub Qt6 frontend (`aegisub-qt`)

本文件记录在 `feature` 分支上并行开发 Qt6/QML 前端的架构、构建方式、已完成进度与已知限制，便于随时交接。

> 原则：**wxWidgets 原版构建保持可用**（双轨制），Qt 前端是新入口 `aegisub-qt`，不修改 `src/` 下原版 wx 界面文件与 `libaegisub/` 核心逻辑；新增 Qt 代码只放在 `src/qt_ui/`。

## 1. 当前进度总览

| 阶段 | 提交 | 内容 |
|---|---|---|
| 0 | — | 本地构建基线：VS18 + meson + ninja，`build/` 可编译原版 `aegisub.exe` |
| 1 | `12b2f16b4` | Qt 前端骨架：`aegisub-qt` 目标、QML 窗口、qrc、`aegisub_core`、ViewModels 基础 |
| 2 | `0a6a5cd61` | `AGENTS.md` 项目规则 |
| 3 | `b6095b638` | 桥接 `AssFile`：`PropertiesViewModel` 读写 `[Script Info]`，`dumpAss()` 打印 |
| 4 | `101661e54` | `SubtitleGridModel`（`QAbstractListModel`，O(1) 索引）+ 只读字幕网格 |
| 5 | 本次工作区改动（未提交） | 行选中 `activeRow`、`EditBoxViewModel` 双向编辑、`EditBox.qml`，网格实时刷新；本文件 |

## 2. 目录结构

```
src/qt_ui/
├── main.cpp                  # 入口：初始化 core → 建 AssFile → 建 VM → 注入 QML 上下文
├── aegisub_core.{h,cpp}      # libaegisub 子系统初始化（dispatch/log/Path/Options）、FlushOptions
├── meson.build               # aegisub-qt 目标；ass_qt_core 静态库（ASS 数据模型，需 wx PCH）
├── resources.qrc             # 全部 QML 注册到 qrc:/
├── Main.qml                  # 主窗口：顶部按钮 + 字幕网格 + 底部 EditBox
├── SubtitleGrid.qml          # 表格视图（ListView + RowLayout delegate、选中高亮）
├── EditBox.qml               # 底部编辑框：开始/结束时间 + 大字幕文本框
├── AboutWindow.qml           # 关于窗口（版本/版权/译员，来自 po/*.mo）
├── PreferencesWindow.qml     # 首选项（Backup 页，App/Auto/Save）
├── PropertiesWindow.qml      # 脚本配置（Title / PlayResX / PlayResY + 打印 ASS）
└── viewmodels/
    ├── app_info_vm.{h,cpp}          # 版本号（src/version.cpp + git_version.h）
    ├── about_vm.{h,cpp}             # 关于窗口数据
    ├── translation_service.{h,cpp}  # i18n：读 build/po/<lang>/LC_MESSAGES/aegisub.mo
    ├── preferences_vm.{h,cpp}       # agi::Options 读写（OPT_GET/OPT_SET）
    ├── properties_vm.{h,cpp}        # AssFile::Get/SetScriptInfo + dumpAss()
    ├── subtitle_grid_model.{h,cpp}  # QAbstractListModel + activeRow + notifyRowUpdated
    └── edit_box_vm.{h,cpp}          # 选中行 text/startTime/endTime 双向编辑
```

## 3. 架构

```
QML 视图 (Window/ListView/TextField)
      │  Q_PROPERTY / Q_INVOKABLE / 信号
      ▼
ViewModel (QObject)  ──►  Aegisub 应用层数据模型
      │                     AssFile / AssDialogue / AssStyle / AssInfo
      ▼
aegisub_core ──► libaegisub（agi::Path / agi::Options / agi::Time / agi::dispatch / agi::log）
                     ▲
                     └── qrc + build/po/*.mo（翻译）、default_config*.json（默认配置）
```

- **`aegisub_core`**：无 wx 参与的 Qt 进程需要自己启动 `agi::dispatch::Init`、`agi::log`、`agi::Path`、`agi::Options`（wx 版在 `src/main.cpp` 里做这些）。默认配置 JSON 由 meson 拷到 exe 旁；`config.json` 默认写 `%APPDATA%\Aegisub\config.json`，不可写时回退到 exe 旁 `.qt_user/`。
- **`ass_qt_core` 静态库**：`ass_file/ass_entry/ass_dialogue/ass_style/ass_attachment/ass_style_storage/ass_override/utils/compat` 这批 `src/` 应用层源，需要 wx PCH（`src/include/agi_pre.h`），因此单独编库并 `link_with`。
- **数据模型**：`AssFile::Events` 是 `boost::intrusive::list`，`SubtitleGridModel` 构造时建 `std::vector<AssDialogue*>` 索引实现 O(1) 行访问；`refresh()` 可重建索引。
- **编辑闭环**：`SubtitleGrid` 点击行 → `gridModel.activeRow = index` → `EditBoxViewModel` 重发属性信号 → `EditBox.qml` 加载该行；编辑文本/时间 → `EditBoxViewModel` 直接改 `AssDialogue` → `gridModel.notifyRowUpdated(row)`（`dataChanged`）→ 网格该行实时刷新。**本阶段不接入 `AssFile::Commit`/Undo**。

## 4. 构建与运行

一次性环境（已装好，位于 `.build-tools/`，被 gitignore）：

- Qt 6.6.3 `win64_msvc2019_64`：`.build-tools/qt6/6.6.3/msvc2019_64`（aqtinstall）
- gettext（`msgfmt` 等）、moonscript：`.build-tools/gettext`、`.build-tools/moonscript`
- Python 3.15 + meson 1.12.1 + ninja；VS 18（MSVC 19.52）

命令（均在仓库根目录）：

```bat
.build-tools\reconfigure.bat        :: meson setup --reconfigure build（注入 Qt 路径/VS 环境）
.build-tools\build.bat               :: 全量构建（wx + qt）
.build-tools\build.bat aegisub-qt    :: 只编 Qt 前端（产物 build/src/qt_ui/aegisub-qt.exe）
.build-tools\build.bat aegisub       :: 只编 wx 版
```

`build.bat` 内部会：`vcvars64.bat` → `VSLANG=1033` → `PYTHONUTF8=0` → `ninja -C build`。**裸终端直接 `ninja -C build` 会因 cl 不在 PATH 失败**，需在 VS 开发者环境里执行。

运行 Qt 前端需要有 Qt DLL 的 PATH；exe 目录已有一份 windeployqt 部署的 Qt（含补齐的 `QtQuick/Layouts`）。

调试用环境变量：

| 变量 | 作用 |
|---|---|
| `AEGISUB_QT_LANGUAGE=zh_CN` | 强制翻译 catalog（默认跟随系统区域；本机 zh-HK 无对应 po，会回退英文） |
| `AEGISUB_QT_CONFIG_DIR=<dir>` | 覆盖 `?user` 目录（避免污染真实 `%APPDATA%\Aegisub`，也用于沙箱环境） |
| `AEGISUB_QT_TRACE=1` | 把启动阶段写入 exe 旁 `aegisub_qt_core.log` |
| `AEGISUB_PO_DIR=<dir>` | 覆盖 `.mo` 搜索根（默认相对 exe 的 `../../po`） |

## 5. 关键实现细节 / 已踩的坑

1. **Python 3.15 默认 UTF-8 模式** 让 meson 用 UTF-8 解码 `cl.exe` 的 GBK 输出而崩溃 → 所有 meson 调用用 `python -X utf8=0 -m mesonbuild.mesonmain`，并给 ninja 触发的 regenerate 设 `PYTHONUTF8=0`。
2. **`agnostic` 头文件顺序**：`libaegisub/signal.h` 有成员名 `slots`，Qt 的 `#define slots public` 会破坏解析 → 含 agi 头的 `.cpp` 必须把 `ass_file.h` / `options.h` 放在任何 Qt 头之前；`Q_SIGNALS` 可用以免被宏影响。
3. **`libaegisub/option_value.h` 没有 include guard**：`src/options.h` 已包含它，不能再显式 include 一次。
4. **`AssFile::AssFile()` 是空构造**（此 fork），默认 `[Script Info]`/默认样式来自 `LoadDefault()`。
5. **`agi::fs::CreateDirectory`（boost::filesystem）在本机/沙箱会 fail-fast**（0xC0000409）→ 建目录改用 `QDir::mkpath`。
6. **`agi::Time`**：字符串构造宽松解析（`0:00:02.40`），格式化用 `GetAssFormatted()`；比较用 `int(Time)`（厘秒）。
7. **QML**：`TextArea` 没有 `textEdited` 信号（只有 `TextField` 有），用 `onTextChanged` + 值比较；从 C++ 刷新输入框用 `Connections` 监听 `activeRowChanged`，避免与用户光标打架。
8. **qrc 隐式组件**不可靠，跨文件窗口/组件统一用 `Loader { source: "qrc:/X.qml" }` 或 `Qt.createComponent`。
9. **自动化验证**：UI Automation 的 `TogglePattern` 不会触发 QML `toggled`（Qt 无障碍路径怪癖），真实交互要用鼠标点击；`TextArea` 需要 `ValuePattern.SetValue` 或真实键盘输入。
10. **部署的 Qt 依赖**：exe 旁 windeployqt 目录若缺 `QtQuick/Layouts` 会导致 “module QtQuick.Layouts is not installed”，需从 Qt 安装目录补齐 `qmldir`/`qquicklayoutsplugin.dll`/`Qt6QuickLayouts.dll`。

## 6. 已知限制（下一步工作）

- **无 Undo/Commit**：直接改 `AssDialogue`，未调用 `AssFile::Commit`（需要 `agi::Context` 与 wx undo 栈，后续引入）。
- **无文件 IO**：只能编辑 `LoadDefault()` + 5 条测试数据；尚未接 `AssParser`/`SubtitleFormatAss` 打开与保存。
- **网格是只读模型**：增删行、排序、多选、折叠都未实现；`refresh()` 是全量 reset。
- **大文件性能**：`ListView` 已复用 delegate，但尚无分页/滚动加载；`rows_` 索引在增删行后需 `refresh()`。
- **只做了 Backup 一个首选项页**、Properties 只做了 3 个字段。
- **bestsource** 在本机构建中被禁用（子模块 `libp2p` 在 bitbucket 需要认证），视频源只有 ffms2。

## 7. 验证方式

- 编译：`.build-tools\build.bat aegisub-qt`（EXIT 0）、`.build-tools\build.bat`（wx 回归 EXIT 0）。
- 运行验证用 UI Automation + `PrintWindow` 截取窗口（不抓桌面），已确认：
  - About：版本/版权/简体中文译员名单；
  - Preferences：开关写回 `agi::Options` 并落盘 `config.json`；
  - Properties：改 Title/PlayRes 后 `dumpAss()` 输出的 `[Script Info]` 反映修改；
  - Grid/EditBox：点击行 → 底部显示该行时间与文本；编辑文本 → 网格对应行实时更新（截图见 `.build-tools/qt_*_check.png`，不入库）。
