# Aegisub Qt6 frontend (`aegisub-qt`)

本文件记录在 `feature` 分支上并行开发 Qt6/QML 前端的架构、构建方式、已完成进度与已知限制，便于随时交接。

> 原则：**wxWidgets 原版构建保持可用**（双轨制），Qt 前端是新入口 `aegisub-qt`，不修改 `src/` 下原版 wx 界面文件与 `libaegisub/` 核心逻辑；新增 Qt 代码只放在 `src/qt_ui/`。

## Stage 15：原版菜单与高频编辑命令

基线 `feature@276856d8b`，本阶段按双轨构建和 Qt 验收封板。Qt 主菜单按 Stage 15 支持范围对齐[原版菜单](../src/libresrc/default_menu.json)和[默认快捷键](../src/libresrc/default_hotkey.json)，Automation 留待 Stage 16。按本阶段约定，`Delete` 删除字幕行、`Ctrl+Space` 播放活动行音频；`Alt+Up` / `Alt+Down` 只注册快捷键，不出现在主菜单。

- [主菜单](../src/qt_ui/Main.qml)依次为 File、Edit、Subtitle、Timing、Video、Audio、View、Help。普通项、图标项、勾选项和子菜单入口均复用 [ShortcutMenuItem](../src/qt_ui/ShortcutMenuItem.qml)（30 px / 13 px）；Options 使用 16×16 齿轮，勾选框 14×14。`Ctrl+D` / `Ctrl+Shift+D` 只用于按帧拆行，`Ctrl+1` / `Ctrl+2` 按视频 START/END 映射跳活动行首末帧；新增 `F3` 和 `Alt+O`，不再绑定 `Ctrl+J`。Spectrum / Waveform 共享同一状态并互斥。
- [网格模型](../src/qt_ui/viewmodels/subtitle_grid_model.cpp)按选中行顺序复制标准 `Dialogue:` / `Comment:`，支持原版字幕行与普通文本粘贴；从活动行后插入并选中新行。键盘剪贴板命令只在主窗口的非文本焦点触发。拆行按原版帧边界分配当前帧，帧在原行外时追加该帧的副本；合并取选中行时间跨度，Concatenate 以 `\N` 连接，Keep First 保留首行正文。帧卡点、相邻连续化、七种稳定排序和多选移位均单次提交；重排跟随原活动行及选区。
- [重采样窗口](../src/qt_ui/ResampleDialog.qml)支持输入目标分辨率、From Video 和 Esc。[重采样 VM](../src/qt_ui/viewmodels/resample_vm.cpp)一次提交脚本头、样式与字幕；字号、描边、阴影以 Y 比例缩放，边距按轴缩放。[标签工具](../src/qt_ui/ass_tag_utils.cpp)缩放 `\pos`、`\move`、`\org`、矩形裁剪和绝对 `\fs`，保留 move 时间、相对字号、向量裁剪、其他标签和自动化模板代码。未接入原版的宽高比策略和色彩转换。

**构建与验证**： [Qt 单目标](../.build-tools/stage15/build-qt-final.log)、[wx + Qt 双轨](../.build-tools/stage15/build-dual.log)均 `EXIT 0`；[Qt 单测](../.build-tools/stage15/test-qt.log) `3/3` 通过（ASS 标签、语法高亮、音频频谱）。首次受限构建不能读取既有 zlib 目录；切换到完全权限后完成构建，未更改 Windows 文件权限。

- Windows UI Automation 已核对八栏菜单和子菜单顺序，96 DPI 下普通、图标、勾选及子菜单行均为 30 px；验证合并、连续化、Start Time 排序、两方向移位、视频首末帧跳转、两种拆行及一次撤销。1280×720 → 1920×1080 已验证持久化脚本分辨率、字号 20 → 30、描边/阴影、边距和 `\pos(160,100)` → `\pos(240,150)`，一次撤销整体恢复。[通过的桌面用例及截图](../.build-tools/stage15/20261125-101511-49716-Light/acceptance.log)，[最终构建对菜单高度和排序/移位的复测](../.build-tools/stage15/20261125-102317-53504-Light/acceptance.log)。完整通过的测试实例退出 0，stderr 只有普通文档加载/保存记录。后续 Windows 复跑受前台激活限制，改由[无头 Qt 集成测试](../src/qt_ui/stage15_integration_test.cpp)补齐：在真实主窗注入按键，验证标准 Dialogue/Comment 剪贴板往返、剪切/粘贴单次撤销、EditBox 文本焦点隔离、Alt+O 打开 Options、F3 选中下一个匹配行，以及 Ctrl+3/4 卡到 Dummy Video 帧边界并逐步撤销。Meson Qt 用例合计 4/4 通过，集成测试退出 0、stderr 为空；最终双轨构建退出 0。

## Stage 14：视频视觉工具进阶（M3 路线 B）

基线 `feature@4e9e36abb`，本阶段未提交。Qt 视频栏工具依次为 Std / Pos / Move / RotZ / Scale / Clip。36 px 工具栏复用 28 px 紧凑按钮和 18 px SVG；空间不足时纵向滚动，键盘聚焦时自动露出目标按钮。三个新增图标均为 [Google 官方 Material Symbols Outlined](https://github.com/google/material-design-icons/tree/10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7/symbols/web)，来源与 Apache-2.0 许可见[图标说明](../src/qt_ui/icons/README.md)。

- [ASS 标签工具](../src/qt_ui/ass_tag_utils.cpp)按原文区间编辑顶层覆写标签，保留中文、非目标标签及 `\t(...)` 内容。Move 把 `\pos` 原位转换为 `\move`（已有 Move 时保留其六参数时间窗口），清除冲突的定位标签；RotZ 支持 `\fr`、`\frz`、样式默认角和 `\org`；Scale 分轴回退到样式，并在写入时钳到至少 1%。新标签数值最多保留一位小数，旋转写入归一到 ±180°。
- [视觉工具 ViewModel](../src/qt_ui/viewmodels/visual_tools_vm.cpp)沿用 Stage 12 的提交链：行选择、文档重载、提交和撤销同步状态；拖动期间 `COMMIT_DIAG_TEXT` 即时更新 EditBox、网格，libass 按 16 ms 合并刷新，松手补画末态。同一手势复用合并 ID，一次 Ctrl+Z 恢复拖动前文本。
- [视频覆盖层](../src/qt_ui/visual_overlay_item.cpp)与预览共用视频实际绘制区域和脚本坐标映射。Move 有起终点和虚线箭头，未设路径时先显示向画面内偏移的终点，Shift 锁主轴；RotZ 以 `\org`（若有）或字幕锚点为中心，按 `atan2(-dy, dx)` 取逆时针正角，Shift 吸附到 15°；Scale 的右、下、右下手柄分别控制 X、Y、等比缩放，Shift 也可保持初始比例。
- 构建：Qt 应用目标及 wx + Qt 全量双轨均退出 0；更新后的 ASS 标签单测已显式重建。Meson 的 `qt-ass-tag-utils`、`qt-ass-highlighter`、`qt-audio-spectrum` **3/3 通过**（`EXIT 0`）。受限模式首次构建命中既有 zlib 读取限制，使用正常完整文件权限构建通过，未更改文件权限。

- Windows UI Automation + 截图均通过：[深浅主题总日志](../.build-tools/stage14/20261125-091739-10948/acceptance.log)、[Move 终点补测](../.build-tools/stage14/20261125-091938-11952/acceptance.log)。Move 起终点均实测按住时更新 EditBox、网格和 libass；拖终点在暂停的路径中段验证，不改起点且不遗留 `\pos`。逆时针拖动产生 `\frz59.5`，黄色 libass 字幕图形的屏幕主轴约 −61.2°（屏幕 Y 向下），覆盖层射线同向。X 杆将 93×13 px 画宽放大到 165×13 px，角手柄等比放大到 151×21 px。每次拖动一次 Ctrl+Z 即恢复原文和干净标题。深浅主题六个工具 SVG、143 px 高视频区滚动、Pos/Clip 实时刷新及撤销均通过。所有验收实例 `APP_EXIT=0`，stderr 仅正常加载记录。截图及复测脚本仅在 `.build-tools/stage14/`。

## Stage 13：动态主题、图标、ASS 高亮与频谱

本阶段基线 `feature@9c6654f40`。接口及验收约定：

- 全应用共用 `themeVM`：`seedColor` 默认 `#00BCD4`，`darkMode` 默认 true，使用 `?user` 配置自动保存恢复。用 OKLCH 在 sRGB 色域内推导 `primary/onPrimary/primaryContainer/onPrimaryContainer`、`secondary/secondaryContainer/onSecondaryContainer`、`surface/surfaceContainerLow/surfaceContainer/surfaceContainerHigh/surfaceContainerHighest`、`onSurface/onSurfaceVariant/outline/outlineVariant`、`rowActive/rowSelected/rowAlt/rowComment`、`danger/warning/kfLine`。`radiusSmall/radiusMedium/controlHeight/menuBarHeight/statusBarHeight` = 4/6/32/28/24。`presetColors` 含 Cyan #00BCD4、Blue #3B82F6、Violet #8B5CF6、Sakura #EC4899、Mint #10B981、Amber #F59E0B。Appearance 支持开关、色卡、自定义 #RRGGBB。
- 图标资源约定：Google Material Symbols，`qrc:/icons/<name>.svg`。名称：`play_arrow pause stop skip_previous skip_next zoom_in zoom_out graphic_eq equalizer near_me open_with crop close add content_copy delete search settings`。
- `AssSyntaxHighlighter` 在 EditBox 文本上区分括号、块内标签/参数/颜色、块外 `\N/\n/\h`、错误括号。根据 darkMode 与 primary 换色，不产生文档提交或撤销步骤。
- `AudioBridge::ComputeSpectrum`：256 或 512 点 Radix-2 FFT、Hann 窗、对数 dB 归一化、越界列为零；`audioVM.spectrumMode` 默认 false。视口热力图独立缓存，保留打轴、关键帧、播放头与吸附。Audio 菜单 Spectrum Mode 与 Wave / Spec 共用状态。
- UI 验收：青蓝深色启动；Appearance 选 Sakura/Mint、切浅色，自定义值重启恢复；截图主窗口及面板、图标、深浅主题 ` {\fad(300,300)}第一行\N中文` 与多余/未闭合括号；确认主题切换不产生脏标记或 Undo；440Hz 频带、关键帧和选区红蓝线、拖动与一次 Ctrl+Z；Stage 12 `\pos`、`\clip` 与核心读写撤销回归。退出码和 stderr 单独检查。

### 实现与验证记录

- OKLCH 保持色相和感知亮度，对超出 sRGB 色域的颜色二分降低色度。Qt 配置独立写入 `?user/qt-theme.ini`，不更改 wx 配置；样式颜色和实际视频像素不是界面主题的一部分。
- 图标来自 [Google Material Symbols Outlined 官方源码（固定提交）](https://github.com/google/material-design-icons/tree/10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7/symbols/web)，采用 **Apache-2.0**。18 个 24px SVG、[许可证](../src/qt_ui/icons/LICENSE)和[来源说明](../src/qt_ui/icons/README.md)随 qrc 打包。本机 Qt 6.6.3 部署含 `Qt6Svg.dll`、`imageformats/qsvg.dll` 和 `iconengines/qsvgicon.dll`，不需要额外字体。
- 高亮复用 `TokenizeDialogueBody`，将 UTF-8 字节位置换算为 UTF-16 排版位置。Qt 6.6 的 `TextArea` 也会将纯格式重排广播成 `textChanged`；主动换色时仅临时屏蔽编辑器的对外信号，保留文档排版刷新。初始回填结束前不向文档写回。单测使用真实 QML `TextArea` 统计事件，并检查中文/代理对、错误标记、正文及撤销历史。
- 频谱采用 512 点 Hann 窗和 Radix-2 FFT，幅度映射到 −80…0 dBFS，对数频率轴低频在底部。画布按音源、可见采样范围、列数和解码进度缓存 Indexed8 图像；换色仅更新色表，播放头和拖线仅重绘叠加层。C++ 测试调用真实 `AudioBridge::OpenDummy/ComputeSpectrum`，覆盖 440 Hz 主峰、归一化、首尾部分窗口、越界、空区间和关闭音源。
- 构建：`bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` 对应 Qt 目标链接成功；`bash .build-tools/build_ninja.sh` 全量 **EXIT 0**（含 wx `aegisub.exe`）。三个阶段相关测试 `qt-ass-tag-utils`、`qt-ass-highlighter`、`qt-audio-spectrum` 的执行文件均 **EXIT 0**。

- Windows UI Automation：以 168 DPI 启动带中文 ASS、关键帧视频、48 kHz / 440 Hz 单声道 WAV 的四区主窗。实际截图 1750×1225：[默认青蓝深色](../.build-tools/stage13/01-cyan-dark.png)、[Appearance 选 Sakura](../.build-tools/stage13/02-sakura-appearance.png)、[Sakura 主窗](../.build-tools/stage13/03-sakura-dark.png)、[Mint 浅色](../.build-tools/stage13/04-mint-light.png)。截图下四分之一区域出现最多的颜色分别从 `#111719` 变为 `#1C1316`、`#EDF6F1`。换色后标题保持干净，Ctrl+Z 不改变字幕；[错误括号](../.build-tools/stage13/05-brace-errors.png)有独立截图。`#8734AB` 和浅色设置在[重启后恢复](../.build-tools/stage13/06-custom-restart.png)。
- [频谱截图](../.build-tools/stage13/07-spectrum-440.png)：440 Hz 亮带实测位于绘图区自上而下约 66.6%，符合该对数频轴的预期；同时识别起止红蓝线和紫色关键帧线。拖动改变打轴，一次 Ctrl+Z 还原起止时间及干净状态。Stage 12 的 `\\pos` 拖动、写盘、撤销，以及[矩形裁剪](../.build-tools/stage13/08-stage12-clip.png)拖动及一次撤销均通过。主题、重启与媒体三个实例的真实 `APP_EXIT=0`；stderr 仅正常的文档 loaded/saved 信息，无 QML、图标或媒体告警。
- 最终[桌面验收日志](../.build-tools/stage13/acceptance.log)另核对 `Wave` 默认选中、工具栏切 `Spec` 后菜单勾选、菜单取消后工具栏回到 `Wave`。截图在桌面前台采集，按每张 9×7 个采样点检查窗口属于测试进程。
- **原版套件例外**：额外运行 `meson test -C build --no-rebuild --suite Aegisub`，三个 Qt 测试全过；原版 `gtest main` 使用系统 TEMP 时为 327/354，失败集中在 `lagi_acs`（18）和 `lagi_audio`（9）。将 `TEMP`、`TMP`、`TMPDIR` 指向可写的 `build/stage13-gtest-temp` 后，音频专项 **24/24，EXIT 0**；重建夹具并用同样环境运行完整套件，原版最终 **336/354，EXIT 1**，剩余 18 项全部是 `lagi_acs`。在 `build` 工作目录中夹具 `data/file` 存在，但用未包含 Qt 改动的原版测试 EXE 单独运行 `--gtest_filter=lagi_acs.CheckFileRead` 仍失败。未变更原版源码、测试断言或系统权限，不能将整个项目测试套件标为全绿。
- 复测入口：[Windows 测试脚本](../.build-tools/stage13/run.ps1)。使用本机既有 Stage 6 字幕和 Stage 11B 测试视频，创建隔离配置与 440 Hz WAV，不读写用户真实配置。该机系统 TEMP 下的 ffms2 索引/主题文件不可可靠写入；验收使用 `build` 下的独立运行目录。该目录的截图/日志/临时脚本已从 `src/qt_ui/tests/stage13/` 整体移出到 gitignore 的 `.build-tools/stage13/`（仓库内不再混入 `.png`/`.log`）。

### S14：音视频打开闪退修复（2026-11-25）

- **现象**：受限环境（低完整性令牌，或 `%LOCALAPPDATA%`/`%APPDATA%` 不可写）下打开任意视频/音频约 1.6s 后进程 `0xC0000409` 闪退；Windows 事件日志固定同签名（`ucrtbase.dll+0xA527E`），本机 5 个 WER dump 的 `ProcessIntegrityLevel` 全部是 `0x1000`。
- **根因**（详细机制见坑 56–59、61）：`?local/ffms2cache` 建目录失败 → `CHECKED_CALL` default 分支 `LOG_D` 写出 GBK「存取被拒。」→ Windows `EmitSTDOUT` 按 UTF-8 转码抛 `agi::charset::BadInput` → `dispatch::Queue::Async` 在 worker 上 `rethrow_exception` → Qt 版 `invoke_main` 直通且无 catch → `std::terminate` → `abort()`。`FFMS_Init` 时序、中文路径均不是本次崩因（后者是独立的打不开 bug，见坑 60）。
- **修复**（只改 `src/qt_ui/`，不动 `src/` 与 `libaegisub/`）：① `aegisub_core.cpp` 给 `invoke_main` 的 thunk 加 `try/catch`；② 用 no-throw 的 `StderrLogEmitter` 替代 `EmitSTDOUT`（原始字节写 stderr，默认只 Warning 以上）；③ 启动时无条件用 `QDir` 准备 `?local` 与 `?local/ffms2cache`，不可写时按「默认 → exe 旁 `.qt_local` → `%TEMP%\Aegisub-qt\local`」回退（写权限用临时文件真实探测；`AEGISUB_QT_CONFIG_DIR` 模式不回退）；④ `GetCacheFilename()` 的建目录改为非抛 `std::filesystem::create_directories`，失败只写数字错误码；⑤ `ProbeTracks` 前移 `FFMS_Init`、RAII `FFMS_CancelIndexing`、整体 try/catch，`VideoBridge::LoadSubtitles` 与 CLI `--video/--audio` 补 catch（含 `agi::Exception`）；⑥ `FlushOptions` 补 `catch (agi::Exception)`——复测中抓到的第二个同签名崩溃点（退出时写配置失败）。
- **验收环境说明**：DSH exec shell 及其子进程是 **Low Integrity（`S-1-16-4096`）**，实测 `%APPDATA%`/`%LOCALAPPDATA%`/`%TEMP%` 的 `CreateFileW` 均为 `ERROR_ACCESS_DENIED`、只有仓库工作区可写——正好等同用户原始崩溃环境，所以复测直接走 `.qt_local` 回退分支；桌面双击（Medium IL、AppData 可写）不会走回退。
- **验收结果**（重新构建的 `build/src/qt_ui/aegisub-qt.exe`）：裸环境 `--video` / `--audio` / `--video`（含音轨）三个场景均 **ExitCode=0、无 0xC0000409、无 `could not create the ffms2 cache directory`**，`.qt_local/ffms2cache/*.ffindex` 正常写出，视频画面 + libass 字幕、音频波形均正常（[视频+字幕](../.build-tools/diag-s14/lead/verify_final/s1_bare_video.png)、[波形](../.build-tools/diag-s14/lead/verify_final/s2_bare_audio.png)、[四区](../.build-tools/diag-s14/lead/verify_final/s3_bare_video_audio.png)）；`AEGISUB_QT_CONFIG_DIR` 对照组 ExitCode=0；两级 `?local` 都不可写的最坏情况优雅报错、ExitCode 仍 0；`aboutToQuit` 写配置失败场景（`.qt_user/config.json` 变目录）ExitCode=0 且只打 `Failed to flush options` 告警。`bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` 与全量 `bash .build-tools/build_ninja.sh` 均 **EXIT 0**。
- 复现与验收资产在 `.build-tools/diag-s14/`（不入库）；本阶段临时截图/日志/脚本已从 `src/qt_ui/tests/stage13/` 移到 `.build-tools/stage13/`。

### P4：Windows 中文路径复测（2026-11-25）

- `bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe`、全量 `bash .build-tools/build_ninja.sh`（wx + Qt）和 `qt-audio-spectrum-test.exe` 均 **EXIT 0**。原版 wx 源文件、`libaegisub/` 和第三方库未改动。
- Windows 桌面测试未设置 Aegisub/Qt 覆盖环境变量，素材目录及 ASS、含音轨 AVI、独立 WAV 文件名均包含中文和空格。ASS 编辑后保存到原文件，独立进程重开逐字匹配；仅给 `--video` 即显示视频与自动加载的音轨波形，同帧有/无 ASS 的截图对照确认 libass 绘制字幕；独立 WAV 波形和频谱均有实际像素输出。四次裸环境应用退出均为 **ExitCode=0**，无媒体路径错误或闪退。
- 本机 `?local` 回退到中文程序目录里的 `.qt_local`。冷缓存时，含音轨 AVI 只生成一个 287 B `.ffindex`，视频和自动音轨共用；重开时文件名、大小、SHA-256 和创建时间不变，只有原版 `Touch()` 更新的修改时间变动。单独开启诊断日志后，两路分别读取 287 B，未触发索引写入。独立 WAV 生成另一个 121 B 索引。
- 本机证据：[测试日志](../.build-tools/p4-unicode/verified-中文/run.log)、[视频与字幕及波形](../.build-tools/p4-unicode/verified-中文/02-av-first.png)、[独立音频频谱](../.build-tools/p4-unicode/verified-中文/05-audio-spectrum.png)、[缓存清单](../.build-tools/p4-unicode/verified-中文/after-audio-cache.json)。

### 工具栏与 Follow Playhead（2026-11-25）

- [紧凑按钮](../src/qt_ui/CompactIconButton.qml)统一使用 18×18 SVG、零 inset/padding、主题前景和选中背景；禁用图标降低不透明度，纯图标动作保留无障碍名称与提示。音频工具条优先保留传输、缩放、Wave/Spec 与跟随按钮，状态文字按宽度收起；视频文件名在预览内截断，窄栏帧号与时码换行。辅助窗调整最小尺寸、面板间距和选中态对比度；ASS 对齐格按数字小键盘排列。Qt 6.6 裸部署用显式 Material `ToolTip`，避免附加式提示在运行时懒加载报 `QQmlComponent: Component is not ready`。
- [Follow Playhead](../src/qt_ui/viewmodels/audio_vm.cpp)默认开，Audio 菜单和音频工具栏同步切换。画布将实际宽度同步给 ViewModel；播放期间左越界或超过 85% 时翻页，目标位置为 `playhead - 15% × visibleMs` 并夹在音频范围内。普通 16 ms 播放 tick 仅刷新叠加层；跨页才刷新采样视口并复用已有 FFT 缓存机制。本次验证了实际翻页行为，未做持续帧率基准测试。
- 构建：Qt 目标、wx + Qt 全量、三项 Qt 核心回归（ASS 标签、语法高亮、FFT）均 **EXIT 0**。全部 23 个 SVG 在 QML、QRC 和磁盘上逐项相符；从应用 `PATH` 排除 Qt SDK，保留部署目录自带的 `Qt6Svg.dll` 和 `qsvg` 插件，应用仍能渲染图标。
- Windows 桌面：Cyan 深色与 Mint 浅色各采集主窗和七个辅助窗的原生 1000×700 客户区，两套均通过媒体传输/视觉工具、音频工具、样式按钮、查找及设置图标像素检查。12 秒独立 WAV 在 800 px/s 验证波形和频谱：开关打开后两者约前翻 2.6 s，播放头留在画面内；关掉后标尺像素无位移，播放头越出视口。桌面实例退出码均为 0，stderr 无 QML 或运行时错误。
- 本机证据：[双主题验收记录](../.build-tools/ui-follow/evidence/20261125-011329-82070e6b/acceptance.log)、[Cyan 主窗](../.build-tools/ui-follow/evidence/20261125-011329-82070e6b/cyan-dark-main.png)、[Mint 主窗](../.build-tools/ui-follow/evidence/20261125-011329-82070e6b/mint-light-main.png)、[Follow 验收记录](../.build-tools/ui-follow/evidence/20261125-010905-6e5adb42/acceptance.log)。最终构建另通过 [P4 中文路径回归](../.build-tools/p4-unicode/ui-follow-final-中文/run.log)：ASS 保存重开、带音轨视频、独立 WAV 和索引重用均正常。截图与临时测试脚本仅在被忽略的 `.build-tools/` 中。

- 像素精修：视频与音频底栏统一为 36 px，28×28 px 按钮上下各留 4 px；`Snap KF` 与 `XAudio2` 控件边界实测相隔 10 px。
- 普通、图标及勾选菜单项统一为 30 px 行高、13 px 字号，前置 SVG 为 16×16 px，勾选框为 14×14 px。Windows 96 DPI 特写验证通过，Qt 目标和双轨全量构建均退出 0。

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
| 9B | `ae2cd9d97` | **M1 终章**：样式管理器（`StylesViewModel` + `StylesWindow.qml`：增删复制/改名联动字幕行、字体字号颜色对齐边距、实时预览）+ 主窗口竖直 `SplitView` + 紧凑菜单栏（28px + 快捷键提示列，`ShortcutMenuItem.qml`）+ 24px 底部状态栏（行数/选区/编码/版本） |
| 10A | `3e3739f34` | **M2a 视频预览**：`av_qt_core` 静态库（原版 ffms2 / Dummy 视频 provider + libass 字幕 provider + wx 隔离桥 `video_bridge`）+ `VideoViewModel` / `VideoViewportItem` / `VideoPanel.qml`：加载真实视频或 Dummy 视频 → libass 实时渲染当前 `AssFile` → 逐帧定位 / 拖动 / 播放 → 编辑与撤销实时重绘画面 |
| 10B | 本次工作区改动（未提交） | **M2b 音频波形（M2 收官）**：`av_qt_core` 增加原版 ffms2 音频 provider 与 XAudio2 player；新增 `audio_bridge`（解码→16bit mono→RAM 缓存、峰值提取、XAudio2/静音降级、区间播放）+ `AudioViewModel` / `AudioWaveformItem` / `AudioPanel.qml`（Aegisub 式波形打轴）+ 四区工作区（视频/波形/网格/EditBox）|
| 11A | 本次工作区改动（未提交） | **M3a 音画同步 + Shift Times + `.srt` 读写**：`VideoViewModel`↔`AudioViewModel` 联动（开视频自动加载音轨、以音频时钟推进视频帧、暂停/拖动/单帧/关闭双向同步停止、`Play Line` 独立）；新增 `ShiftTimesViewModel` + `ShiftTimesDialog.qml`（Time/Frames、Forward/Backward、All/Selected/Selection onward、Start&End/Start/End，单次 `COMMIT_DIAG_TIME` 提交，`Ctrl+I`）；`DocumentViewModel` 按后缀分派原版 `SRTSubtitleFormat` 读写（shim 补齐 4 个静态 helper），`FileDialog` 增加 SRT 过滤器 |
| 11B | 本次工作区改动（未提交） | **M3b 纯 Qt 多轨选轨 + 关键帧显示/吸附**：`ffmpegsource_common_qt.cpp` 替换原版 `ffmpegsource_common.cpp` 编进 `av_qt_core`，用可注入 `TrackSelector` 替换 `wxGetSingleChoiceIndex`（多轨零 wx 弹窗）；`VideoBridge/AudioBridge` 增加 `ProbeVideoTracks/ProbeAudioTracks` 与 `preferredTrack`，`VideoViewModel/AudioViewModel` 两段式 `trackSelectionRequired` → `TrackPickerDialog.qml` → `openLocalPathWithTrack`（命令行自动第一条、音频菜单 `Select Audio Track...`）；`VideoBridge` 暴露关键帧时间，`VideoViewModel` 提供 `keyframeTimesMs/keyframeCount/currentFrameIsKeyframe` 并接线给 `AudioViewModel`，波形绘制淡紫关键帧线 + ±6px 吸附（`Snap KF` 开关、`Shift` 临时禁用），`VideoPanel` 增加 `KF` 徽标 |

> **M1 里程碑（Stage 0–9B）**：Qt6/QML 前端已具备与 wx 版并行的日常编辑能力——真实 `.ass` 读写、统一提交/撤销总线、行操作与多选批量、EditBox 全字段与键盘流、查找替换、样式管理器，以及桌面级工作区（紧凑菜单/可拖拽分区/状态栏）。原版 wx 构建同步保持可用（双轨制）。

> **M2a 里程碑（Stage 10A）**：视频预览闭环打通——`Video` 菜单可开真实视频（ffms2）或 Dummy 视频，libass 把当前脚本实时渲染到视频帧上；切行自动 seek、逐帧步进/点时间轴/播放暂停可用，EditBox 改字与 `Ctrl+Z` 撤销都会立即反映到画面上；无视频时视频区完全隐藏，M1 布局不变。

> **M2 里程碑（M2a + M2b，Stage 10A–10B）**：桌面四区工作区完成——左上视频（libass 实时字幕）、右上音频波形（Aegisub 式打轴：左键/右键/拖拽红蓝线直接改 Start/End，单次 Commit + 一次 Undo，网格/EditBox/视频同步）、中间字幕网格、底部 EditBox；音频用 ffms2 解码 + 原版 XAudio2 player 区间播放（含播放头），无音频设备时降级为墙钟静音播放。视频与音频各自可开可关，全部关闭时回到 M1 纯文本布局。

> **M3a 里程碑（Stage 11A）**：音画同步播放与格式扩展——打开真实视频会自动把同一文件的音轨载入波形面板（无音轨静默跳过，不弹错误条）；视频播放改用**音频播放位置作为主时钟**推进帧号（长时间播放不漂移），暂停/拖动时间轴/单帧步进/关闭视频/关闭音频都会让两侧同时停止，`Play Line` 保持独立（抢占音频设备时只暂停视频，播完自动停）；新增 `Subtitle → Shift Times...`（`Ctrl+I`）桌面窗口（按时间或按帧、正向/反向、全部/选中/选区往后、只改开始/只改结束/两者，单次 `AssFile::Commit` 一次撤销）；`DocumentViewModel` 支持 `.srt`（SubRip）读写（复用原版 `SRTSubtitleFormat`，UTF-8 写出，`.ass`/`.ssa`/`.srt` 互转），FileDialog 与命令行均可用。

> **M3b 里程碑（Stage 11B，路线 A 收尾）**：彻底移除打开多轨音视频时的 wx 选轨弹窗——`av_qt_core` 用 Qt 副本 `ffmpegsource_common_qt.cpp` 替换原版，`AskForTrackSelection()` 改为注入式 C++ 回调；打开文件先轻量 probe 轨道，多轨且是交互入口时弹纯 QML `TrackPickerDialog`（视频/音频两种标题），选完以指定 track id 打开，命令行与自动加载则自动选第一条、永不弹窗；`Audio` 菜单新增 `Select Audio Track...` 可随时重选。视频关键帧接入音频波形：`VideoProvider::GetKeyFrames()` → `TimeAtFrame(START)` 毫秒表 → 波形绘制 1px 淡紫竖线（视口内二分查找，只画可见的）→ 拖拽/点击 Start/End 时在 ±6px 容差内吸附到最近关键帧（`Snap KF` 开关、按住 `Shift` 临时禁用），`VideoPanel` 在关键帧帧号旁显示 `KF` 徽标；吸附提交仍走 `AssFile::Commit`，一次 `Ctrl+Z` 撤销。

## 2. 目录结构

```
src/qt_ui/
├── main.cpp                  # 入口：初始化 core/libass → 建 AssFile → 建 VM → 注入 QML（--video 在 QML 后打开）
├── aegisub_core.{h,cpp}      # libaegisub 子系统初始化（dispatch/log/Path/Options）、FlushOptions
├── commit_bridge.{h,cpp}     # wx 隔离层：Commit/AddUndoManager 的 std::string 版（.cpp 编进 ass_qt_core）
├── subtitle_format_shim.cpp  # 方案 B：补 SubtitleFormat 的 ctor/CanReadFile/CanWriteFile/CanSave + SRT 写出用到的 4 个静态 helper
├── video_bridge.{h,cpp}      # wx 隔离层：建 ffms2/Dummy VideoProvider、libass SubtitlesProvider，取帧+叠加渲染；轨道 probe/preferred track、关键帧毫秒表（编进 av_qt_core）
├── audio_bridge.{h,cpp}      # wx 隔离层：ffms2 音频解码→S16 mono→RAM 缓存、波形峰值、XAudio2 区间播放/静音降级；轨道 probe/preferred track（编进 av_qt_core）
├── ffmpegsource_common_qt.{h,cpp} # wx 隔离：src/ffmpegsource_common.cpp 的 Qt 副本，AskForTrackSelection 改为注入式 TrackSelector + ProbeTracks（编进 av_qt_core）
├── media_track_info.h        # 纯 C++ 轨道信息（id + codec 名），bridge 与 VM 共用
├── color_matrix.cpp          # 从 video_provider_manager.cpp 提取的 4 个 ColorMatrix::* 函数（编进 av_qt_core）
├── qt_background_runner.{h,cpp} # 同步版 agi::BackgroundRunner / ProgressSink（编进 av_qt_core）
├── video_viewport_item.{h,cpp}  # QQuickPaintedItem，注册为 QML `VideoViewport`（黑底、等比居中、直接吃 QImage）
├── audio_waveform_item.{h,cpp}  # QQuickPaintedItem，注册为 QML `AudioWaveform`（标尺/波形/选区/红蓝线/播放头 + 鼠标打轴 + 滚轮缩放）
├── meson.build               # aegisub-qt 目标；ass_qt_core / av_qt_core 两个静态库（均需 wx PCH）
├── resources.qrc             # 全部 QML 注册到 qrc:/
├── Main.qml                  # 主窗口：MenuBar + 竖直 SplitView（视频/音频 | 网格 | EditBox）+ 错误提示条 + FileDialog
├── VideoPanel.qml            # 视频预览面板：VideoViewport + 34px 紧凑控制条（播放/步进/时间轴/帧号时间码/KF 徽标/Sync）
├── AudioPanel.qml            # 音频面板：AudioWaveform + 34px 控制条（Play Line/Stop/缩放/Snap KF/轨道标签/播放状态/音量；窄面板自动隐藏时间区间）
├── TrackPickerDialog.qml     # 模态选轨窗口：Select Video/Audio Track、轨道列表（Track N — codec）、Open/Cancel、Esc/双击
├── SubtitleGrid.qml          # 表格视图（ListView + RowLayout delegate、选中高亮）
├── EditBox.qml               # 底部编辑框：开始/结束时间 + 大字幕文本框
├── AboutWindow.qml           # 关于窗口（版本/版权/译员，来自 po/*.mo）
├── PreferencesWindow.qml     # 首选项（Backup 页，App/Auto/Save）
├── PropertiesWindow.qml      # 脚本配置（Title / PlayResX / PlayResY + 打印 ASS）
├── SearchDialog.qml          # 非模态查找/替换窗口（Find/Replace 复用同一实例）
├── StylesWindow.qml          # 非模态样式管理器（列表 + 表单 + 实时预览）
├── ShiftTimesDialog.qml      # 非模态时间平移窗口（Time/Frames、方向、范围、字段、Apply/Close）
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
    ├── video_vm.{h,cpp}             # 视频预览 VM：开视频/Dummy、轨道 probe 与选轨、取帧+libass 叠加、seek/步进/播放、关键帧属性、提交/撤销实时重绘
    ├── audio_vm.{h,cpp}             # 音频 VM：开音频/Dummy、轨道 probe 与选轨、波形视口（缩放/滚动/居中）、区间播放、波形打轴（单次 Commit）、关键帧吸附
    ├── shift_times_vm.{h,cpp}       # 时间平移 VM：按时间/按帧、方向、范围（全部/选中/选区往后）、字段（起/止/两者）、单次提交
    └── document_vm.{h,cpp}          # 文件路径/脏标记/错误 + 打开保存（AssParser/AssSubtitleFormat + SRTSubtitleFormat）
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
- **视频预览（Stage 10A）**：`av_qt_core` 静态库把原版**无 GUI** 的 provider 源文件（`ffmpegsource_common.cpp`、`video_provider_ffmpegsource.cpp`、`video_provider_dummy.cpp`、`video_provider_cache.cpp`、`colorspace.cpp`、`subtitles_provider.cpp`、`subtitles_provider_libass.cpp`、`subtitles_provider_csri.cpp`）编进 Qt 进程；`color_matrix.cpp` 只搬运 `video_provider_manager.cpp` 里 4 个无 wx 的 `ColorMatrix::*`，不链接带 wx 对话框的 manager。provider 需要 wx PCH，因此 `av_qt_core` 与 `ass_qt_core` 一样用 `cpp_pch: ../include/agi_pre.h`。
- **wx 隔离（`video_bridge`）**：Qt 翻译单元不能 include wx/`agi::signal` 头，`video_bridge.{h,cpp}` 对 Qt 侧只暴露纯 C++ 接口（`OpenFile` / `OpenDummy` / `LoadSubtitles` / `DecodeFrame` / `FrameData` …）。实现里，ffms2 走 `CreateFFmpegSourceVideoProvider` 再包 `CreateCacheVideoProvider`，Dummy 直接 `new DummyVideoProvider`，字幕用 `libass::Create` + `SubtitlesProvider::LoadSubtitles(AssFile*)`。`QtBackgroundRunner` 是同步版 `agi::BackgroundRunner`/`ProgressSink`（M2a 没有进度对话框），因此首次索引/字体缓存构建会阻塞 UI，见 §6。
- **帧格式守卫**：`DecodeFrame()` 在调 `SubtitlesProvider::DrawSubtitles()` **之前**检查 `frame.pitch != frame.width * 4`，有行 padding 就先紧凑重排（原版 libass blender 假定 `pitch == width*4`，不能改原文件）；`VideoViewModel` 再把 BGRA8 直接装进 `QImage::Format_ARGB32`（Windows 小端零转换），`frame.flipped` 时做一次垂直翻转。libass 会把叠加像素的 alpha 字节写成 0，转 QImage 后统一把 alpha 置 255，否则 `QPainter` 会把字幕画成透明洞。
- **实时字幕联动**：`VideoViewModel` 订阅 `AssFile::AddCommitListener`（100ms 防抖，避免逐字符输入时反复重建 libass track）、`UndoManager::restored`（延后一拍，等网格重建索引）和 `DocumentViewModel::documentReloaded`；有视频时每次都会 `videoVM → bridge.LoadSubtitles(file_)` 并重绘当前帧，所以 EditBox 改字、Styles Manager 改色/字号、Undo/Redo 都**立即**出现在画面上。`gridModel.activeRowChanged` 在 `hasVideo && autoSeekToActiveLine && !isPlaying` 时用 `FrameAtTime(start, agi::vfr::START)` seek（保证行首那一帧就可见），对应 wx 版 `video/jump/start` 的语义。
- **视频控制与渲染**：播放用 `QTimer` 按平均 FPS 起跳、但以墙钟推进（丢 tick 只跳帧不会变慢），到尾自动停；`VideoViewportItem`（`QQuickPaintedItem`，QML 名 `VideoViewport`）持有 `QImage`、黑底等比 letterbox，帧通过 `VideoViewModel::frameReady(QImage)` 直接推给它（不走 `QQuickImageProvider`，URL 缓存问题少且 `PrintWindow` 截图稳定）。`VideoPanel.qml` 底部 34px 控制条：Play/Pause、`◀`/`▶` 单帧、`0..frameCount-1` 时间轴、`Frame N / total  H:MM:SS.CC`、`Sync`（绑 `autoSeekToActiveLine`）。
- **视频菜单/命令行**：`Video` 菜单位于 `Subtitle` 与 `Audio` 之间：Open Video...、Use Dummy Video、Close Video、Jump to Active Line（`Ctrl+J`）。VideoPanel 与 AudioPanel 并排放在顶部水平 `SplitView` 里，`visible` 各绑自己的 `hasVideo`/`hasAudio`（任一隐藏时另一侧自动铺满，全部关闭时整个媒体区折叠回 M1 布局），媒体区默认 preferred 300px / min 180。命令行 `aegisub-qt.exe [file.ass] [--video <path>] [--audio <path>]`；`--video` 在 `engine.load()` **之后**才打开（否则首帧 `frameReady` 早于 QML 存在，视频区会一直黑到下一次帧变化），`--audio` 同理。
- **音频管线（Stage 10B）**：`av_qt_core` 再编入原版 `audio_provider_ffmpegsource.cpp` + `audio_player_xaudio2.cpp`（不链接带 wx 对话框的 `audio_provider_factory.cpp`）。`audio_bridge.{h,cpp}` 对 Qt 侧只暴露纯 C++ 接口：`CreateFFmpegSourceAudioProvider(path, QtBackgroundRunner*)` → `CreateConvertAudioProvider`（保证 S16 mono，<32k 采样率线性插值倍增）→ 按 `Audio/Cache/Type` 选 `CreateRAMAudioProvider`（默认，后台线程全量解码）/`CreateLockAudioProvider`；播放用 `CreateXAudio2Player(provider, nullptr)`（工厂的 `wxWindow*` 参数不使用）。音频与视频共用同一份 ffms2 索引缓存（`Video/Open Audio` 默认开、`Index All Tracks` 默认开，打开视频时索引已含音轨）。
- **波形与打轴**：`AudioBridge::ComputePeaks(start_sample, end_sample, width)` 一次整段读取可见范围内的 `GetInt16MonoAudio`（S16 → `[-1,1]` 峰值；可见范围外的列保持 0，不会把波形拉伸铺满视口），`AudioWaveformItem` 缓存峰值并在视口/缩放变化时重算。左键点/拖设置 Start、右键点/拖设置 End，靠近红/蓝线（±6px）按左键可抓取拖动；拖拽期间只画预览线，**松手才调用 `AudioViewModel::commitLineTiming()`** 写入 `AssDialogue::Start/End` 并 `commit("Set line timing", COMMIT_DIAG_TIME, -1, line)` → 一次手势 = 一条 Undo；网格/EditBox/视频字幕/波形全部经同一条 `AssFile::Commit` 总线同步。`Ctrl+滚轮` 缩放（20–800 px/s），普通滚轮水平滚动。
- **音频播放**：`AudioViewModel::playSelection()` 用活动行 `[Start, End]`（end<=start 时播 500ms）调 `AudioBridge::PlayRange`；16ms `QTimer` 把 `AudioPlayer::GetCurrentPosition()` 换算成 `playbackTimeMs` 驱动白色播放头，播放器自然结束时自动停。XAudio2 初始化/播放失败（无音频设备的无头环境）会捕获异常并降级为**墙钟静音播放**（播放头照常推进、打轴交互不受影响），`playerStatus` 属性与日志记录原因，绝不崩溃。
- **音频菜单**：位于 `Video` 与 `Tools` 之间：Open Audio...、Open Audio from Video（有真实视频路径时可用）、Use Dummy Audio、**Select Audio Track...**（Stage 11B：多轨时弹 Qt 选轨，单轨静默重开）、Close Audio、Play Line（`Ctrl+Space`）、Stop。`Use Dummy Audio` 不用原版 `DummyAudioProvider`——它只支持 silence/noise 且忽略 `ln` 参数（报告 2.5 小时），Qt 桥自己合成 10 秒 440 Hz 正弦（带慢包络）用于无素材环境验证。
- **音画同步（Stage 11A）**：`main.cpp` 在 `VideoViewModel` / `AudioViewModel` 构造后调用 `videoVM.setAudioViewModel(&audioVM)` 接线。`VideoViewModel::openLocalPath()` 成功打开真实视频后调用 `AudioViewModel::openLocalPathQuiet()`（已有音频则跳过；无音轨/打开失败只写 debug 日志，**不碰错误条**）；`togglePlay()` 时若音频已打开则 `audioVM->playFrom(currentTimeMs())`，播放期间 `onPlaybackTick()` 以 `audioVM->playbackTimeMs()` 换算帧号（内部 `audio_sync_` 标记），没有音频时仍走原墙钟推进；`stopPlayback()` 会同时停音频；所有手动定位（`seekFrame`，含点/拖视频时间轴、`seekToTimeMs`）先 `stopPlayback()` 再定位；`closeVideo()` 也统一走 `stopPlayback()`；音频被关闭/替换时 `VideoViewModel::onAudioAvailabilityChanged()` 停掉视频；`AudioViewModel::playSelection()`（Play Line）先发 `linePlaybackStarted()`，视频收到只暂停、不启动自己的播放，因此两者不会互相抢音频设备。`AudioViewModel::playFrom()` 是整段播放（`playing_selection_ = false`，播放头不被活动行选区钳制），`playSelection()` 仍按活动行 `[Start,End]` 播并在结尾自动停。
- **Shift Times（Stage 11A）**：`ShiftTimesViewModel` 持有 `AssFile*` / `SubtitleGridModel*` / `VideoViewModel*`。目标行从网格行索引按文件顺序取：0=全部行、1=`sortedSelectedRows()`、2=首个选中行（无选中时 `activeRow`）到末尾（对齐原版 `mode==2`，包含未选中的行）。时间模式用 `agi::Time` 解析 `shiftTime` 并加/减毫秒；帧模式用 `VideoViewModel::frameAtTimeMs(ms, end)` / `timeAtFrameMs(frame, end)`（`end` 选择 `agi::vfr::END` 语义——为此 `VideoBridge` 补了 `FrameAtTimeEnd` / `TimeAtFrameStart` / `TimeAtFrameEnd`）；Start 用 START 语义、End 用 END 语义，与原版 `DialogShiftTimes::Shift()` 一致。结果钳制为 `>= 0` 且 `End >= Start`（只改一个字段时也不越界）；只统计真正变化的行，变化 >0 时**单次** `aegisub_qt::commit(file_, "shifting", AssFile::COMMIT_DIAG_TIME, -1, nullptr)`，一次 `Ctrl+Z` 整体撤销，网格/EditBox/视频字幕/波形经同一条提交总线刷新。`Ctrl+I` / `Subtitle → Shift Times...` 打开非模态窗口（`Main.qml::openShiftTimes()`，单实例复用）。
- **`.srt` 读写（Stage 11A）**：`src/subtitle_format_srt.cpp` 只依赖 `SubtitleFormat` 基类 + `TextFileReader/Writer`（**零 wx GUI 依赖**），因此直接编进 `ass_qt_core`；但它的 `WriteFile()` 还会调用 `SubtitleFormat::{ConvertNewlines,StripComments,RecombineOverlaps,MergeIdentical}`，这 4 个静态成员原本定义在**不链接**的 `subtitle_format.cpp` 里（该文件带 wx 对话框与全部兄弟格式），因此 `src/qt_ui/subtitle_format_shim.cpp` 按原文补齐（都是纯 `AssFile` 操作，无 wx）。`DocumentViewModel::loadFromPath()` / `saveToPath()` 按**目标后缀（大小写不敏感）**分派 `SRTSubtitleFormat` 或 `AssSubtitleFormat`：`.srt` 读进临时 `AssFile`（`ReadFile` 自己 `LoadDefault(false, catalog)` 建好 `[Script Info]`/`Default` 样式，空文件只补一条默认行，不能再调 `LoadDefault(true)` 否则重复）；保存时的 `markSaved`/编码/历史基线等收尾两种格式共用。FileDialog 过滤器新增 `Subtitle files (*.ass *.ssa *.srt)` 与 `SRT subtitles (*.srt)`，命令行首参同样按后缀分派。
- **纯 Qt 多轨选轨（Stage 11B）**：`src/ffmpegsource_common.cpp` 里唯一的 GUI 依赖是 `AskForTrackSelection()` 的 `wxGetSingleChoiceIndex`；Qt 进程没有 wxApp，多轨文件会弹 wx 对话框甚至崩。Stage 11B 在 `src/qt_ui/ffmpegsource_common_qt.cpp` 放了一份该文件（含 `DoIndexing`/`GetTracksOfType`/`GetCacheFilename`/`CleanCache` 等）的 Qt 副本并让它**替换** `av_qt_core` 里的原文件：`AskForTrackSelection()` 改为调用 `aegisub_qt::SetTrackSelector()` 注入的 `std::function<int(MediaKind, const std::vector<MediaTrackInfo>&)>`；没有 selector 时回退第一条轨道，永不弹窗。`VideoBridge`/`AudioBridge` 在创建 provider 前用 RAII `TrackSelectorGuard` 安装"返回指定 track id（或自动第一条）"的 selector。选轨是**两段式**：`ProbeVideoTracks()`/`ProbeAudioTracks()`（`FFMS_CreateIndexer` + `GetTracksOfType` + `FFMS_CancelIndexing`，只读容器头、不写索引）→ 交互入口（`Video → Open Video...`、`Audio → Open Audio...`、`Audio → Select Audio Track...`）遇到多轨时发 `trackSelectionRequired(path, kind, tracks)` → QML `TrackPickerDialog` 列出 `Track N — codec` → 选定后 `openLocalPathWithTrack(path, id)` 以该 id 打开；命令行 `--video/--audio`、开视频自动加载音轨（`openLocalPathQuiet()`）走"自动第一条"，绝不弹窗。取消只清 pending，不影响已打开的媒体。
- **关键帧显示与吸附（Stage 11B）**：`VideoBridge::OpenFile()` 后用 `VideoProvider::GetKeyFrames()` 取关键帧帧号，经 `TimeAtFrame(frame, agi::vfr::START)` 换算毫秒、钳到 `>= 0`、升序去重，存进 `GetKeyframeTimesMs()`（Dummy 视频的 `GetKeyFrames()` 天然为空）。`VideoViewModel` 暴露 `keyframeTimesMs`（QVariantList）、`keyframeCount`、`currentFrameIsKeyframe`（用同一 `TimeAtFrameStart` 在毫秒表里二分），打开/关闭视频时发 `keyframeTimesChanged()`；`main.cpp` 把它接到 `AudioViewModel::setKeyframeTimesMs()`。`AudioWaveformItem` 在波形柱之后、红蓝线/播放头之前画 1px `#b388ff` α≈0.55 竖线，用 `std::lower_bound` 只遍历视口内 `[viewStartMs, viewStart+visibleMs]` 的关键帧；`AudioViewModel::nearestKeyframeMs()` 也是二分。点击/拖拽 Start/End 时，若 `snapToKeyframes` 为真（默认）且没按 `Shift`，把鼠标毫秒吸附到 `±6px 对应的毫秒容差`内最近的关键帧（容差 = `ceil(6 * 1000 / zoomPxPerSec)`），拖拽预览线就会精确贴合紫线，松手照旧单次 `commitLineTiming()` → 一次 Undo。`AudioPanel` 的 `Snap KF` 复选框在有关键帧时可用；`VideoPanel` 在帧号槽位左侧显示 `KF` 徽标（非关键帧时文本为空，不占额外宽度）。
- **菜单/文件对话框**：`Main.qml` 的 `menuBar` 提供 File（New/Open/Save/Save As/Exit）、Edit（Undo/Redo/Find/Replace）、Subtitle（行操作/样式/属性）、Video（Open/Dummy/Close/Jump）、Audio（Open/Open from Video/Dummy/Close/Play Line/Stop）、Tools（Preferences）、Help（About）；`QtQuick.Dialogs` 的 `FileDialog` 负责字幕后缀、视频文件与音频文件选择。
- **命令行**：`aegisub-qt.exe [file.ass] [--video <path>] [--audio <path>]`，字幕参数存在且是文件则启动即打开，否则 `newDocument()`；`--video`/`--audio` 指向的现有文件在 QML 就绪后打开。

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

**Stage 10A/10B 的构建变化**：`src/qt_ui/meson.build` 新增 `av_qt_core` 静态库（视频/字幕/音频 provider 原件 + XAudio2 player + wx 隔离桥），`aegisub-qt` 改为 `link_with: [libaegisub, ass_qt_core, av_qt_core]`；根 `meson.build` 不需要改——`aegisub-qt` 本来就从根 `deps` 继承 `ffms2`/`libass`/`libav*` 的依赖与链接（`build.ninja` 里能看到 `libffms2.a`、`libass.a`、`libav*.a`），**也早就链接了 `xaudio2.lib`/`winmm.lib`/`ole32.lib`/`uuid.lib`/`dsound.lib`**，所以音频后端零新增依赖、不需要安装 Qt Multimedia。视频解码/字幕渲染/音频播放都是静态链接或系统 DLL，部署目录不需要额外 DLL。

**Stage 11A 的构建变化**：`ass_qt_core` 增加原版 `../subtitle_format_srt.cpp`（SRT 解析器），`aegisub-qt` 增加 `viewmodels/shift_times_vm.cpp` 与对应 moc 头；`resources.qrc` 增加 `ShiftTimesDialog.qml`。无新增第三方依赖（SRT 只用到已有的 `TextFileReader/Writer` 与 boost），wx 目标不受影响。

**Stage 11B 的构建变化**：`av_qt_core` 用 `src/qt_ui/ffmpegsource_common_qt.cpp` **替换** `../ffmpegsource_common.cpp`（同一个 `FFmpegSourceProvider` 实现，只换掉 wx 选轨对话框；原版文件保持不动，供 `aegisub.exe` 使用）；`resources.qrc` 增加 `TrackPickerDialog.qml`。无新增运行期依赖（轨道 probe 与关键帧都用已有的 ffms2 API）。测试素材生成额外用了一个放在 `.build-tools/stage11b/assets/py/imageio_ffmpeg` 的 ffmpeg 可执行文件（仅生成 `.mkv` 素材，不入库、不参与构建）。

**部署注意**：exe 目录 `build/src/qt_ui` 里有一份 windeployqt 的 Qt。使用 `QtQuick.Dialogs` 需要把 Qt 安装目录的 `qml/QtQuick/Dialogs`（含 `quickimpl` 全部文件）以及 `Qt6QuickDialogs2.dll`、`Qt6QuickDialogs2Utils.dll`、`Qt6QuickDialogs2QuickImpl.dll` 拷进去（本阶段已补齐）。

调试用环境变量：

| 变量 | 作用 |
|---|---|
| `AEGISUB_QT_LANGUAGE=zh_CN` | 强制翻译 catalog（默认跟随系统区域；本机 zh-HK 无对应 po，会回退英文） |
| `AEGISUB_QT_CONFIG_DIR=<dir>` | 覆盖 `?user`，沙箱下同时覆盖 `?local`（ffms2 索引缓存也落在该目录），避免污染真实 `%APPDATA%\Aegisub`，也用于自动化测试 |
| `AEGISUB_QT_TRACE=1` | 把启动阶段写入 exe 旁 `aegisub_qt_core.log` |
| `AEGISUB_QT_FFMS_DEBUG=1` | 强制 ffms2/libavformat 的 debug 日志（打印到 stderr，用于排查多轨探测/索引/关键帧问题；默认 quiet） |
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
30. **原版视频 provider 把 wx 也带进来了**：`video_provider_ffmpegsource.cpp`/`ffmpegsource_common.cpp` 里有多轨选择用的 `wxGetSingleChoiceIndex`，`subtitles_provider_libass.cpp` 有 `wx/intl.h`/`wxThread::IsMain()`；`av_qt_core` 用 wx PCH 编译这些原件即可（Qt 进程已链接 `libwxmono.a`）。但**多视频轨文件会弹 wx 对话框**（Qt 进程没有 wxApp），M2a 先接受（测试文件都是单轨），后续应在 `video_bridge` 里换掉 AskForTrackSelection。
31. **ffms2 索引缓存目录**：`GetCacheFilename()` 用 `agi::fs::CreateDirectory`，本环境下可能 fail-fast（见坑 5）。`VideoViewModel::openLocalPath()` 先按 `?local/ffms2cache` 用 `QDir::mkpath` 把目录建好，provider 看到的永远是已存在目录；`AEGISUB_QT_CONFIG_DIR` 也会把 `?local` 一起关进沙箱目录。
32. **libass blender 假定 `pitch == width * 4`**：`SubtitlesProvider::DrawSubtitles()` 直接用 `frame.data.data()` 和 `width*4` 当 stride，忽略 `VideoFrame::pitch`。ffms2 的 BGRA 输出可能带行 padding，所以 `VideoBridge::DecodeFrame()` 必须在调 draw **前**重排成紧凑缓冲（不能改原文件）。
33. **libass 会把叠加像素的 alpha 写成 0**：转 `QImage::Format_ARGB32` 时若原样保留，`QPainter` 会把字幕区域当透明，预览里出现「黑洞」。`VideoViewModel::renderCurrentFrame()` 逐像素把 alpha 置 `0xFF`。
34. **命令行打开的视频必须在 `engine.load()` 之后**：`VideoViewModel::openLocalPath()` 会同步取第一帧并发 `frameReady`；若在 QML 引擎加载前打开，`VideoPanel` 还没订阅信号，视频区会一直黑到下一次帧变化。`main.cpp` 把 `--video` 的处理挪到 `engine.load()` + `rootObjects` 检查之后。
35. **本机部署的 Material ToolTip 会打 QML 警告**：视频面板按钮挂了 `ToolTip.visible: hovered` 后，首次 hover 会打印 `QQmlComponent: Component is not ready`（部署目录的 ToolTip 组件加载失败，但不影响功能）。M2a 直接把 ToolTip 去掉，保持 stderr 干净；要恢复提示需先补齐/排查部署的 Qt Quick Controls 文件。
36. **原版 `DummyAudioProvider` 不能用来做波形测试**：它只支持 `silence`/`noise`（文档里的 `sine` 没实现），并且完全忽略 `ln` 参数，永远报告 2.5 小时长度。`AudioBridge::OpenDummy()` 自己实现 10 秒 440 Hz 正弦（带慢包络）的 `SyntheticSineProvider`，长度/内容都可控。
37. **XAudio2 player 的工厂签名带 `wxWindow*`（实际不用）**：`src/audio_player_xaudio2.cpp` 除这个参数外不依赖 wx；手动 `extern std::unique_ptr<AudioPlayer> CreateXAudio2Player(agi::AudioProvider*, wxWindow*)` 后传 `nullptr` 即可。另外 `AudioPlayerOpenError` 定义在**全局命名空间**（`src/include/aegisub/audio_player.h`），不是 `agi::`，catch 错会 C2039。
38. **没有音频设备时要降级而不是崩**：无头/沙箱环境下 `CreateXAudio2Player` 会抛 `AudioPlayerOpenError`（或播放时源语音失败）。`AudioBridge` 捕获后置 `UsingSilentFallback()`，用 `std::chrono` 墙钟推进播放头，打轴/波形功能照常。`AudioPlayer::GetCurrentPosition()` 在未播放时返回 0，所以 VM 必须先判 `IsPlaying()` 再取位置。
39. **波形峰值映射不能把 `end` clamp 到音频总样本数再均分列**：那样请求范围超出音频末尾时会把整段波形拉伸铺满视口（实测 3 秒 WAV 显示成 9 秒波形）。`ComputePeaks` 按**请求的时间范围**映射列，只有落在 `[0, num_samples)` 内的列读样本，其余列保持 0。
40. **Qt 6.6 的 `QQuickItem` 尺寸变化虚函数叫 `geometryChange(new, old)`**，不是 `geometryChanged`；写错 override 会报 C3668 且 moc 生成的代码一起失败（`AudioWaveformItem` 踩过）。
41. **拖拽打轴只在 mouseRelease 提交**：`mousePress`/`mouseMove` 只改预览线并 `update()`，松手时才 `AudioViewModel::commitLineTiming()`；否则每个 mousemove 都会产生一条 Undo 记录，撤销粒度完全失效。
42. **媒体区用「invisible + 动态 fillWidth」实现自适应**：`SplitView` 会忽略 `visible: false` 的子项，所以 VideoPanel/AudioPanel 各自绑自己的 visible；视频面板 `SplitView.fillWidth: !audioVM.hasAudio`，音频面板恒 `fillWidth: true`——单个可见时铺满整行，两者都关时整个媒体 SplitView 折叠回 M1 布局。
43. **QML `Shortcut` 无法区分 `Ctrl+S` 与 `Ctrl+Shift+S`**：对字母键，Qt 的 shortcut 匹配不把 Shift 当作区分条件，两条 `Shortcut`（`StandardKey.Save` / `StandardKey.SaveAs`）同时注册时**两条都不触发**（实测按下 Ctrl+Shift+S 无任何反应）。现在改由 `main.cpp` 的 `FileShortcutFilter` 在应用级过滤器里先吃 `ShortcutOverride`、再在 `KeyPress` 里按 Shift 分派，并通过 `QMetaObject::invokeMethod(root, "saveDocument"/"openSaveAsDialog")` 调 QML 函数（`Ctrl+Z/Y` 的 `UndoShortcutFilter` 是同一套路）。
44. **SRT 写出的 4 个静态 helper 在未链接的 `subtitle_format.cpp` 里**：只把 `subtitle_format_srt.cpp` 编进 `ass_qt_core` 会在 `SRTSubtitleFormat::WriteFile()` 处 LNK2019（`ConvertNewlines`/`StripComments`/`RecombineOverlaps`/`MergeIdentical`）。按方案 B 原文复制进 `src/qt_ui/subtitle_format_shim.cpp` 即可（它们只操作 `AssFile`，无 wx）。注意源码里的字面量是 `"\\h"`/`"\\n"`/`"\\N"`（C++ 字符串 `\h`/`\n`/`\N`，`boost::ireplace_all` 大小写不敏感所以 `\N` 也变真换行）——从文档/日志里抄代码时容易多转义一层。
45. **`TextField.textEdited` 不响应程序化赋值**：自动化（UIA `ValuePattern.SetValue`）或代码设置 `text` 只触发 `textChanged`，不触发 `textEdited`，于是 VM 永远拿不到新值（Shift Times 的 Apply 报 `No lines changed` / `Invalid time`）。`ShiftTimesDialog.qml` 的时间输入框改用 `onTextChanged` + `if (vm !== text)` 守卫（`loadForm()` 回填时不会反向写坏 VM）。这类「用户输入」信号在自动化里都不可靠，优先用 `onTextChanged` 并做值比较。
46. **XAudio2 失败时 `playFrom()` 仍返回 true（静音降级）**：`AudioBridge::PlayRange()` 在 XAudio2 初始化/播放失败时切换到墙钟「静音播放」并返回 true，`AudioViewModel::playFrom()` 因此也返回 true——音画同步这套逻辑照样按音频时钟推进（只是播放头来自墙钟、没有声音），状态栏会显示 `Silent playback (...)` 原因。所以「音频时钟驱动视频」不依赖真实声卡，但验证「可听声音」必须确认状态栏是 `XAudio2`；本机 Stage 11A 自动化运行期间 XAudio2 设备不可用（连 Stage 10B 的 WAV 也降级），E2E 用播放头推进与 `Stop` 可用性验证传输状态（见 §7）。
47. **Shift Times 的时间解析**：`agi::Time(std::string)` 的构造**不抛异常**（垃圾字符串静默变成 0），所以 VM 先用正则 `^[0-9]+(?::[0-9]+){0,2}(?:[.,][0-9]+)?$` 过滤空串/非法输入再交给 `agi::Time`；`int(agi::Time)` 仍是厘秒取整，与原版 wx 的隐式转换一致（亚厘秒会丢）。时间/帧加减用 `long long` 中间量再钳回 `int`，避免极端值溢出。
48. **多轨选轨绕不开原版 provider，但不能改它**：`video_provider_ffmpegsource.cpp` / `audio_provider_ffmpegsource.cpp` 的构造函数在 `TrackList.size() > 1` 时调用 `FFmpegSourceProvider::AskForTrackSelection()`，原实现是 `wxGetSingleChoiceIndex`（Qt 进程没有 wxApp，多轨文件会弹 wx 对话框或崩）。Stage 11B 的做法是**整文件替换**：把 `src/ffmpegsource_common.cpp` 复制成 `src/qt_ui/ffmpegsource_common_qt.cpp` 编进 `av_qt_core`（不编原文件、不改 `src/`），`AskForTrackSelection()` 改成查 `aegisub_qt::SetTrackSelector()` 注入的 `std::function`（无 selector 时回退第一条）。bridge 在创建 provider 前用 RAII guard 装好"返回指定 track id / 第一条"的 selector。不要尝试给原类加成员（`ffmpegsource_common.h` 在 `src/` 禁改）。
49. **两段式选轨（probe → QML picker → 指定 track 打开）**：先 `ProbeTracks()`（`FFMS_CreateIndexer` + `GetTracksOfType` + `FFMS_CancelIndexing`，不写索引）拿轨道列表；多轨且是交互入口时 VM 发 `trackSelectionRequired(path, kind, tracks)`，QML 弹 `TrackPickerDialog`；用户点 Open 后 `openLocalPathWithTrack(path, id)`，provider 构造期间 selector 直接返回该 id、不再询问。命令行 `--video/--audio` 与 `openLocalPathQuiet()`（开视频自动带音轨）走"自动第一条"，绝不弹窗；多轨取消只清 pending，保留当前媒体。
50. **probe 成本与索引缓存**：每次 open 都会多做一次 `FFMS_Indexer` 创建（只读容器头，开销很小）；真正的索引只在首次打开时由 `DoIndexing` 写入 `?local/ffms2cache/*.ffindex`，之后 probe/open 都读缓存。`AEGISUB_QT_CONFIG_DIR` 会同时隔离 `?local`（含 ffms2 缓存），自动化测试注意别复用旧缓存测不同文件。
51. **关键帧毫秒用 `TimeAtFrame(frame, agi::vfr::START)`，不是帧起点**：START 语义给的是"从该时间开始的行在该帧可见"的时间点，CFR 下它约等于帧边界往前半帧（24fps 实测首帧 `-20.8ms`、之后 `979.2 / 1979.2 / …`），所以关键帧毫秒表是 `{0(钳零), 979, 1979, 2979, 3979}`。吸附到 2s 关键帧后 EditBox/网格显示 **`0:00:01.98`** 而不是 `0:00:02.00`——写测试要按真实值断言。`VideoViewModel::currentFrameIsKeyframe` 也必须用同一个 `TimeAtFrameStart` 去二分，混用 `TimeAtFrame` 会在 CFR/VFR 下错位。
52. **不可见 QML `Label` 仍可能留在 UIA 无障碍树里**：`visible: false` 的 Label 在 Windows UIA 里可能仍以 Text 元素出现（Stage 11B 的 `KF` 徽标踩过：非关键帧也检测到 `KF`）。要让 UIA 真正看不到，改成"始终存在但文本为空"（`text: cond ? "KF" : ""`），这样按 Name 精确匹配 `KF` 才可靠；固定槽位宽度还能避免控制条跳动。
53. **AVI 的稀疏 idx1 关键帧标志在 libavformat 里不可靠**：按 `avidec.c` 逻辑 idx1 的 `AVIIF_INDEX(0x10)` 位应映射成 `AV_PKT_FLAG_KEY`，但实测对未压缩 rawvideo AVI（即使 idx1 只有 0/24/… 带 0x10）ffms2 仍把 120 帧全当关键帧（波形上出现密集紫线）。测试素材改用 ffmpeg 生成的 **mpeg4 MKV**（`-g 24 -keyint_min 24 -sc_threshold 0 -bf 0`），I 帧精确落在 0/1/2/3/4s；真实 XviD/H.264 等有正常 I 帧的 AVI/MKV 不受影响。
54. **PowerShell 5.1 `Start-Process -PassThru -RedirectStandard*` 拿不到真实 ExitCode**：`Process.ExitCode` 恒为空（用 `cmd.exe` 最小复现确认），导致自动化里 `APP_EXIT=0` 的断言形同虚设。Stage 11B 的 E2E 改为 .NET `Process` + `ReadToEndAsync` 双流读取，退出后再读 `ExitCode` 并落盘 stdout/stderr。
55. **双面板下控制条空间很紧**：视频面板 460px 时 `Play/步进/时间轴/Frame 时间码/Sync` 本来就放不下（Stage 10A 起 `Sync` 在双面板布局里被裁掉，UIA 文本仍完整）；Stage 11B 的 `KF` 徽标放在帧号槽位内部、不额外占宽，音频条则在面板宽 <700px 时隐藏时间区间 Label、`<620px` 时隐藏轨道标签，把空间留给 `Snap KF`、播放状态和音量。改这些宽度时注意别把 `Play Line`/`Snap KF`/`XAudio2` 状态挤出可见区域（UIA 断言不依赖可见宽度，但人眼看得到）。
56. **后台 dispatch 线程的 C++ 异常会直接杀进程（S14 音视频打开闪退的根因）**。`libaegisub/common/dispatch.cpp` 的 `Queue::Async` 在 catch 后会用 `invoke_main([=]{ std::rethrow_exception(e); })` 把异常交回「主线程钩子」；wx 版 `src/main.cpp` 是把 thunk post 到主循环并在 `EVT_CALL_THUNK` 处理器里 `try/catch`，而 Qt 版最初写成直通 `[](agi::dispatch::Thunk f) { f(); }`（`aegisub_core.cpp`），于是后台任务里任何逃出的异常都会在原 worker 线程重抛 → `std::terminate` → `abort()` → Windows **`0xC0000409`**（实测 `ucrtbase.dll+0xA527E` = `abort+0x4E`，`ExceptionInformation[0]=7` = `FAST_FAIL_FATAL_APP_EXIT`；5 个 WER dump 全同签名，且都是 `ProcessIntegrityLevel=0x1000` 的受限令牌进程）。现已给 `invoke_main` 的 `f()` 包 `try/catch(agi::Exception/std::exception/...)`，异常只写 stderr；**不要**在 dispatch 任务里假设「异常会回到主线程」。
57. **日志 emitter 不能假设消息是 UTF-8**。`libaegisub/windows/log_win.cpp` 的 `EmitSTDOUT` 对每条日志都做 `charset::ConvertW(buff)`（`utf-8`→`utf-16le`、`enableSubst=false`），而日志并不保证是 UTF-8：`libaegisub/common/fs.cpp` 的 `CHECKED_CALL` 用 **POSIX errc 常量**（如 `permission_denied`=13）去 switch **Win32 错误码**（`ERROR_ACCESS_DENIED`=5），匹配不上时走 default 分支先 `LOG_D(...) << ec.message()`；本机（ACP 936）这条 `ec.message()` 是 GBK 的「存取被拒。」，转 UTF-8 时抛 `agi::charset::BadInput`，再配合坑 56 变成 0xC0000409 闪退。Qt 前端现在**不订阅 `EmitSTDOUT`**，改用 `aegisub_core.cpp` 里的 `StderrLogEmitter`：原始字节 `fprintf(stderr, ...)`、不转码、不抛异常；默认只输出 Exception/Assert/Warning（Info/Debug 太吵；但 **Warning 也会进 stderr**，例如无音频设备的 `Silent playback`，所以自动化断言 stderr 时不要假设「为空/无媒体告警」，应按关键字断言），`AEGISUB_QT_LOG_STDERR=0` 关闭全量、非 `0` 值开启全量。日志里要打平台错误信息时只打数值错误码，不要打本地化的 `ec.message()`。
58. **`?local` 必须由 Qt 侧无条件准备，且按三级回退**。`?local` 默认 `%LOCALAPPDATA%\Aegisub`（`libaegisub/windows/path_win.cpp`），ffms2 索引缓存写在 `?local/ffms2cache`；`FFmpegSourceProvider::GetCacheFilename()` 仍会尝试建该目录。受限令牌/只读配置/CI 下建目录失败会同时触发坑 56+57。现在 `aegisub_core.cpp` 启动时用 `QDir` 无条件准备 `?local` 与 `?local/ffms2cache`：`prepareDirectory()` 要求路径必须是目录（普通文件占位也会被判失败），写权限用**临时文件真实探测**（不要用 `QFileInfo::isWritable()`——Windows 下 `_waccess` 对目录只检查存在性，实测恒真），不可写时依次回退 exe 旁 `.qt_local`、`%TEMP%\Aegisub-qt\local`；`AEGISUB_QT_CONFIG_DIR` 模式只认 override 目录、不向目录外回退。`ffmpegsource_common_qt.cpp::GetCacheFilename()` 的建目录改为非抛的 `std::filesystem::create_directories(..., ec)`，失败只写数字错误码——缓存目录建不出来只意味着下次重新索引，绝不再崩。
59. **`FFMS_Init` 必须先于 `FFMS_CreateIndexer`**。Stage 11B 的 `ProbeTracks()` 原来先 `FFMS_CreateIndexer`、后面建 provider 时才 `FFMS_Init`（`ffmpegsource_common_qt.cpp`）。本版 ffms2 的 `FFMS_Init` 只做 `call_once { av_log_set_level(...) }`（`subprojects/ffms2/src/core/ffms.cpp`），暂时无害，但头文件契约要求它先跑（`ffms2.h`：「Pass 0 to both arguments」）；现已前移到 `FFMS_CreateIndexer` 之前。`ProbeTracks` 用 RAII `IndexerGuard` 保证所有路径都 `FFMS_CancelIndexing`，并整体 `try/catch`；`VideoBridge::LoadSubtitles`（libass 的 `LoadSubtitles` 会抛 `agi::InternalError`）与 `main.cpp` 的 CLI `--video/--audio` 打开也都补了 `try/catch(...)`——Qt 6.6 不会吞 `Q_INVOKABLE`/槽里逃出的异常（同样 terminate→abort）。
60. **P4 Unicode 路径窄化（已解决）**：Qt 原来漏掉了 [wx 的 Boost 路径初始化](../src/main.cpp#L124)。Windows 下 [InitializeCore](../src/qt_ui/aegisub_core.cpp#L143) 在启动 dispatch 前用 `boost::locale::generator().generate("en_US.UTF-8")` 给 `agi::fs::path`（`boost::filesystem::path`）做 `imbue`，只替换路径 `codecvt`，不改全局 C/C++ locale。字幕和媒体统一用 `QString::toStdWString()` 构造原生路径；探测、原版 provider 和 `.ffindex` 的 `.string()` 再统一输出 UTF-8。此版 [FFMS_Init](../subprojects/ffms2/src/core/ffms.cpp#L79) 的两个参数不参与编码，保留 `(0, 0)`；媒体由 libavformat 打开，索引由 [FFMS2 FileHandle](../subprojects/ffms2/src/core/filehandle.cpp#L31) 经 AVIO 读写，均走 [FFmpeg 的 Windows UTF-8 主路径](../subprojects/ffmpeg/libavutil/file_open.c#L43)，转换成宽串后用 `_wsopen`，不需要 8.3 短名。视频与自动音轨共用规范化绝对媒体路径，缓存键用该路径的 UTF-8 CRC、文件大小和修改时间；索引基名为 ASCII，缓存父目录可含中文。原版 provider 命中也会 `Touch()` 索引，mtime 变化不代表重建。`ComputeSpectrum()` 现在捕获 `agi::Exception`、`std::exception` 和未知异常，未计算列保留为零，绘制端也检查输出缓冲尺寸。


61. **`agi::Exception` 不继承 `std::exception`，GUI/退出边界的 catch 必须单列它**。`libaegisub/exception.h` 里 `agi::Exception` 是独立类（不是 `std::exception` 派生），所以 `catch (std::exception const &)` 接不住 `agi::fs::FileNotFound`/`FileSystemError`/`AccessDenied` 等。S14 复测在「安装目录只读 + `?user` 回退目录也不可写」时抓到第二个 `0xC0000409`：`aboutToQuit` → `FlushOptions` → `Options::Flush` → `io::Save(?user/config.json)`（`libaegisub/windows/fs.cpp` 的 `Save` 建目录/建文件失败）抛 `agi::fs::FileNotFound` → 原 `FlushOptions` 只 catch `std::exception` → 逃出 `aboutToQuit` → `std::terminate` → `abort()`。现已给 `FlushOptions` 补 `catch (agi::Exception const&)`/`catch (...)`，`main.cpp` 的 CLI `--video/--audio` catch 也补了 agi 分支。规则：所有从 Qt 事件循环/槽/`aboutToQuit`/QML `Q_INVOKABLE` 可达的边界，catch 列表必须含 `agi::Exception`（或直接用 `catch (...)` 兜底 + 记日志）。

### Stage 12：视频视觉工具（M3 路线 B）

- `VideoViewportItem::contentRect` 是实际绘制视频帧的等比居中矩形（不含黑边），尺寸或帧宽高比改变时重新计算。`VisualOverlayItem` 的绘制、八个裁剪手柄的命中、鼠标坐标与脚本坐标互转共享此矩形。`Std` 只显示悬停坐标；`Pos` 可从准星或画面开始定位；`Clip` 可拖角、拖边、框内平移，也可在已有框外拖出新框。无矩形标签时以整帧虚线框起步。
- `ass_tag_utils` 按原文区间编辑顶层覆写标签，不重写正文、中文或其他标签。锚点取显式 `\pos`、`\move` 起点，或按样式/行边距及 `\an`/`\a` 算出的静态位置。首次写静态 `\pos` 会移除与之冲突的顶层 `\move`，可撤销。`Clip` 只编辑普通矩形 `\clip`；不改矢量、反向 `\iclip` 或 `\t(...)` 内嵌裁剪，只有矢量或反向标签时不额外插入普通矩形。
- `VisualToolsViewModel` 使用 `COMMIT_DIAG_TEXT` 展开实时 EditBox/网格联动，通过 `commit_bridge` 的返回 ID 合并同一手势的撤销快照。切行、换工具、文档替换、Undo/Redo 或失去鼠标捕获会结束当前手势。`×` 是独立可撤销提交。视频预览在拖拽时用 16ms 合并刷新，松手补画最后状态；键入文字仍用 100ms 防抖。缺失的 `PlayRes` 按轴回退视频尺寸或 384×288，libass 预览使用同一分辨率而不改写文档头。
- `VideoPanel` 左侧新增 36px 工具栏；控制条使用紧凑播放按钮和独立 `KF` 槽位，窄面板在第二行显示坐标以保留 `Sync` 和时间轴。新增 `qt-ass-tag-utils` Meson 测试，无额外运行依赖。

**构建与实测（2026-11-24，`feature`，未提交）**：

- `bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe src/qt_ui/qt-ass-tag-utils-test.exe` → `EXIT 0`，Qt 与单测均链接成功；单测程序运行 `EXIT 0`。全量 `bash .build-tools/build_ninja.sh` → `EXIT 0`，原版 `aegisub.exe` 正常链接。日志：[Qt](../build/stage12-qt-final.log)、[双轨](../build/stage12-full.log)。
- Windows UI Automation + 实际窗口截图 **38/38**。`test_stage6_input.ass` + Dummy Video：默认准星未写标签；鼠标仍按住时，`\pos(254.2,109.7)` 已出现在 EditBox/网格、标题出现 `*`，黄色字幕像素向左移动。原行加入 `\i1\fad(0,30)` 后对中文行另做拖拽，标签及正文逐字保留；一次 `Ctrl+Z` 恢复拖拽前覆写块。[定位截图](../build/stage12-e2e/20261124-214306/03_pos_mouse_held.png)、[中文和原有标签截图](../build/stage12-e2e/20261124-214306/08_pos_chinese_tags_held.png)。
- 裁剪右边拖到 `\clip(0,0,642,720)` 时，松手前黄色字幕像素已由 1083 降到 560；单次撤销恢复原文和干净标题，Redo 恢复矩形；`×` 清除及一次 Undo 通过。手改 `\pos(100,100)`、切行后准星跳转及 `Std` 悬停 `640, 359` 均通过。双面板 `Sync` 可见且可切换，`KF` 在第 49 帧出现、下一帧消失。两次 GUI 实例正常退出 `0`，stderr 无 QML 警告/错误或致命错误；原始 ASS 文件哈希不变。[裁剪截图](../build/stage12-e2e/20261124-214306/05_clip_mouse_held.png)、[双面板截图](../build/stage12-e2e/20261124-214306/10_dual_panel_sync_kf.png)、[实测记录](../build/stage12-e2e/20261124-214306/transcript.txt)。
- Stage 11 回归 **7/7 套**：音画同步 29/29、无音轨 6/6、Shift Times 28/28、SRT 29/29、多音轨 34/34、关键帧/吸附 45/45、多视频轨 34/34。关键帧及多视频轨在临时脚本改用可见按钮/菜单控件定位后复跑通过；不改原回归脚本。[首轮日志](../build/stage12-regression.log)、[补跑日志](../build/stage12-rerun.log)。

## 6. 已知限制（下一步工作）

- **Save As 的原生对话框自动化**：Stage 11A 起已覆盖——Windows 的 `FileDialog.SaveFile` 会弹出 `#32770` 原生窗口，自动化流程是「File 菜单 → Down×4 → Enter → 找到 `#32770` → 用控件 id 1001 `WM_SETTEXT` 写入绝对路径 → 点控件 id 1（保存）」，之后弹窗关闭、标题切到新文件名（见 §7）。仍依赖 Windows 原生对话框；换成非原生实现的环境需要另写。
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
- **视频预览（Stage 10A）限制**：索引视频与构建 libass 字体缓存走同步版 `QtBackgroundRunner`，**首次打开会短暂卡住 UI**（没有进度对话框/取消）；帧解码在 GUI 线程同步完成，4K/高码率播放可能掉帧；没有 DAR 非等比拉伸、缩放/裁切、旋转显示（旋转/翻转由原 ffms provider 处理）；Dummy 视频是固定棋盘格，不随时间变化（用于验证字幕叠加而不是动态画面）；`Open Video...` 的 Windows 原生文件对话框已由 Stage 11B 的自动化覆盖（原生 `#32770`，控件 id 1148/1），多视频轨文件由 Qt `TrackPickerDialog` 选轨（见下条）。
- **视频/字幕状态**：`VideoViewModel` 只在有视频时维护 libass track；关闭视频后不会保留最后一帧；`Sync` 勾选时切行会强制 seek（播放中不 seek），没有 Aegisub 的“自动滚动”与“按帧编辑”联动。
- **音频波形（Stage 10B）限制**：只支持单声道 S16（经 `CreateConvertAudioProvider` 统一转换），没有频谱/立体声波形；峰值是整段可见范围一次性读取，超长音频（数小时）+ 高缩放下每次视口变化会重算，没有多级 mipmap 缓存；RAM 缓存按 `Audio/Cache/Type` 默认 1 会把整条音轨解码进内存（与 wx 版一致，长视频音频内存占用高）；`Audio/Cache/Type=2`（HD 缓存）未接，桥里回退到 Lock 模式；多音轨用户交互已由 Stage 11B 的 Qt `TrackPickerDialog` 接管；没有“自动滚动跟随播放头”；播放只支持活动行区间，没有循环播放与速率控制；音频电平/波形颜色不可配置（固定配色）。
- **音频与视频的联动（Stage 11A 已接通，仍有缺口）**：打开真实视频会自动加载同文件音轨，播放以音频时钟为准、暂停/定位/关闭双向同步（见 §3）；但没有 Aegisub 的 audio lead-in/lead-out 与音视频偏移调节，也没有「按帧对齐/吸附」；`Play Line` 只播当前活动行、不播选区外的连续音频；音频比视频短时播到音频结尾会停掉视频（反之视频到最后一帧也会停音频），没有「循环/续播」策略；多音轨文件已由 Qt `TrackPickerDialog` 处理（Stage 11B），但不会记住每个文件上次选中的轨道。
- **纯 Qt 多轨选轨（Stage 11B）限制**：只对 ffms2 能枚举出多轨的容器生效（AVI/MKV/MP4 等）；每次 open 会多做一次轻量 `FFMS_Indexer` probe（只读头，成本低）；命令行 `--video/--audio` 与开视频自动加载音轨固定选第一条，暂不支持 `--audio-track N` 之类的参数；`Audio → Select Audio Track...` 会在当前文件上重新 probe 并重开（会停止播放、重解码/波形，不保留播放位置）；取消选轨会保留原来已打开的文件和面板状态，但不会把"已关闭的旧文件"恢复回来；选轨结果没有持久化（关闭/重开文件会回到第一条）；多轨标签只在音频面板较宽（>620px）时显示。
- **关键帧显示/吸附（Stage 11B）限制**：关键帧完全来自 `VideoProvider::GetKeyFrames()`——Dummy 视频没有关键帧（紫线与吸附自然不可用），部分容器/编码没有关键帧信息时同样为空；不提供"按帧吸附"或"吸附到帧边界"，只吸附关键帧；吸附容差固定为 6 像素换算的毫秒数（缩放变化时容差随时间尺度变化，符合屏幕距离直觉），只支持 `Shift` 临时禁用，没有持久化开关（默认开）；关键帧只影响 Start/End 手动打轴，不影响视频跳转/定位逻辑；全关键帧素材（如未压缩 AVI）在波形上会呈现密集紫线（技术上是正确的，未做密度过滤）。
- **Shift Times（Stage 11A）限制**：只平移 Start/End（不动样式/文本/图层），没有原版的 `shift_history.json`（最近 50 条历史）；没有预览/试算；时间输入按厘秒取整（`agi::Time` 的 int 转换），亚厘秒会丢；`Selection onward` 以首个选中行起算（无选中时用活动行），与原版「从第一个命中行往后全部行」一致；帧模式完全依赖已打开视频的帧率/时间戳映射（VFR 用 `FrameAtTime`/`TimeAtFrame` 的 START/END 语义），没有视频时该选项禁用；`Apply` 不关闭窗口（原版 Apply 即关窗），可连续调整多次，每次都是一条独立 Undo。
- **SRT 读写（Stage 11A）限制**：写出固定 UTF-8（没有编码选择器，读入时仍自动检测）；ASS→SRT 会按原版规则丢样式/注释行、拆分或合并重叠/相同行（样式全部落到 `Default`，SRT 本身没有样式）；`SRTSubtitleFormat::CanSave()` 只在 `GetWriter` 时代替不了——Qt 侧直接实例化写出，不兼容的脚本（附件/非 Default 样式/不支持的覆写标签）不会提前弹警告，只是内容有损；没有 SRT 专属导入/导出选项（帧率、编码、默认样式目录仍走 `agi::Options` 默认值）；SRT 的 `<font color=...>` 等标签按原版 `SrtTagParser` 能力处理，不支持全部 HTML。
- **bestsource** 在本机构建中被禁用（子模块 `libp2p` 在 bitbucket 需要认证），视频源只有 ffms2。

## 7. 验证方式

- 编译：`.build-tools\build.bat aegisub-qt`（EXIT 0）、`.build-tools\build.bat`（wx 回归 EXIT 0）。本机 DSH 沙箱里改用 `bash .build-tools/build_ninja.sh [target]`（见 §4），Stage 7 / 8A / 10A 实测 `aegisub-qt` 与全量（wx + qt）均 EXIT 0。
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
  - Stage 10A（M2a 视频预览，脚本/截图在 `.build-tools/stage10a/`，不入库；素材 = `test_stage6_input.ass` + 自制 640×360 / 24fps 未压缩 AVI）：
    - 构建：`bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` 与全量 `bash .build-tools/build_ninja.sh` 均 **EXIT 0**（`aegisub-qt.exe` 与 `aegisub.exe` 都链接成功）。
    - 无视频启动：布局与 M1 完全一致（`s1_m1_layout.png`）；`Video → Use Dummy Video` 后视频区从隐藏展开为上方约 300px（`s2_dummy_open.png`），按脚本 `PlayResX/Y` 打开 Dummy 1280×720/23.976fps 棋盘格，libass 实时渲染第 1 行（`Title` 样式黄字），`Frame 37 / 36000`、`0:00:01.50`；
    - 点第 2 行（`Default` 中文行，`0:00:02.50`）→ 自动 seek 到 `Frame 61 / 36000`、`0:00:02.50`，画面渲染「第二行：QML 网格与编辑框」（`s3_row2.png`）；
    - 正文改成 `live_edit`（自动化用 UI Automation `ValuePattern.SetValue`，绕开本机 IME 把 ASCII 吞成拼音组合的问题，见坑 11）→ 网格第 2 行 / EditBox / **视频上的字幕**同时变化（`s4_edit.png`）；`Edit → Undo` 后三者一起回退到中文原文、标题 `*` 消失（`s6_undo.png`）；
    - 单帧 `▶`×2 → `Frame 61→63`、时间 `0:00:02.59`（`s7_step.png`）；`Play` 约 1.3s → `Frame 94 / 0:00:03.88`、按钮变 `Pause`（`s8_playing.png`）；点时间轴中段 → `Frame 16590 / 36000`、`0:11:31.86`（`s10_slider.png`）；
    - `Video → Close Video` → 视频区塌缩、界面恢复 M1 布局（`s11_closed.png`）；再开 Dummy 后关窗 → 进程干净退出（bash `wait` 得 `APP_EXIT=0`，stderr 无 QML 警告）；
    - 真实 ffms2 路径：`aegisub-qt test_stage6_input.ass --video test_video.avi` → 打开自制 640×360/24fps AVI（首次索引写入 `?local/ffms2cache/*.ffindex`），首帧即有画面 + `Title` 字幕叠加（`r1_open.png`），切第 2 行后画面进度与字幕同步（`r2_row2.png`）；
    - 错误路径：`--video bogus_video.mp4`（把 ASS 改名成 mp4）→ stderr 输出 `Failed to open video ... "no video tracks found"`，窗口正常、视频区保持隐藏（`e1_bad_video.png`），不崩溃；
    - 回归：行操作、查找替换、样式管理器、未保存确认框等 M1 功能不受影响（视频面板 `visible: videoVM.hasVideo`，无视频时 SplitView 完全折叠）。

  - Stage 10B（M2b 音频波形与播放，脚本/截图在 `.build-tools/stage10b/`，不入库；素材 = `test_stage6_input.ass` + 自制 3 秒 44.1k mono S16 `test_audio.wav`）：
    - 构建：`bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` 与全量 `bash .build-tools/build_ninja.sh` 均 **EXIT 0**（含新增 `audio_provider_ffmpegsource.cpp` + `audio_player_xaudio2.cpp`；无 Qt Multimedia 依赖）。
    - 四区布局：`Use Dummy Video` + `Use Dummy Audio` → 左上视频（libass 字幕）+ 右上 10 秒 440Hz 正弦波形（标尺 0:00–0:09、红 Start / 蓝 End、选区青蓝高亮、控制条 `0:00:01.50 → 0:00:02.50` + `XAudio2` 状态）+ 中间网格 + 底部 EditBox，无裁切（`f1_four_zone.png`）；
    - 切到第 2 行 → 波形自动居中显示 `[2.50, 5.00]`（`f2_row2.png`）；
    - **左键点波形空白**设置新 Start（2.79s）→ 网格 `Start=0:00:02.79`、EditBox Start/Duration 同步、标题 `*`、红线下移，且视频画面（Frame 61 = 2.50s）上的字幕**立即消失**（行已不覆盖该帧，`f3_setstart.png`）；`Edit → Undo` → 波形红蓝线、网格、EditBox、视频字幕全部回退、`*` 消失（`f4_undo_start.png`）；
    - **右键点波形空白**设置 End（3.18s）→ 网格 `End=0:00:03.18`、Duration 0.68、蓝线移动（`f5_setend.png`），Undo 回退；
    - **拖拽红色 Start 线**（按住线拖到 ~3.03s）→ 松手后单条提交：网格 Start 3.03、Duration 1.97、标题 `*`、视频字幕消失（`f6_dragstart.png`），一次 `Ctrl+Z`（Edit→Undo）完整回退（`f7_undo_drag.png`）；
    - **Play Line**（XAudio2 真实设备存在，状态显示 `XAudio2`）：白色播放头在选区内推进、Stop 可用（`f8_playing.png`），约 2.5s 后自动停止、播放头消失（`f9_stopped.png`）；
    - **真实音频**：`aegisub-qt test_stage6_input.ass --audio test_audio.wav` → ffms2 解码 WAV（首次索引进 `?local/ffms2cache`）→ 波形正确在 3 秒处结束（之后平直，`w1_wav_open.png`），`Play Line` 播放头推进（`w4_wav_playing.png`）；
    - **关闭**：`Audio → Close Audio` → 音频区折叠、视频区铺满（`f10_audio_closed.png`）；再 `Close Video` → 恢复 M1 布局（`f11_all_closed.png`）；开 Dummy 视频+音频、播一下再关两者、关窗 → bash `wait` 得 `APP_EXIT=0`，stderr 无 QML 警告；
    - 回归：视频预览、行操作、文本编辑/撤销、查找替换、样式管理器等 M1/M2a 功能不受影响。

  - Stage 11A（M3a 音画同步 + Shift Times + `.srt`，脚本/截图/日志在 `.build-tools/stage11a/`，不入库；素材 = `test_av.avi`（320×180 10fps 60 帧 + 44.1k PCM16 mono 440Hz 6s，FFmpeg 标准 AVI 双流）、`test_stage11a.srt`（6 条中文 cue，含多行与 `<i>/<b>` 标签）、`test_shift.ass`（`test_stage6_input.ass` 逐字节副本））：
    - 构建：`bash .build-tools/build_ninja.sh` 全量双轨 **EXIT 0**、日志无 FAILED（`build/src/qt_ui/aegisub-qt.exe` 与 `build/aegisub.exe` 均为最新）；Qt 单目标 `bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` 同样 EXIT 0。
    - 自动化方式：PowerShell + Win32（`PrintWindow` 截图、`keybd_event` 组合键、原生 Save As 对话框用控件 id 1001/1 操作）+ UI Automation 读控件与网格单元格（不 OCR）；磁盘结果用 `verify_stage11a.py` 解析 `.ass`/`.srt` 做逐字段断言。脚本 `e2e_a_av.ps1` / `e2e_a2_noaudio.ps1` / `e2e_b_shift.ps1` / `e2e_c_srt.ps1` + `lib2.ps1`，结果文件 `e2e_a_results.txt` / `e2e_a2_results.txt` / `e2e_b_results.txt` / `e2e_c_results.txt`。
    - **A/V 同步（29/29 PASS）**：`aegisub-qt test_shift.ass --video test_av.avi`（**不带** `--audio`）→ 音频面板与波形自动出现、`Play Line`/`Stop` 就绪（`a1_loaded.png`）；点播放 → 按钮变 `Pause`、`Stop` 可用（音频传输在跑），1.6s 后视频 `Frame 16→32 / 0:00:03.10`（`a2_playing.png`），再等 1s 视频时间 `+1000ms`（墙钟 +1000ms，`a3_playing_1s_later.png`）；`Pause` → 帧冻结在 `Frame 44 / 0:00:04.30`、`Stop` 变灰（`a4_paused.png`）；播放中点视频时间轴 → 双方停止、跳到 `Frame 56 / 0:00:05.50`（`a5_after_seek.png`）；播放中点 `Play Line` → 视频立刻暂停、音频播完活动行自动停（`a6_play_line.png`）；播放中 `Audio → Close Audio` → 视频停在 `Frame 28 / 0:00:02.70`、音频面板折叠（`a7_audio_closed.png`）；`Audio → Open Audio from Video` 后面板恢复并重新同步；播放中 `Video → Close Video` → 音频停止、视频面板折叠而音频面板保留（`a8_video_closed.png`）。**无音轨视频（6/6 PASS，`e2e_a2_noaudio.ps1`）**：`--video .build-tools/stage10a/test_video.avi`（34MB 纯视频 AVI）→ 视频正常打开（`Frame 37 / 50`）、音频面板不出现、错误提示条为空（自动加载静默跳过），播放按墙钟推进（`a9_no_audio_video.png`）。
    - **Shift Times（28/28 PASS）**：打开 `e2e_shift.ass`（`test_shift.ass` 副本）→ 点第 2 行 + `Ctrl+Click` 第 3 行（状态栏 `5 lines | 2 selected`，`b2_selected.png`）→ `Ctrl+I` 弹出 Shift Times（`b3_dialog.png`），设 `0:00:05.00`、`Selected rows`、`Start and End`、Forward → `Apply` 显示 `Shifted 2 line(s)`（`b4_dialog_applied.png`）→ 网格仅第 2/3 行变为 `0:00:07.50–0:00:10.00` / `0:00:10.00–0:00:13.00`、其余行不动、标题出现 ` *`（`b5_applied.png`）→ **一次** `Ctrl+Z` 全部回退且 `*` 消失（`b6_undo.png`）；再次 Apply 后 `Ctrl+S`，磁盘 `.ass` 经 Python 校验第 2/3 行 `+5000ms`、其余逐字段一致；`Ctrl+Z` 后标题重新变脏、再 `Ctrl+S` 磁盘回到基线逐字段一致（`b7_restored_saved.png`）。
    - **`.srt`（29/29 PASS）**：`aegisub-qt test_stage11a.srt` → 网格 6 行中文 cue、时间码 `0:00:01.50–0:00:02.60`、多行文本以 `\N` 显示、`<i>/<b>` 转成 `{\i1}/{\b1}`、Style 全为 `Default`、状态栏编码 `utf-8`（`c1_srt_open.png`）；选中第 2 行把正文改成 `SRT 编辑行：中文与换行\N第二行内容` → 网格实时更新、标题 `*`（`c2_edited.png`）；`File → Save As...`（原生对话框）存成新 `.srt` → Python 校验 6 条 cue 时间不变、cue2 为两行新文本、其余 cue 文本与基线（含 `<i>/<b>` 标签）一致（19/19，`c3_saved_srt.png`）；再 Save As 成 `.ass` → 6 个 event、时间一致、样式全 `Default`、`{\i1}/{\b1}` 保留、`[Script Info]` 与 `Default` 样式存在（23/23，`c4_saved_ass.png`）；重新打开新 `.srt` 与新 `.ass` 均 6 行、cue2 多行编辑内容、标签与时间码无损（`c5_reopened_srt.png` / `c6_reopened_ass.png`）。
    - 回归：`Ctrl+S` 保存（`FileShortcutFilter` 改造后）、`Ctrl+Z` 撤销、网格多选与 EditBox 联动、视频/音频面板开合均正常；自动化 stderr 无 QML 警告，进程按 PID 干净退出。
    - 说明：本轮自动化期间本机 XAudio2 设备不可用（状态栏 `Silent playback (XAudio2 playback failed)`，连 Stage 10B 的 WAV 也一样降级），所以实际出声是墙钟静音降级；但 `playFrom()` 走的是同一条音频时钟路径，视频帧与音频播放头按音频时间推进/停止的断言全部成立（见坑 46）。

  - Stage 11B（M3b 纯 Qt 多轨选轨 + 关键帧显示/吸附，脚本/截图/日志在 `.build-tools/stage11b/`，不入库；素材：`test_multiaudio.avi`（1 视频 + 2 条 PCM16 44.1k 音轨：440Hz/幅度0.2 与 880Hz/幅度0.7，各 6s）、`test_keyframe.mkv`（320×180 24fps 120 帧 5s、mpeg4、I 帧恰在 0/1/2/3/4s）、`test_twovideo.mkv`（2 条视频轨：Track1 320×180 24fps 72 帧、Track2 641×361 12fps **36 帧**）；字幕/参考音频复用 Stage 11A）：
    - 构建：`bash .build-tools/build_ninja.sh src/qt_ui/aegisub-qt.exe` **EXIT 0**；`bash .build-tools/build_ninja.sh` 全量双轨 **EXIT 0、日志 0 个 FAILED**（`build/src/qt_ui/aegisub-qt.exe` 与 `build/aegisub.exe`）。测试素材用 `.build-tools/stage11b/assets/py/imageio_ffmpeg` 里的 ffmpeg 生成（仅测试用，不入库、不进构建）。
    - 自动化方式：沿用 Stage 11A 的 PowerShell + Win32 + UIAutomation；新增 `.build-tools/stage11b/e2e/common.ps1`（顶层窗口枚举与 wx/#32770 检测、命名 UIA 控件、菜单/选轨窗口驱动、System.Drawing 关键帧紫线像素分析、真实 ExitCode 关窗——PS 5.1 的 `Start-Process -PassThru -RedirectStandard*` 拿不到 ExitCode，改用 .NET `Process` + `ReadToEndAsync`，见坑 54）。原生打开对话框自动化：`#32770` 里文件名 Edit **id=1148** 用 `WM_SETTEXT` 写路径、确认 Button **id=1** 点击。
    - **纯 Qt 多轨选轨 - 音频（34/34 PASS，`e2e_d_tracks.ps1`）**：`aegisub-qt test_shift.ass --audio test_multiaudio.avi` 启动不弹选轨（自动第一条），音频面板出现、trackLabel = `Track 1 — pcm_s16le`（`d1_autofirst.png`）；`Audio → Select Audio Track...` → 弹出标题 `Select Audio Track` 的 Qt 窗口，含 RadioButton `Track 1 — pcm_s16le`/`Track 2 — pcm_s16le` 与 `trackPickerOpenButton`/`trackPickerCancelButton`（`d2_picker.png`）；点 Cancel → 窗口关闭、trackLabel 仍 Track 1、面板与错误条状态不变（`d3_cancelled.png`）；重开 → 选 Track 2 → Open → 窗口关闭、trackLabel 变 `Track 2 — pcm_s16le`、波形仍正常（`d4_track2.png`）；单音轨文件（`stage11a/test_av.avi`）走 `Select Audio Track...` 不弹窗、静默重开（`d5_single.png`）；全程无 `#32770`/wx 顶层窗口，两个实例真实 ExitCode=0、stderr 仅 `[document] loaded ...`。
    - **关键帧显示与吸附（45/45 PASS，`e2e_e_keyframes.ps1`）**：`aegisub-qt test_shift.ass --video test_keyframe.mkv --audio <6s wav>` → 初始 `Frame 37 / 120   0:00:01.50`、无 `KF`；点 `▶`×12 → `Frame 49 / 120   0:00:02.00`、UIA Text `KF` 出现，再 `▶`×1 → `Frame 50 / 120   0:00:02.04`、`KF` 消失（`e1_kf_badge.png`）；截图像素扫描检出 **5 条紫色关键帧竖线** x=482/579/679/779/879（间距 97/100/100/100 px），红线 x=632；在第 3 条（2.0s）线右侧 4px 点击 → 网格第 1 行 Start **`0:00:01.98`**（`agi::vfr::START` 语义下的关键帧毫秒是 1979，不是 2000，见坑 51）、End 仍 `0:00:02.50`、标题 `test_shift.ass * - Aegisub Qt6`、EditBox Start ValuePattern=`0:00:01.98`（`e2_snap_click.png`）；一次 `Ctrl+Z` → Start 回 `0:00:01.50`、标题干净（`e2_after_undo.png`）；拖拽红线从 1.5s 到同一位置 → 松手 Start `0:00:01.98`、一次 Undo 回退（`e3_snap_drag.png`）；取消勾选 `Snap KF` 后同一位置点击 → Start `0:00:02.01`（≠1.98 且 >2.00，`e4_snap_off.png`）；无原生弹窗、ExitCode=0、stderr 干净。
    - **纯 Qt 多轨选轨 - 视频（34/34 PASS，`e2e_f_videotrack.ps1`）**：`Video → Open Video...`（原生 `#32770`，Edit id=1148 + Button id=1）打开 `test_twovideo.mkv` → 弹出标题 `Select Video Track` 的 Qt 窗口，列出 `Track 1 — mpeg4`/`Track 2 — mpeg4` 两个 RadioButton 与 Open/Cancel（`f2_video_picker.png`）；Cancel → 窗口关闭、仍无视频、无错误条、无原生窗口残留（`f4_cancelled.png`）；重开 → 选 Track 2 → Open → 视频打开且帧标签 `Frame 19 / 36   0:00:01.50`（Track 1 是 72 帧，`/36` 证明选中的是第 2 条视频轨，`f3_video_track2.png`）；ExitCode=0、stderr 干净。
    - **回归（4/4 suites PASS，`regression.ps1`）**：Stage 11A 的 `e2e_a_av` 29/29、`e2e_a2_noaudio` 6/6、`e2e_b_shift` 28/28、`e2e_c_srt` 29/29 全部通过（仅测试脚本随 `Audio` 菜单新增 `Select Audio Track...` 把 `Close Audio` 的键盘序号 4→5 调整，产品行为无回归）；Stage 11A 的截图/结果由 regression 重新生成在当前构建上。
    - 说明：XAudio2 设备是否可用在多次自动化之间会变化（Stage 11B 的关键帧/选轨截图里常见 `XAudio2`，而同一轮 Stage 11A 回归里又出现 `Silent playback (...)` 降级），两种情况下传输状态/播放头断言均通过；关键帧吸附与波形渲染不依赖音频设备。
