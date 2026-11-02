# 全能影音播放器 · 浩科优化版

基于 [VideoLAN VLC 3.0.24](https://code.videolan.org/videolan/vlc/) 的 Windows x64 独立第三方优化版，当前版本 **3.0.24-Haoke.5**。本项目不代表 VideoLAN 官方发行或背书。

**[下载安装包：浩科工具站 haoke.org](https://haoke.org/software/b92689d5ae8d3982?platform=windows)** · [完整源码与依赖下载](https://github.com/zh1414319052-lgtm/vlc-haoke/releases/tag/3.0.24-Haoke.5) · [维护与构建说明](haoke/README.md)

## 本版改动

原生 Qt 影院深色界面、中英文系统首选语言适配、Noto 字体私有加载；音量图标与点击区、播放列表对比度、倍速弹窗及设置指示器修正。空列表在详细列表、列表、图标和 PictureFlow 视图中只绘制一次，并处理加入/删除最后一项与窄窗口布局。首次隐私提示适配屏幕工作区。Windows 安装向导、保留配置升级/卸载和验签版本提醒已接入。

重建原生入口和 Qt 界面插件，其余 **364 个官方 DLL/插件保持原字节**；不声称新增解码能力或全格式、HDR、性能保证。当前浩科改动及实际验收范围是 Windows x64，其他上游平台不表示已有浩科移植版。

## 开源许可与归属

- VLC 桌面播放器及浩科修改：**GPL-2.0-or-later**，见 [COPYING](COPYING) 和各文件声明。
- LibVLC/相关核心文件：**LGPL-2.1-or-later**，见 [COPYING.LIB](COPYING.LIB)。依赖的实际条款共同适用，部分组合可能适用 GPLv3；不能把整个安装包一概标为 LGPL。
- 保留 [上游作者](AUTHORS)、原始版权与第三方通知；浩科修改文件注明修改日期。字体 **Noto Sans CJK SC** 为 SIL OFL 1.1，许可见 [字体目录](haoke/installer_assets)。依赖许可见 [依赖/运行库许可](依赖/运行库许可)。文档 CC BY-SA 不适用于本安装包。
- VLC/VideoLAN 名称与商标归原权利人；这是独立第三方派生项目。上游基线提交为 `6de05adcbaf2e8b85fe86aad4169393098628119`。上游 [再分发与商标说明](https://www.videolan.org/legal.html) 另行适用。

## 获取完整对应源码

本仓库包含完整 VLC 发行源码、本版代码、字体、构建脚本和许可，`docs/SOURCE_FILES.sha256` 记录与本地正式版对应源码的逐文件哈希。大型归档以普通 Release 附件提供，不依赖 Git LFS，也不要求登录下载：

1. 克隆本仓库的 `haoke` 分支并切换标签 `3.0.24-Haoke.5`，或下载 Release 的 `vlc-haoke-3.0.24-Haoke.5-source.zip` 并解压到新的源码目录。
2. 下载 `vlc-haoke-3.0.24-Haoke.5-dependency-sources.zip`，先按 `SHA256SUMS` 校验，再解压到同一源码根目录（得到 `依赖/源码包/`）。它保留 **89 份固定依赖源码**及两个固定 Git 归档的侧文件。recipe/patch 位于 `vlc-3.0.24/contrib/src`。
3. 构建当前 Windows 发行时，再下载并校验 `vlc-haoke-3.0.24-Haoke.5-build-inputs.zip`，解压到同一根目录。内含固定 Qt/contrib 二进制工具链与官方 VLC 运行库输入；这些输入**不能代替依赖源码**。
4. 按 [haoke/README.md](haoke/README.md) 配置 Windows Python、WSL Ubuntu/MinGW-w64、Qt 5.6.3 与 Inno Setup，并运行依赖/通知检查后构建。完整来源、资产哈希和安装包指纹见 [公开发行清单](docs/PUBLIC_RELEASE.json)。

分发安装包时，须让接收者同时可取得该版本修改源码、依赖源码与构建材料；不能只提供上游链接。公开源码不包含网站授权码、账户凭据、签名私钥、用户媒体或本机操作回执。正式签名须使用维护者外部受保护身份；第三方可按 GPL 修改构建，并以自己的发行身份发布。已发布版本不可用不同安装包哈希覆盖。

## 构建与测试

```powershell
python -X utf8 haoke/verify_dependency_sources.py
python -X utf8 haoke/prepare_dependency_notices.py --check-only
python -X utf8 -m unittest discover -s haoke/tests -p test_build_setup.py
```

本版已完成针对语言/参数、窗口策略、更新协议/状态/传输、安装构建输入及依赖许可的检查；在隔离目录验收安装、旧版升级、数据保留、卸载以及完整界面的首页/播放/列表/设置/关于页。物理音频、多显示器、所有编解码/HDR/性能和跨机签名恢复不在已验证范围。详细构建入口在 `haoke/`，不要求导出维护者私钥来阅读或修改源码。

## Download / License

Windows downloads are available at [haoke.org](https://haoke.org/software/b92689d5ae8d3982?platform=windows). This is an independent modification of VideoLAN VLC 3.0.24, not an official VideoLAN release. VLC and the Haoke modifications are GPL-2.0-or-later; libVLC files are LGPL-2.1-or-later; dependencies and fonts retain their respective licenses. The complete corresponding source and fixed dependency archives are available in the versioned GitHub Release linked above.
