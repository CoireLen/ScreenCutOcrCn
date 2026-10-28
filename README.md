# ScreenCutOcrCn

Windows 截图 + 中文 OCR 小工具，基于 [CnocrCpp](https://github.com/CoireLen/CnocrCpp)（PP-OCRv3 检测 + CTC 识别，onnxruntime 推理）。

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Alt + C` | 框选区域 → 识别文字 → 写入剪贴板 |
| `Alt + A` | 框选区域 → 截图写入剪贴板 |
| `Esc` / 右键 | 取消当前框选 |

选区界面：半透明暗化遮罩、十字准线、实时显示选区尺寸、四角把手，顶部提示操作方式。识别完成后屏幕上方会短暂提示结果行数。

## 主界面与托盘

主界面是 Qt 窗口，顶部工具栏可直接触发截图/识别，下方三个标签页：

| 标签页 | 内容 | 操作 |
| --- | --- | --- |
| 截图历史 | 缩略图 + 时间 + 尺寸，右侧大图预览 | 双击或“复制所选”写入剪贴板；“清空截图历史”（确认后清空） |
| 识别历史 | 时间 + 摘要，右侧完整文字预览 | 双击或“复制所选”复制文字；“清空识别历史” |
| 日志 | 启动信息、截图/识别过程、耗时与错误 | “清空日志” |

- 历史上限：截图 30 条、识别 100 条、日志 500 行，超出后自动丢弃最旧的记录。
- **关闭窗口**时弹窗三选一：`常驻托盘`（隐藏窗口，热键与托盘菜单继续可用）、`退出程序`、`取消`。
- **托盘菜单**（右键托盘图标）：`显示主界面`、`截图 (Alt+A)`、`识别 (Alt+C)`、`退出`；单击或双击托盘图标恢复主界面。
- 托盘气泡会提示当前仍可用的快捷键。

## 环境依赖

| 依赖 | 说明 |
| --- | --- |
| onnxruntime | 1.31.1，默认路径 `E:/Lib/onnxruntime-win-x64-1.31.1`（需含 `include/`、`lib/`） |
| OpenCV | 通过 vcpkg 安装，默认 vcpkg 根目录 `E:/Lib/vcpkg` |
| Qt6 | 推荐同样用 vcpkg：`vcpkg install "qtbase[core,gui,widgets]:x64-windows"`；也可用官方 Qt SDK |
| CMake / MSVC | CMake ≥ 3.21，Visual Studio 2022+，C++17 |

路径均可在配置时覆盖：

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DONNXRUNTIME_ROOT=<onnxruntime解压目录> \
  -DVCPKG_ROOT_DIR=<vcpkg目录> \
  -DQT_ROOT=<官方Qt msvc目录，使用vcpkg时不用填>
cmake --build build --config Release
```

> 使用 Visual Studio 2026 内置 CMake 时，生成器改为 `-G "Visual Studio 18 2026" -A x64`。

构建后自动完成：

1. 复制 `onnxruntime.dll`、`onnxruntime_providers_shared.dll` 到输出目录；
2. 复制 `modle/*.onnx`、`dict/*.txt` 到输出目录（程序按相对路径读取）；
3. 用 `windeployqt` 部署 Qt 运行时与平台插件（没有时直接复制 Qt 插件目录）。

运行时请从输出目录启动 `ScreenCutOcrCn.exe`。

> Release 构建为 Windows 子系统，启动时不会弹出控制台窗口，运行日志显示在主界面“日志”页；Debug 构建保留控制台，便于查看 `std::cerr` 输出。

## 模型与字典

| 模型 | 字典 | 说明 |
| --- | --- | --- |
| `modle/ch_PP-OCRv3_det_infer.onnx` | — | DBNet 文本检测 |
| `modle/cnocr136fc.onnx` | `dict/label_cn.txt` | 中文识别（默认） |
| `modle/en_number_mobile_v2.0_rec_infer.onnx` | `dict/en_dict.txt` | 英文数字识别 |
| `modle/chinese_cht_PP-OCRv3_rec_infer.onnx` | `dict/chinese_cht_dict.txt` | 繁体识别 |

## 错误提示与健壮性

- **启动自检**：检查 `modle/cnocr136fc.onnx`、`modle/ch_PP-OCRv3_det_infer.onnx`、`dict/label_cn.txt`，缺失时弹窗列出**绝对路径**与当前工作目录，便于定位部署问题。
- **初始化失败**：OCR 引擎构造异常时弹窗显示异常信息、当前工作目录与排查建议（模型完整性、onnxruntime、VC++ 运行库），进程以返回码 1 退出。
- **热键被占用**：`Alt+C` / `Alt+A` 注册失败时弹窗列出失败项；程序继续运行，只是对应功能无法用热键触发。
- **运行时错误**：截图失败、OCR 异常、剪贴板被占用等都会在屏幕顶部以提示条显示具体原因，而不是静默失败。
- **并发保护**：同一时刻只执行一个截图/识别流程，重复触发会提示“正在处理中，请稍候”。
- **剪贴板**：打开剪贴板失败时重试；文本按 UTF-8 解码为 UTF-16；图片以标准 `CF_DIB`（32 位、自底向上）写入并正确处理行尾填充，所有失败路径都会释放句柄/内存并输出错误码。
- **提示条**：全局单实例、纯文本渲染（OCR 内容中的 `<...>` 不会当成富文本）、自动换行并限制最大宽度。

## 常见问题

| 现象 | 排查方向 |
| --- | --- |
| 启动弹窗“缺少识别所需的模型/字典文件” | 按弹窗给出的绝对路径补齐 `modle/`、`dict/`，或从输出目录启动 |
| 启动弹窗“OCR 初始化失败” | 模型文件可能损坏；确认 `onnxruntime.dll` 与 VC++ 运行库（`vcruntime140*.dll`、`msvcp140*.dll`）在 exe 旁 |
| 启动无窗口 / 闪退 | 缺少 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll` 或 `platforms/qwindows.dll`，重新执行 `windeployqt` |
| 弹窗“热键注册失败” | 快捷键被其他程序占用（如输入法、截图工具），关闭冲突程序后重启本程序 |
| 截图变黑 | 已内置 GDI 兜底；若仍出现，多为显卡驱动/远程桌面限制，可先切换桌面后重试 |

## 实现说明

- `screen_capture.*`：截图与选区。截图和 `QDialog` 遮罩都在 GUI 线程执行；`QScreen::grabWindow` 返回全黑时退回 GDI `BitBlt`（每次新建 DC/Bitmap），主屏物理区域由 Win32 监视器信息确定。提示条用 Win32 物理坐标居中，避免高 DPI 下 Qt 逻辑/物理坐标不一致导致偏移。
- `main.cpp`：热键线程只负责把任务排队回 GUI 线程；两条流程共用 `capture_and_select()`，OCR 放入 `QThreadPool` 异步执行，识别结果统一写剪贴板并弹出提示。
- `main_window.*`：主窗口与托盘。截图、识别结果和日志分别写入三个标签页（附件用 `Qt::UserRole` 保存完整数据，列表只显示缩略图/摘要）；`closeEvent` 拦截关闭并提供“常驻托盘/退出程序/取消”；`QSystemTrayIcon` 菜单项与工具栏信号复用同一条流程，`setQuitOnLastWindowClosed(false)` 保证窗口隐藏后进程继续运行。
- `cnocr.*` / `cnstd.*` / `db_postprocess.*` / `modle.*` / `text_utils.*`：与 CnocrCpp 同步的检测、后处理、推理封装与 UTF-8 工具。

## 更新记录

### 20261029

- Release 构建改为 Windows 子系统：双击启动不再弹出控制台窗口，日志统一显示在主界面“日志”页；Debug 构建仍保留控制台。
- 新增 Qt 主界面与托盘：截图历史（缩略图 + 大图预览）、识别历史（摘要 + 全文）、日志三个标签页，各自带清空按钮；关闭窗口可选“常驻托盘/退出程序/取消”；托盘右键菜单提供显示主界面、截图、识别、退出。
- 修复软件运行一段时间后截图全黑：截图与选区改到 GUI 线程，移除跨线程创建/销毁的 OpenCV 全屏窗口，并对 `grabWindow` 全黑结果增加 GDI 兜底。
- 选区 UI 重做：暗化遮罩、十字准线、实时尺寸、四角把手、顶部操作提示，支持 `Esc`/右键取消。
- 复用与简化：两条截图流程共用同一套选区逻辑；热键回调统一排队到 GUI 线程；OCR 异步执行并给出状态提示。
- 同步 CnocrCpp 的 PP-OCRv3 版本：新增检测模型，重写推理封装，修复 CTC blank 索引、置信度、字典 CRLF/`<space>`/`<unused*>` 处理。
- 修复剪贴板文本超过 1000 字节被整段丢弃、截图 `cv::Mat` 引用已释放 `QImage` 缓冲、反向拖拽产生负尺寸矩形等问题。
- 健壮性：启动自检模型/字典并弹窗给出缺失文件绝对路径；热键注册改为同步返回结果，失败时提示占用的快捷键；截图、识别、剪贴板失败均有可见提示；剪贴板与 GDI 资源在所有失败路径上正确释放。
- 截图改为写入标准 `CF_DIB`（32 位、自底向上），修复部分程序读取剪贴板图片为空的问题；提示条改用 Win32 物理坐标定位，修正高 DPI 下的偏移。

### 20230926

更换 cnocr 版本

### 20220629

Alt-A 截图保存到剪贴板
