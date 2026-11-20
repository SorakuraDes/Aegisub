# Aegisub Qt6 Refactoring Project Rules

## 核心架构目标
1. 逐步将原版 wxWidgets UI 迁移至 Qt 6 (QML + Qt Quick Controls Material 3)。
2. 采用“双轨制”构建：保留原版可执行文件的构建，新增 `aegisub-qt` 作为独立测试入口。
3. 严格遵守 MVVM 架构：QML 仅负责纯 UI 渲染，所有业务逻辑必须通过 C++ ViewModel 桥接 `libaegisub`。

## 目录与修改权限红线
- **绝对禁止修改**：`src/` 目录下除 `meson.build` 之外的任何原版 wxWidgets 界面文件（如 `dialog_*.cpp`, `frame_*.cpp` 等）。
- **绝对禁止修改**：`libaegisub/` 目录下的核心底层逻辑。
- **允许修改**：项目根目录和各级目录下的 `meson.build` 文件。
- **强制要求**：所有新的 Qt 相关的 C++ 和 QML 代码必须存放在新建的 `src/qt_ui/` 目录下。

## 编译验证流程
- 每次修改代码或构建脚本后，必须在终端执行 `ninja -C build`。
- 如果发生编译或链接错误，最多尝试修复 3 次。如果 3 次后仍未通过，必须停止修改并向用户汇报错误日志，等待人工介入。不要通过删减原有核心功能来强行通过编译。