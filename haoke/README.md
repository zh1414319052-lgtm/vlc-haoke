# VLC Windows 浩科正式版维护

当前正式版 3.0.24-Haoke.5 / 3.0.24.5 / 3000024005，公开发行事实、哈希、许可及未测范围见项目根 `README.md` 与 `docs/PUBLIC_RELEASE.json`。版本已发布，下一次修改可执行代码必须先递增 `version.py` 与 `release_identity.json`，禁止同版替换已发布哈希。

## 许可与源码

基于 VideoLAN VLC 3.0.24，提交 6de05adcbaf2e8b85fe86aad4169393098628119；播放器及浩科修改 GPL-2.0-or-later，LibVLC 相关文件 LGPL-2.1-or-later，依赖/资源按各自文件许可。上游为 https://code.videolan.org/videolan/vlc/ 。保留全部原作者/版权/许可，修改文件附浩科日期说明；`OPEN_SOURCE_NOTICE.txt` 是随包归属说明，不替换原许可。Noto Sans CJK SC / Noto Sans SC 为 SIL OFL 1.1，可商用，不因嵌入而改成 GPL。

分发本版安装包时，同时提供完整对应源码、固定依赖源码、recipe/patch、接口与构建脚本；上游链接或二进制工具链不能替代修改版源码。89 份依赖归档与清单在 `../依赖/源码包`、`../依赖/source-manifest.json`，字体原件与许可在 `installer_assets`，第三方通知在 `../依赖/运行库许可`。

## 实现与不变量

重建原生 WinMain 和 Qt UI 插件，其余 364 个官方 DLL/插件保持原字节。Qt 共享主题/字体为 `ui_theme.hpp`、`ui/theme.qss`，结构设计为 `../design/ui-20261102/vlc.haoke-ui.json`。空状态只在当前视图实际绘制面使用局部坐标绘制；树/列表/图标使用 viewport，PictureFlow 使用内容画布。空→有内容→清空与视图切换都要检查。字体、点击区与可见轮廓分别验收，不仅看 iconSize。

语言使用 Windows 首选 UI 语言列表第一项：所有 zh 变体→简体中文，其余→英文；显式 `--language` 可覆盖，不能从后续中文候选改变第一首选。关于页不同上游文案变体统一添加归属。原生中英文版本资源由 build-entry.sh 从 version.py 生成。列表总时长和视图模式在语言安装后翻译，不做静态启动前翻译。

保留既有 64% 工作区/视频比例策略、手动尺寸、配置及快捷键。正式版 `HAOKE_FORMAL_UPDATES` 复用现有签名更新协议，禁用上游自动版本通知和旧界面选项，保留原媒体元数据选择，首次提示披露浩科请求及常规连接元数据。无新增遥测、心跳、设备统计、后台轮询或自动安装。

## 构建

需要 Windows Python3.12（polib）、WSL Ubuntu/MinGW-w64、固定 Qt5.6.3 contrib 和 Inno Setup6.7.3。本项目实际使用 `C:/Program Files (x86)/Inno Setup 6/ISCC.exe`，未更改编译器许可。保留的 Inno 使用条款在 installer_assets/INNO-LICENSE.txt。

1. 使用固定归档 `../依赖/VLC_Qt工具链_910cc9ca.tar.zst` 的新隔离 prefix；按上游 contrib change_prefix.sh 迁移文本路径，核对 Qt 工具为 5.6.3。工具链 SHA-256 为 49f7690b3d09760f842f9f8e9612c28de8cbb1d027bcb3f6aaa25767611eea8c。
2. `bash haoke/build-qt.sh <source-root> <relocated-prefix> <new-ext4-build> formal`。生成带当前身份头的真实 Qt DLL 与匹配 haoke-qt-build.json；版本/身份变更须重新生成，QA/fixture 不能冒充正式。
3. `python -X utf8 haoke/build.py --stage formal --qt-plugin <raw-DLL> --qt-build-metadata <matching-json> --signing-metadata <protected-public-identity.json> --work-dir <new-runtime> --output <new-runtime.zip>`。输出必须新建；resume 只用于本任务的准确工作目录，仍需新输出文件名。该 ZIP 是安装器输入。
4. `python -X utf8 haoke/build_setup.py --runtime-zip <accepted-runtime.zip> --runtime-sha256 <exact-hash> --signing-metadata <same-public-identity.json> --work-dir <new-setup-work> --output <name_Setup.exe>`。输出和工作目录须不存在。构建检查正式身份、原生文件版本和运行文件签名，生成并签名真正 Inno 向导、卸载器和语言 bootstrap；最终元数据必须取这个 Setup，不能取内层 ZIP。

sign-artifact.ps1 和 sign-windows.ps1 显式选择原生系统 wsl.exe，兼容 32 位 Inno 宿主；不设置全局 PATH、信任根或扩权。原证书和密钥保留在外部受保护存储，仅公开 identity 引用随维护使用，密钥/密码/授权码不进入源码或包。自签名身份固定摘要验证与公众 CA 信任分开报告。

## 针对性验证与发布

`python -X utf8 haoke/verify_dependency_sources.py`；`python -X utf8 haoke/prepare_dependency_notices.py`；`python -X utf8 -m unittest discover -s haoke/tests -p test_build_setup.py`。语言、窗口及 CRT 测试在原生入口构建中运行；更新协议/状态/传输测试脚本及 server fixture 在 haoke/tests。首次运行不能全部用禁止隐私提示的测试参数代替，应检查真实普通启动、提示位置、语言和用户选择。

正式包在任务专用安装目录检查安装、冷启动、同身份覆盖升级、数据保留、系统卸载、快捷方式、核心交互及整窗 UI。测试仅用合成数据，保留原用户配置/安装。最终 .5 的实际结果与未测范围见根 README，不把构建通过、早期 QA 或演示片段当作本包全部功能验收。

网站复用软件 ID b92689d5ae8d3982、Windows stable、org.VideoLAN.VLC 及既有证书/Ed25519 信任键。读取实时契约和 revision，只改本轮 Windows 平台；保留其他平台、固定下载链接、提取码和媒体。网站更新与通知发布均须核对最终签名 Setup 的版本、大小、哈希，失败不得弱化验签。用户维护原网盘安装包入口，通知发布不代表已代传或核验网盘。对应源码公开于 https://github.com/zh1414319052-lgtm/vlc-haoke；GitHub 下载入口指向 https://haoke.org/software/b92689d5ae8d3982?platform=windows。

公开维护元数据见 ../docs/PUBLIC_RELEASE.json。实际账户凭据、签名私钥和本机操作回执不在公开仓库内。大型源码/构建输入先从同版本 Release 获取并校验，目录结构与此构建说明保持一致。
