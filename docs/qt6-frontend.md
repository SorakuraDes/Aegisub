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
| 5 | `785db8cbd` | 行选中 `activeRow`、`EditBoxViewModel` 双向编辑、`EditBox.qml`，网格实时刷新；本文件 |
| 6 | `0c6882011` | 真实 `.ass` 读写（`AssParser` + `AssSubtitleFormat`，方案 B shim）、`DocumentViewModel`、菜单栏 + `FileDialog`、脏标记/标题、`modelReset` 同步 |
| 7 | `acdcb2065` | `AssFile::Commit` 统一提交总线 + 轻量 `UndoManager`（快照栈、合并提交、`isModified`）+ 脏标记改由撤销位置推导 + Edit 菜单 / `Ctrl+Z`·`Ctrl+Y` + 未保存确认框；新增 `commit_bridge`（隔离 wxString） |
| 8A | `b5278c711` | 网格行操作 `insertBefore`/`insertAfter`/`duplicateRow`/`deleteRow`（走 `COMMIT_DIAG_ADDREM`，原生 Undo/Redo）+ 网格右键上下文菜单 + `Subtitle` 菜单 4 项 + 焦点隔离快捷键（`Delete`/`Insert`/`Ctrl+D`） |
| 8B | `8061f69d5` | EditBox 补齐核心字段（`Comment`/`Layer`/`Style`/`Actor`/`Effect`/`Duration`）+ `availableStyles` + 时长联动 + Enter 键盘流（`Enter` 提交并下一行、`Shift+Enter` 插 `\N`，由 C++ 键过滤器实现）；`EditBox.qml` 重排为两行顶栏 + 正文区 |
| 9A | `04e26bb7c` | 网格多选（`selected_rows_`/`anchor`/`isSelected` role/`selectionCount`、`selectSingle`/`toggleSelect`/`selectRange`/`selectAll`）+ 批量行操作与批量元数据（单次 Commit、一次撤销全部）+ `SearchViewModel`/`SearchDialog.qml` 查找替换（正则/字段/选区范围、`Replace All` 单次提交） |
| 9B | 本次工作区改动（未提交） | **M1 终章**：样式管理器（`StylesViewModel` + `StylesWindow.qml`：增删复制/改名联动字幕行、字体字号颜色对齐边距、实时预览）+ 主窗口竖直 `SplitView` + 紧凑菜单栏（28px + 快捷键提示列，`ShortcutMenuItem.qml`）+ 24px 底部状态栏（行数/选区/编码/版本） |

> **M1 里程碑（Stage 0–9B）**：Qt6/QML 前端已具备与 wx 版并行的日常编辑能力——真实 `.ass` 读写、统一提交/撤销总线、行操作与多选批量、EditBox 全字段与键盘流、查找替换、样式管理器，以及桌面级工作区（紧凑菜单/可拖拽分区/状态栏）。原版 wx 构建同步保持可用（双轨制）。

## 2. 目录结构

```
src/qt_ui/
├── main.cpp                  # 入口：初始化 core → 建 AssFile → 建 VM → 可选命令行文件 → 注入 QML
├── aegisub_core.{h,cpp}      # libaegisub 子系统初始化（dispatch/log/Path/Options）、FlushOptions
├── commit_bridge.{h,cpp}     # wx 隔离层：Commit/AddUndoManager 的 std::string 版（.cpp 编进 ass_qt_core）
├── subtitle_format_shim.cpp  # 方案 B：补 SubtitleFormat 的 ctor/CanReadFile/CanWriteFile/CanSave
├── meson.build               # aegisub-qt 目标；ass_qt_core 静态库（ASS 数据模型 + I/O，需 wx PCH）
├── resources.qrc             # 全部 QML 注册到 qrc:/
├── Main.qml                  # 主窗口：MenuBar + 字幕网格 + 底部 EditBox + 错误提示条 + FileDialog
├── SubtitleGrid.qml          # 表格视图（ListView + RowLayout delegate、选中高亮）
├── EditBox.qml               # 底部编辑框：开始/结束时间 + 大字幕文本框
├── AboutWindow.qml           # 关于窗口（版本/版权/译员，来自 po/*.mo）
├── PreferencesWindow.qml     # 首选项（Backup 页，App/Auto/Save）
├── PropertiesWindow.qml      # 脚本配置（Title / PlayResX / PlayResY + 打印 ASS）
├── SearchDialog.qml          # 非模态查找/替换窗口（Find/Replace 复用同一实例）
├── StylesWindow.qml          # 非模态样式管理器（列表 + 表单 + 实时预览）
├── ShortcutMenuItem.qml      # 紧凑菜单项：右侧显示快捷键提示（Qt Quick MenuItem 不渲染）
└── viewmodels/
    ├── app_info_vm.{h,cpp}          # 版本号（src/version.cpp + git_version.h）
    ├── about_vm.{h,cpp}             # 关于窗口数据
    ├── translation_service.{h,cpp}  # i18n：读 build/po/<lang>/LC_MESSAGES/aegisub.mo
    ├── preferences_vm.{h,cpp}       # agi::Options 读写（OPT_GET/OPT_SET）
    ├── properties_vm.{h,cpp}        # AssFile::Get/SetScriptInfo + dumpAss()
    ├── subtitle_grid_model.{h,cpp}  # QAbstractListModel + activeRow + 行操作（插/复制/删）
    ├── edit_box_vm.{h,cpp}          # 选中行全字段双向编辑（文本/时间/时长/元数据）+ Enter 键盘流
    ├── undo_manager.{h,cpp}         # 快照式 Undo/Redo 栈 + 合并提交 + isModified
    ├── search_vm.{h,cpp}            # 查找/替换（正则、字段、选区范围、单次提交）
    ├── styles_vm.{h,cpp}            # [V4+ Styles] 样式列表/字段编辑、改名联动字幕行
    └── document_vm.{h,cpp}          # 文件路径/脏标记/错误 + 打开保存（AssParser/AssSubtitleFormat）
```

## 3. 架构

```
QML 视图 (Window/MenuBar/ListView/TextField/FileDialog)
      │  Q_PROPERTY / Q_INVOKABLE / 信号
      ▼
ViewModel (QObject)  ──►  Aegisub 应用层数据模型
      │                     AssFile / AssDialogue / AssStyle / AssInfo
      ▼
aegisub_core ──► libaegisub（agi::Path / agi::Options / agi::Time / agi::dispatch / agi::log / agi::charset）
                     ▲
                     └── qrc + build/po/*.mo（翻译）、default_config*.json（默认配置）
```

- **`aegisub_core`**：无 wx 参与的 Qt 进程需要自己启动 `agi::dispatch::Init`、`agi::log`、`agi::Path`、`agi::Options`（wx 版在 `src/main.cpp` 里做这些）。默认配置 JSON 由 meson 拷到 exe 旁；`config.json` 默认写 `%APPDATA%\Aegisub\config.json`，不可写时回退到 exe 旁 `.qt_user/`。
- **`ass_qt_core` 静态库**：`ass_*` 数据模型 + `ass_parser`、`subtitle_format_ass`、`text_file_reader/writer`、`string_codec`、`utils`、`compat` 等应用层源，需要 wx PCH（`src/include/agi_pre.h`），因此单独编库并 `link_with`。
- **数据模型**：`AssFile::Events` 是 `boost::intrusive::list`，`SubtitleGridModel` 构造时建 `std::vector<AssDialogue*>` 索引实现 O(1) 行访问；`refresh()` 可重建索引（非空表默认选中第 0 行，空表 -1，并发出 `modelReset`）。
- **文件 I/O**：`DocumentViewModel` 用原版 `AssParser` 解析、`AssSubtitleFormat::ReadFile/WriteFile` 读写。读取时先解析到临时 `AssFile`，成功才 `swap` 进主文档（坏文件不会破坏当前文档）；编码用 `agi::charset::Detect()` 自动检测（检测为空则回退 UTF-8）。写出统一 UTF-8。
- **方案 B shim**：`AssSubtitleFormat` 的内联 ctor 需要 `SubtitleFormat(std::string)`，基类 vtable 还需要 `CanReadFile/CanWriteFile/CanSave`；这些都在含 wx 对话框与全部兄弟格式的 `subtitle_format.cpp` 里。`src/qt_ui/subtitle_format_shim.cpp` 只提供这几个 trivial 定义且**不链接** `subtitle_format.cpp`，因此 Qt 前端不引入 wx 对话框/其它字幕格式代码。
- **提交总线（Stage 7）**：所有写操作（文本 / 时间 / `[Script Info]`）先改 `AssFile`，然后调用 `AssFile::Commit(desc, type, amend_id, line)`。`UndoManager` 订阅 `PushState` 记快照，`SubtitleGridModel` / `EditBoxViewModel` 订阅 `AnnounceCommit` 刷新——不再有「改完再手动通知某个 model」的胶水代码。
- **wx 隔离（`commit_bridge`）**：`AssFile::Commit` 收 `wxString`、`PushState` 的 `AssFileCommit` 里也带 `wxString`，而 Qt 翻译单元不能 include wx 头（宏/`slots` 冲突）。`commit_bridge.{h,cpp}` 编进 `ass_qt_core`（有 wx PCH），对 Qt 侧只暴露 UTF-8 `std::string` 的 `aegisub_qt::commit()` / `addUndoManager()`。
- **Undo 快照**：`entries_[0]` 是干净基线，`entries_[i>0]` 是第 i 次提交后的文件状态；`pos_` 当前快照、`saved_pos_` 保存点，`isModified = pos_ != saved_pos_`（上限 100 步）。合并提交沿用原版契约：调用方把上次 `Commit` 返回的 id 再传回去，同 id 且栈顶就是当前状态时**覆盖**栈顶快照而不新增步骤；`EditBoxViewModel` / `PropertiesViewModel` 用 1000ms 单次定时器 + 行号/字段类型变化来断开合并，保存后也会断开（见坑 15）。
- **撤销恢复**：`restoreTo()` 复制目标快照 → `AssFile::swap` 换回 → 发 `restored()`（不走 `Commit`，不会二次压栈）。此时所有 `AssDialogue*` 都失效，`SubtitleGridModel` 先 `beginResetModel`/重建 `rows_`，其它监听者（`EditBoxViewModel`、`PropertiesViewModel`、`DocumentViewModel`）再刷新。`AssFile` 拷贝构造函数不拷贝 `ProjectProperties`，快照里手工补（见坑 14）。
- **脏标记与标题**：`DocumentViewModel::dirty` 直接返回 `UndoManager::isModified()`（撤销回保存点会自动变回 false）；标题为 `displayName + (dirty ? " *" : "") + " - Aegisub Qt6"`。`New`/`Open` 成功后 `resetToClean()`，保存成功 `markSaved()`。
- **快捷键**：`Ctrl+Z` / `Ctrl+Y` / `Ctrl+Shift+Z` 由 `main.cpp` 里的应用级事件过滤器接管（`UndoShortcutFilter`）：先吃掉 `ShortcutOverride`（否则 `QQuickTextInput` 会用自己的撤销栈把键抢走、QML `Action.shortcut` 也拿不到），再处理 `KeyPress`。菜单 `Edit` 里另有 `Undo <desc>` / `Redo <desc>` 动态项（`enabled` 绑 `canUndo`/`canRedo`）。
- **未保存保护**：`Main.qml` 的 `requestAction("new"/"open"/"quit")` 在 `documentVM.dirty` 时弹 Material 3 `Dialog`（`Save` / `Discard` / `Cancel`）；窗口 `onClosing` 也走同一入口（`forceClose` 标记让 `Discard` 之后能真正关闭）。`Save` 在未命名时转 `Save As`，保存成功后继续原动作。
- **行操作（Stage 8A）**：`SubtitleGridModel::insertBefore` / `insertAfter` / `duplicateRow` / `deleteRow` 直接改 `AssFile::Events`（`new AssDialogue` / `new AssDialogue(*src)` 深拷贝、`iterator_to` + `insert`、`erase` + `delete`），随后 `Commit(COMMIT_DIAG_ADDREM)`。`Commit` 统一重排 `Row`，网格的 `onFileCommitted` 走 `refreshPreservingActiveRow()` 修索引，最后由操作自己 `setActiveRow` 定位到新行 / 邻行。时间规则对齐 wx 版：插前 = `End` 接当前行 `Start`、最多 `Timing/Default Duration` 且不压前一行；插后 = `Start` 接当前行 `End`、不越过下一行；删最后一行时补一条空默认行（同 `edit/line/delete`）。
- **焦点隔离快捷键**：`Delete` / `Insert` / `Ctrl+D` 用 QML `Shortcut` 实现，`enabled` 绑 `!textInputFocused()`；`EditBox.qml` 的 `TextArea`/`TextField`/`SpinBox`/`ComboBox`/`CheckBox` 带 `isEditBoxField: true` 标记，`Main.qml` 的 `textInputFocused()` 从 `activeFocusItem` **沿 parent 链**找该标记（SpinBox 聚焦时焦点在它内部的 TextInput 上），避免抢文本框自己的 Delete/插入键操作。网格行的右键上下文菜单在 `SubtitleGrid.qml`（右键先选行再 `popup`，菜单项直接作用于 `gridModel.activeRow`）。
- **EditBox 字段与键盘流（Stage 8B）**：`EditBoxViewModel` 现在覆盖 `Comment`/`Layer`/`Style`/`Actor`/`Effect`/`Duration`，全部走 `commitEdit()`（`COMMIT_DIAG_META` / `COMMIT_DIAG_TIME`）；因为元数据字段共用 `COMMIT_DIAG_META`，合并提交额外用 `field_key` 分组，避免 1 秒内改两个字段被并成一步。`Duration` 是派生值（`End - Start`，钳到非负），改它只写 `End`，改 `Start`/`End` 会发 `durationChanged()` 让 QML 刷新派生字段。`availableStyles` 在 `modelReset` / `COMMIT_STYLES` / `restored` 时重建（空表回退 `Default`）。`Enter` = 提交并跳下一行（末行自动 `insertAfter` 建行），`Shift+Enter` = 光标处插入字面 `\N`；这两个键由 `main.cpp` 的 `BodyEnterFilter` 转发（原因见坑 20）。
- **QML 字段回填策略**：EditBox 的输入框**不做属性绑定**，由 `loadSelection()` 显式回填；时间三件套在 `startTimeChanged`/`endTimeChanged`/`durationChanged` 时用 `syncTimeFields()` 只更新**没有焦点**的字段，避免把正在输入的文本按规范化格式覆盖掉（会跳光标）。
- **多选与批量（Stage 9A）**：`SubtitleGridModel` 维护 `active_row_` + `selected_rows_`（`QSet<int>`）+ `anchor_row_`，新增 `isSelected` role、`selectionCount` 属性以及 `selectSingle` / `toggleSelect` / `selectRange` / `selectAll`；QML 按点击修饰键分派（`Shift`=区间、`Ctrl`=切换、普通=单选、`Ctrl+A`=全选，EditBox 有焦点时不许劫持）。`duplicateSelectedRows()` / `deleteSelectedRows()` 先收集 `AssDialogue*` 再一次性插入或「先摘链再 delete」，**单次** `Commit(COMMIT_DIAG_ADDREM)`，所以一次 `Ctrl+Z` 整体撤销；行结构变化后 `refreshPreservingActiveRow()` 调用 `sanitizeSelection()` 把选中集合校准到合法行号并保证至少选中活动行。EditBox 的元数据字段在多选时对 `metadataTargets()`（全部选中行）批量写入，并以 `single_line = nullptr` 单次提交 `COMMIT_DIAG_META`（正文/时间仍只改活动行）；网格对这类「无 single_line 的内容提交」只发全行 `dataChanged`，不做 model reset，这样撤销合并状态与选中集合都不会被打断。
- **查找与替换（Stage 9A）**：`SearchViewModel` 独立持有 `AssFile*` + `SubtitleGridModel*`，按 `searchField`（0=Text、1=Actor、2=Effect、3=Style）取字段；普通模式用 `QString::contains`（可选大小写），正则模式用 `QRegularExpression`（非法正则只写 `statusMessage`，不崩溃）。`findNext`/`findPrev` 从活动行环形查找，命中后 `selectSingle` 并发 `locateRow`（QML 里 `positionViewAtIndex`）。`replaceAll` 遍历全表或 `inSelectionOnly` 时的选中行，累计后**单次** `Commit`，状态行报 `Replaced N occurrence(s) in M line(s)`；`replaceCurrent` 替换活动行后自动跳下一处。搜索窗口是独立的非模态 `Window`，`Edit→Find...`（`Ctrl+F`）/`Replace...`（`Ctrl+H`）复用同一实例并聚焦对应输入框，主窗口关闭时一起关闭。
- **样式管理器（Stage 9B）**：`StylesViewModel` 持有 `AssFile*`，自己维护 `std::vector<AssStyle*>` 索引 + 名称缓存（`refreshStyles()` 只从缓存重建，不 deref 旧指针，因此 `restored()` 里调用是安全的）；所有字段修改先写 `AssStyle`，再 `UpdateData()`（`AssStyle` 缓存了它的 ASS 行）后 `Commit(COMMIT_STYLES)`，1000ms 合并窗口避免每次 SpinBox 都成一步撤销。改名会同时改 `AssFile::Events` 里的 `Style` 与正文中 `{\rOld}` 覆写标签（`AssDialogue::ParseTags`/`UpdateText` + `ProcessParameters`），以 `COMMIT_STYLES | COMMIT_DIAG_META` **单次**提交。`New`/`Copy`/`Delete` 的名字用大小写不敏感去重，且保证至少保留一条样式。`New`/`Open` 不走 `Commit`，所以 `DocumentViewModel::documentReloaded()` 显式通知样式表重建。
- **桌面工作区（Stage 9B）**：内容区是竖直 `SplitView`（网格 `fillHeight`/min 160，EditBox preferred 185/min 150），可拖拽调比例；`MenuBar` 用自定义 delegate 压到 28px、字号 13px，菜单项改用 `ShortcutMenuItem`（平铺 `MenuItem` + `contentItem` 里左右两段文字）显示快捷键提示——Qt Quick 的 `MenuItem` 本身不渲染快捷键列；快捷键因此全部改用 `Shortcut` 元素注册（`Ctrl+Z/Y` 仍由 `main.cpp` 的过滤器处理）。`footer` 是 24px 状态栏（行数 / 选区数 / 编码 / 版本号）。
- **菜单/文件对话框**：`Main.qml` 的 `menuBar` 提供 File（New/Open/Save/Save As/Exit）、Subtitle（Properties）、Tools（Preferences）、Help（About），`QtQuick.Dialogs` 的 `FileDialog` 负责 Open / Save As。
- **命令行**：`aegisub-qt.exe [file.ass]`，参数存在且是文件则启动即打开，否则 `newDocument()`。

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

**本机 DSH 沙箱里的替代构建脚本**：在这个环境的 bash 里 `vcvars64.bat` 会失败（cmd 的 `for /F` 把整条命令当成一个 token，MSVC/SDK 环境根本没设起来），所以用 `.build-tools/build_ninja.sh`：

```bash
bash .build-tools/build_ninja.sh                    # 全量（wx + qt）
bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe   # 只编 Qt
```

它手工拼 MSVC 14.52 / Windows SDK 10.0.26100 / Qt 6.6.3 / Python / gettext 的 `PATH`、`INCLUDE`、`LIB`（外加拿 VS 自带 CMake，meson 的 wxWidgets cmake subproject 需要），再 `ninja -C build`。另外 `subprojects/zlib-1.2.11` 的 DACL 不继承工作区授权，ninja 重建 `build.ninja` 时要 stat 它的 `meson.build`，因此该命令需要 full-access 文件权限（见坑 16）。

**部署注意**：exe 目录 `build/src/qt_ui` 里有一份 windeployqt 的 Qt。使用 `QtQuick.Dialogs` 需要把 Qt 安装目录的 `qml/QtQuick/Dialogs`（含 `quickimpl` 全部文件）以及 `Qt6QuickDialogs2.dll`、`Qt6QuickDialogs2Utils.dll`、`Qt6QuickDialogs2QuickImpl.dll` 拷进去（本阶段已补齐）。

调试用环境变量：

| 变量 | 作用 |
|---|---|
| `AEGISUB_QT_LANGUAGE=zh_CN` | 强制翻译 catalog（默认跟随系统区域；本机 zh-HK 无对应 po，会回退英文） |
| `AEGISUB_QT_CONFIG_DIR=<dir>` | 覆盖 `?user` 目录（避免污染真实 `%APPDATA%\Aegisub`，也用于沙箱环境） |
| `AEGISUB_QT_TRACE=1` | 把启动阶段写入 exe 旁 `aegisub_qt_core.log` |
| `AEGISUB_PO_DIR=<dir>` | 覆盖 `.mo` 搜索根（默认相对 exe 的 `../../po`） |

## 5. 关键实现细节 / 已踩的坑

1. **Python 3.15 默认 UTF-8 模式** 让 meson 用 UTF-8 解码 `cl.exe` 的 GBK 输出而崩溃 → 所有 meson 调用用 `python -X utf8=0 -m mesonbuild.mesonmain`，并给 ninja 触发的 regenerate 设 `PYTHONUTF8=0`。
2. **头文件顺序**：`libaegisub/signal.h` 有成员名 `slots`，Qt 的 `#define slots public` 会破坏解析 → 含 agi 头的 `.cpp` 必须把 `ass_file.h` / `options.h` 放在任何 Qt 头之前。
3. **`libaegisub/option_value.h` 没有 include guard**：`src/options.h` 已包含它，不能再显式 include 一次。
4. **`AssFile::AssFile()` 是空构造**（此 fork），默认 `[Script Info]`/默认样式来自 `LoadDefault()`。
5. **`agi::fs::CreateDirectory`（boost::filesystem）在本机/沙箱会 fail-fast**（0xC0000409）→ 建目录改用 `QDir::mkpath`。
6. **`TextFileReader` 的编码参数**：传空串并**不会**自动检测（空串会被当成编码名）；需要先用 `agi::charset::Detect(path)` 检测再传入。
7. **`agi::Time`**：字符串构造宽松解析（`0:00:02.40`），格式化用 `GetAssFormatted()`；比较用 `int(Time)`（厘秒）。
8. **QML**：`TextArea` 没有 `textEdited` 信号（只有 `TextField` 有），用 `onTextChanged` + 值比较；从 C++ 刷新输入框用 `Connections` 监听 `activeRowChanged` / `modelReset`，避免与用户光标打架。
9. **qrc 隐式组件**不可靠，跨文件窗口/组件统一用 `Loader { source: "qrc:/X.qml" }` 或 `Qt.createComponent`。
10. **菜单快捷键**：`Action.shortcut` 依赖 `StandardKey`，在窗口真正获得前台焦点时生效（自动化里需先点击窗口再 SendKeys）。
11. **自动化验证**：UI Automation 的 `TogglePattern` 不会触发 QML `toggled`；`TextArea` 需 `ValuePattern.SetValue`；中文 IME 会干扰 SendKeys（自动化脚本可用 UTF-8 BOM 保存并临时切 US 键盘布局）。
12. **部署的 Qt 依赖**：exe 旁 windeployqt 目录若缺 `QtQuick/Layouts` 或 `QtQuick/Dialogs` 会导致 “module ... is not installed”，需从 Qt 安装目录补齐 `qmldir`/插件 DLL/依赖 DLL。
13. **`commit_id` 是回写指针**：`AssFileCommit::commit_id` 指向调用方（`Commit` 的 `amend_id` 参数），`PushState` 的接收者必须把最终采用的 id 写回去；`Commit` 的返回值就是它。合并提交判定必须同时检查「栈顶就是当前状态（`pos_ == size-1`）」，否则撤销后编辑会误改历史快照。
14. **`AssFile` 拷贝构造函数不拷贝 `ProjectProperties`**（`Info`/`Styles`/`Events`/`Attachments`/`Extradata` 才拷）。做 undo 快照和恢复副本时都要手动 `copy.Properties = file->Properties`，否则一次撤销会把工程属性洗成默认值。
15. **保存后不能再合并**：`markSaved()` 之后若继续把新编辑合并进栈顶（= 保存点）快照，会出现 `pos_ == saved_pos_` 但内容已改，脏标记永远为 false。`UndoManager::markSaved()` 会关闭一次合并开关。
16. **本机 DSH 沙箱的两个环境坑**：(a) `cmd` 的 `for /F ('command')` 会把整条命令当成一个 token，导致 `vcvars64.bat`/`VsDevCmd.bat` 全部失效——用 `.build-tools/build_ninja.sh` 代替 `build.bat`；(b) `subprojects/zlib-1.2.11` 的 DACL 是保护态（不继承工作区授权），受限沙箱下列不出目录、ninja 重建 `build.ninja` 会 `Access is denied`，需要 full-access。两者都只是构建环境问题，与源码无关。
17. **局部变量别叫 `restored`**：`UndoManager` 的信号就叫 `restored()`，在 `restoreTo()` 里用 `AssFile restored(...)` 会遮蔽信号名，`emit restored();` 直接编译失败（C2064）。同理信号名与成员变量不要撞。
18. **`wxString::utf8_string()` 在这个 wx 版本没有**：用 `wxString::ToStdString(wxConvUTF8)` 转 UTF-8（`commit_bridge.cpp`）。
19. **`agi::signal` 的槽按「连接顺序的逆序」执行**（`Signal::operator()` 从最后一个槽往前遍历），和 Qt 信号的正序不同。`SubtitleGridModel` 先构造先连接，所以在 `AnnounceCommit` 里**最后**收到通知：行增删提交时 `EditBoxViewModel` 的槽先跑，而网格 `rows_` 还指着刚 `delete` 掉的 `AssDialogue`，此时读 `getActiveDialogue()` 就是 use-after-free。因此 `EditBoxViewModel::onFileCommitted` 对 `COMMIT_DIAG_ADDREM` / `COMMIT_ORDER` / `COMMIT_NEW` 直接返回，交给网格 `modelReset` 刷新；`onRestored()` 的刷新用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 延后一个事件循环。
20. **`TextArea` 会自己吞掉 `Return`**：`QQuickTextEdit` 在 `keyPressEvent` 里处理 Return 并 accept，QML 的 `Keys.onReturnPressed` / `Keys.onPressed` 根本收不到（这也是 Stage 7 里 `TextArea` 没有 `textEdited` 的同一类问题）。所以 EditBox 的 Enter 流是在 `main.cpp` 里用应用级事件过滤器（`BodyEnterFilter`）接的：它只在焦点对象的 `objectName == "editBoxTextArea"` 时拦截 Return/Shift+Return，再调 `EditBoxViewModel::handleEnterKey()`。`Shift+Enter` 的插入动作交给 QML（`lineBreakRequested` 信号），因为 TextArea 由 QML 拥有。
21. **输入框不要做双向绑定**：`TextField.text` 一旦被用户编辑，QML 绑定就会被破坏；而「绑定 + 每次 commit 回写」会在用户输入中途把文本按解析后的规范格式覆盖（光标跳到末尾）。做法是 `loadSelection()` 显式回填 + 值相等就跳过，跨字段联动（Start/End/Duration）只更新**没有焦点**的那个字段。注意两个方向要分开：**外部变更**（undo/redo、切换行）必须**强制**回填所有字段，哪怕光标就在那个框里，否则撤销后框里还显示旧值；只有**自身编辑**引起的联动刷新才跳过有焦点的字段。
22. **SpinBox 的键盘焦点在它内部的 TextInput 上**：可编辑 `SpinBox` 聚焦时 `Window.activeFocusItem` 是它的 contentItem（`TextInput`），不是 `SpinBox` 本身，所以焦点隔离判断不能只看当前 item 的标记属性，要沿 `parent` 链向上找 `isEditBoxField`，否则在 Layer 框里按 `Insert` 会漏到网格行快捷键上。
23. **批量修改多行要传 `single_line = nullptr`**：网格的 `onFileCommitted` 对「带 single_line 的内容提交」只对那一行发 `dataChanged`；批量改多行如果不传 nullptr，就只有活动行会重绘，其余行还是旧值。传 nullptr 时网格对全部行发 `dataChanged`（**不要**在这里做 model reset，原因见坑 25）。
24. **行号型选中集合必须重新校准**：`selected_rows_` 存的是行号，删除/插入/撤销会改变行号含义，`refreshPreservingActiveRow()` 里必须 `sanitizeSelection()`（丢弃越界、保证有行时至少选中一行），否则 EditBox 的批量目标与 `selectionCount` 会指向错误行甚至越界。
25. **内容类批量提交不能触发 `modelReset`**：`EditBoxViewModel` 在 `modelReset` 里会 `resetCoalescing()`。批量元数据提交是 `single_line = nullptr`（网格拿不到具体行），如果为此走全量 `refreshPreservingActiveRow()`，逐字符输入会被打断成「一个按键一步撤销」。正确做法：内容类提交（`COMMIT_DIAG_FULL`，含批量元数据与 Replace All）只发全行 `dataChanged`；只有结构性提交（`COMMIT_DIAG_ADDREM` / `COMMIT_ORDER` / `COMMIT_STYLES` / `COMMIT_NEW`）才 reset 索引。
26. **表单回填必须先挂守卫**：`SpinBox` / `CheckBox` 在创建时用自己的初始值触发 `onValueChanged` / `onToggled`，如果处理器直接写 ViewModel，会把模型值写成初始值（实测样式管理器的 `fontSize` 被写成 `from: 1`，且顺手提交了一次）。做法：QML 里一个 `loadingForm` 标志，`loadStyleForm()` 赋值前置 true、赋完置 false，处理器里 `if (!loadingForm && vm != value)` 才写回。
27. **`ass_override.h` 没有 include guard**：`ass_dialogue.h` 已经 include 了它，再显式 include 会 C2011 重定义（和坑 3 的 `option_value.h` 同一类问题）。改名联动 `{\r...}` 标签时直接用 `AssDialogue::ParseTags` / `AssDialogue::BlockOverride::ProcessParameters` 即可。
28. **`AssStyle` 缓存了它的 ASS 行**：改完 `name`/`font`/颜色等字段后必须 `AssStyle::UpdateData()` 再 `Commit`，否则写盘用的还是旧串（这也是改名要 `UpdateData()` 的原因）。
29. **Material `Button` 压到 28px 高会看不到文字**：Material 默认上下 padding 合计超过按钮高度，contentItem 被裁掉（实测样式管理器左下的 New/Copy/Delete 变成空胶囊）。压高度时同时把 `topPadding`/`bottomPadding` 归零、高度给到 30px，或干脆自绘（3×3 对齐键盘就是自绘 Rectangle + Label）。

## 6. 已知限制（下一步工作）

- **Save As 的原生对话框自动化未覆盖**：Windows 下 `FileDialog` 用系统对话框，自动化只验证了对话框弹出；保存逻辑本身（与 `saveFileAs` 共用 `saveToPath`）已通过 Ctrl+S 保存验证。
- **Edit 菜单不显示快捷键文本**：Qt Quick Controls 的 `MenuItem` 不渲染 `Action.shortcut` 的文本（Qt Widgets 的 QMenu 才会），所以菜单里只有 `Undo` / `Redo <desc>`，看不到 `Ctrl+Z`/`Ctrl+Y` 提示；快捷键本身由 `main.cpp` 的事件过滤器实现并已验证（要提示得给两个菜单项写自定义 `contentItem`）。
- **撤销粒度**：快照式 undo 每次提交都深拷贝整个 `AssFile`（1000ms 合并窗口内只保留一份），大文件 + 高频输入时内存/CPU 有开销；栈上限 100 步，保存点被挤出后文档会一直显示「已修改」。
- **EditBox 仍缺的字段/交互**：没有 `MarginL/MarginR/MarginV` 三个边距、没有时间微调按钮（±10ms/±帧）与「按帧编辑」、没有自动定位到视频时间；`availableStyles` 只来自当前文件的 `[V4+ Styles]`（不读样式库）；新增字段的 UI 文案（`Comment`/`Layer`/`Style`/`Actor`/`Effect`/`Duration`/`chars`）还没进 po，实际显示英文。
- **网格操作仍有缺口**：已支持插行 / 复制 / 删行 / 多选批量操作（含 Undo/Redo），但键盘范围选择（`Shift+↑/↓`）、拖拽框选、列排序、行折叠、跨行拖动都没做；`refresh()` / 撤销后的 `refreshPreservingActiveRow()` 仍是全量 `beginResetModel`，行数很多时每次增删都要整体重绘。
- **查找替换限制**：只在当前文件内搜索（没有跨文件/批量目录替换）；命中只是选中整行，网格里不会高亮具体匹配片段；普通模式替换是字面量（正则模式用 `\1` 反向引用，不支持 `$1` 写法）；`In selection` 只在有多选时生效。
- **样式管理器限制**：只编辑当前脚本的 `[V4+ Styles]`，不读写样式库/样式目录（`AssStyleStorage`）；未暴露 ScaleX/ScaleY、Spacing、Angle、Encoding；没有颜色选择器（只能填 `#RRGGBB` + Alpha）、不能多选样式；预览是纯 QML `Text`，只反映字体/字号/粗斜下删/主色/描边色，不渲染 ASS 的阴影、不透明度混合等真实效果；删样式不做引用修复（与 wx 版一致，字幕行会保留指向已删除样式的名字）。
- **菜单/工作区限制**：Qt Quick `Label` 不解析 `&` 助记符，菜单栏标题里的 `&` 被剥掉了（`Alt+F` 这类键盘导航不可用）；菜单项快捷键列是 `ShortcutMenuItem` 自绘的静态文本，真正的按键由 `Shortcut` 元素驱动，两者必须手动保持一致；状态栏编码在新建文档和保存后固定显示 `UTF-8`（保存永远写 UTF-8）。
- **大文件性能**：`ListView` 已复用 delegate，但尚无分页/滚动加载。
- **没有最近文件列表 / 另存时的编码选择器**（当前保存固定 UTF-8，打开时自动检测）。
- **未保存确认框**：`Save` 分支里如果用户取消了「另存为」系统对话框，原动作（New/Open/退出）会停住而不是继续/中止；没有「稍后再说」以外的记忆。
- **只做了 Backup 一个首选项页**、Properties 只做了 3 个字段（标题是每次按键提交，靠 1000ms 合并成一步）。
- **bestsource** 在本机构建中被禁用（子模块 `libp2p` 在 bitbucket 需要认证），视频源只有 ffms2。

## 7. 验证方式

- 编译：`.build-tools\build.bat aegisub-qt`（EXIT 0）、`.build-tools\build.bat`（wx 回归 EXIT 0）。本机 DSH 沙箱里改用 `bash .build-tools/build_ninja.sh [target]`（见 §4），Stage 7 / 8A 实测 `aegisub-qt` 与全量（wx + qt）均 EXIT 0。
- 运行验证用 UI Automation + `PrintWindow` 截取窗口（不抓桌面），已确认：
  - About：版本/版权/简体中文译员名单；
  - Preferences：开关写回 `agi::Options` 并落盘 `config.json`；
  - Properties：改 Title/PlayRes 后 `dumpAss()` 输出的 `[Script Info]` 反映修改；
  - Grid/EditBox：点击行 → 底部显示该行时间与文本；编辑文本 → 网格对应行实时更新；
  - Stage 6：`aegisub-qt test.ass` 打开含中文/多行/多样式的文件 → 编辑文本与开始时间 → 标题出现 ` *` → Ctrl+S 保存 → 标题 ` *` 消失 → 重新打开文件，网格与 EditBox 内容为修改后的值，`[Script Info]`（Title/PlayResX/PlayResY）与 `[V4+ Styles]`（Default/Title）完整保留（截图 `.build-tools/qt_*_check.png`、`.build-tools/stage6_result.png`，不入库）。
  - Stage 7（`aegisub-qt test.ass`，脚本 + 16 张截图 + 机器可读状态在 `.build-tools/stage7/`，不入库）：
    - 打开 → 标题无 `*`、EditBox 与网格第 1 行一致；
    - TextArea 内逐字符输入 → 标题出现 `*`、网格与 EditBox 同步；
    - **焦点在 TextArea 时按 Ctrl+Z** → EditBox 与网格**同时**回退、标题 `*` 消失；Ctrl+Y → 修改恢复、`*` 重现；
    - 合并提交：6 个字符（间隔约 155ms）输入后**一次** Ctrl+Z 全部回退（不是只删末字符）；
    - 未保存确认框：`Ctrl+N` / 窗口关闭都弹 Save·Discard·Cancel；Cancel 内容不丢，Discard 新建/退出成功；
    - 保存点：Ctrl+S 去 `*`，随后 Ctrl+Z 回原值并重新变脏；Ctrl+Y 回到保存点自动变干净；
    - Properties：改 Title → 主窗口变脏，Ctrl+Z → Title 字段与主标题同时回退（`PropertiesViewModel` 订阅 `restored`）；
    - Edit 菜单：栈空时 `Undo` 置灰，`Redo <desc>` 显示历史名（动态文案）；
    - 已知限制：菜单里不显示 `Ctrl+Z`/`Ctrl+Y` 文本（见 §6）。
  - Stage 8A（行操作，脚本/截图在 `.build-tools/stage8/`，不入库；用副本 `test_rows.ass`，原文件不动）：
    - 右键任意行 → 上下文菜单 4 项（`Insert Before`/`Insert After`/`Duplicate`/`Delete`）；`Insert After` → 行数 +1、选中新行、EditBox 空、标题 `*`、Style 继承、Start = 当前行 End；
    - `Ctrl+Z` → 行数与内容复原、`*` 消失；`Ctrl+Y` → 新行回来；
    - `Subtitle → Insert Before` → 新行插到当前行上方、Style 继承；`Ctrl+D` → 副本紧跟原行，Text/Start/End/Style/Actor 逐字段一致；
    - `Delete` → 行数 -1、选中落到邻行且 EditBox/时间框同步；`Ctrl+Z` → 被删行内容完整恢复；
    - 连删到空表：最后一行删除后自动补空默认行（`0:00:00.00`–`0:00:03.00`），撤销可恢复；
    - 焦点隔离：焦点在 EditBox 文本框时 `Delete`/`Insert` 只作用于文本（不删行/不插行）；点网格行后焦点转到网格，`Delete`/`Insert`/`Ctrl+D` 直接作用于行；
    - 混合操作（插行 + 复制 + 删行）后 `Ctrl+S` → 关闭重开，逐行 Text/Start/End/Style/Actor 与保存前一致；
    - Stage 7 回归不受影响（文本编辑、Ctrl+Z、未保存确认框）；`app_stderr.log` 无 QML 警告/报错。
  - Stage 8B（EditBox 字段与键盘流，脚本/截图在 `.build-tools/stage8b/`，不入库；用副本 `test_meta.ass`）：
    - `Comment` 勾选 → 网格该行灰显（文本列亮度 p95 161→104）且标题带 `*`，Ctrl+Z 精确恢复颜色与标题；
    - `Style` 下拉（`Default`/`Title`）切换 → 网格 Style 列实时更新；`Layer`/`Actor`/`Effect` 修改 → 网格对应列更新，逐步 Ctrl+Z 可回退；
    - `Duration` 与 `Start`/`End` 双向联动（改 Duration → End 与网格同步；改 Start → Duration 实时），撤销后三个时间框**立即**回填（即使光标还在框里）；
    - `Enter` = 提交并跳下一行、末行自动新建（5→6 行）且焦点留在正文可继续输入；`Shift+Enter` = 插入字面 `\N`（保存后磁盘 ASS 确认）；
    - 焦点隔离覆盖 `SpinBox`/`ComboBox`/`CheckBox`：Layer 框内按 `Insert`/`Delete` 不再增删行，点网格行后快捷键照常可用；
    - 布局：1000x700 客户区下元数据行 / 时间行 / 正文区全部完整可见（EditBox 约 176px 实际高度，无裁剪、无浮动占位符溢出）；
    - 回归：行操作、文本撤销、未保存确认框、元数据保存重开均无损。
  - Stage 9A（多选批量 + 查找替换，脚本/截图在 `.build-tools/stage9a/`，不入库；用副本 `test_multi.ass`）：
    - `Shift+Click` 选 1–3 行 → 活动行深蓝、其余选中行浅蓝，`Ctrl+Click` 切换不把选择清空，`Ctrl+A` 全选 5 行且 EditBox 内 `Ctrl+A` 只选文本不影响行选择；
    - 批量 `Comment`：3 行同时变灰（文本列亮度 151/154/186 → 97/97/115），**一次** `Ctrl+Z` 三行同时恢复；
    - 批量 `Delete`：5→3 行，一次撤销回到 5 行且逐行内容与 ASS 一致；批量 `Ctrl+D`：5→7，一次撤销复原；
    - 批量元数据（Actor/Effect/Layer）：3 行同时应用并**一次撤销全部恢复**（修复前逐字符一步），且选中集合在批量编辑后保留；
    - `Ctrl+F` 打开非模态 `Find & Replace`：`Find Next`/`Find Prev` 选中并滚动定位（状态 `Line N`）；
    - `Replace All`（普通/正则/`In selection`）：状态 `Replaced N occurrence(s) in M line(s)`，一次 `Ctrl+Z` 全部撤销；正则非法只显示 `Invalid regular expression` 不崩溃；`Replace` 替换当前命中并跳到下一处；
    - `Esc` 关闭搜索窗，`Edit→Find/Replace` 复用同一实例并聚焦对应输入框，关闭主窗口时搜索窗一起关闭、进程退出。
  - Stage 9B（M1 终章：样式管理器 + 桌面工作区，脚本/截图在 `.build-tools/stage9b/`，不入库）：
    - Styles Manager 独立窗（800×560）：列表 `Default`/`Title`、表单与 ASS 一致（`Arial`/`48`/`#ffffff`/`A=255`）、`Preview Text` 正常渲染；
    - `New`/`Copy`/`Delete`：新样式实时进入 EditBox 的 `Style` 下拉、`Copy` 不重名、删除同步消失；删到只剩 1 条再删显示 `Cannot delete the last style` 且列表保留 1 条；
    - 改 `Size`/`Bold`/`Primary`/对齐 → 预览实时变化、标题 `*`；保存后磁盘 `Style: Title` 行为 `60`/`-1`/`&H0000FF00`/`7`，重开文件后值仍在；
    - 重命名 `Title`→`TitleNew` 联动网格 `Style` 列，级联到 `{\r...}` 标签，**一次** `Ctrl+Z` 样式表与字幕行同时回退；
    - 样式字段编辑撤销后表单/预览立即刷新（即使样式窗仍打开）；`Esc` 关闭、关闭主窗口连带关闭；
    - 紧凑菜单栏高 **28px**、无 `&` 字符，File/Edit/Subtitle 菜单右侧显示 `Ctrl+N/O/S/Shift+S/Q`、`Ctrl+Z/Y/F/H`、`Ctrl+D`/`Del`/`Insert` 提示；
    - `SplitView` 可拖拽（实测网格 457→409、EditBox 175→223，且不低于 160/150）；状态栏 `5 lines` → `5 lines | 3 selected` → `6 lines`，右侧 `utf-8 | Aegisub <版本>`；
    - 回归（文本编辑/撤销、行操作、查找替换、未保存确认框）正常，stderr 无 QML 警告/报错。
    - **「偶发静默退出」的排查结论**：不是应用崩溃，而是**两套自动化脚本按进程名互杀**（旧 `lib.ps1` 的 `Start-App()` 用 `Get-Process -Name aegisub-qt | Stop-Process -Force` 做全局清理，且两个测试同时在跑）。改为按 PID 清理后串行复跑 30 轮：`ExitCode` 全为 0、无 PREMATURE_EXIT、事件日志无 `Application Error`/WER、无按键被误触到窗口关闭按钮。
