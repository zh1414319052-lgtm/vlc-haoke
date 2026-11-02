# Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later
"""Derive Haoke names/About copy from the complete upstream VLC catalogs.
No media metadata, placeholder, ASCII protocol context or copyright is renamed.
"""
import polib
from version import VERSION

def catalogs(source, formal=False):
    chinese = polib.pofile(str(source / 'vlc-3.0.24/po/zh_CN.po'), encoding='utf-8',
                          check_for_duplicates=True)
    english = polib.POFile()
    english.metadata = {'Project-Id-Version': 'VLC ' + VERSION,
                        'Language': 'en_US', 'Content-Type': 'text/plain; charset=UTF-8',
                        'Plural-Forms': 'nplurals=2; plural=(n != 1);'}
    abouts = []
    for entry in chinese:
        if entry.msgctxt == 'ASCII':
            continue
        if entry.msgstr:
            entry.msgstr = entry.msgstr.replace('VLC media player', '全能影音播放器')
        if entry.msgid.startswith('<p>VLC media player is a free and open source'):
            abouts.append(entry)
    if not abouts or any(not entry.msgstr for entry in abouts):
        raise RuntimeError('Upstream About contract changed')
    for message, translation in (
        ('Ready when you are', '准备好，开始播放'),
        ('Drop your media here, or choose how to open it', '将影音文件拖到这里，或选择打开方式。'),
        ('Video · Music · Disc · Network Stream', '视频 · 音乐 · 光盘 · 网络串流'),
        ('Network Stream', '网络串流'),
        ('Internet', '互联网'),
        ('<p>This Haoke edition checks signed version information from haoke.org while safely idle in the foreground. These checks do not upload your media list or content; the server still receives ordinary connection metadata such as your IP address. Manual checking is available from Help. Updates are not installed automatically.</p>',
         '<p>此浩科版会在安全的前台空闲状态从 haoke.org 检查签名版本信息，不上传媒体清单或内容；服务器仍会接收 IP 地址等常规连接信息。可从帮助菜单手动检查，不会自动安装更新。</p>'),
        ('Fit window to video aspect ratio', '窗口适配视频显示比例'),
        ("Adjust the window to the video's display aspect ratio while preserving its size budget. Small videos stay large; fullscreen and maximized windows remain unchanged.",
         '根据视频显示比例调整窗口，并保留当前尺寸基准。小分辨率视频保持大窗播放；不改变全屏和最大化窗口。'),
    ):
        existing = chinese.find(message)
        if existing: existing.msgstr = translation
        else: chinese.append(polib.POEntry(msgid=message, msgstr=translation))
    chinese_prefix = (
        '<p><b>' + ('浩科优化版 ' if formal else '浩科 QA 测试版 ') + VERSION + '</b><br/>'
        '基于 VideoLAN VLC 的第三方优化发行，非 VideoLAN 官方发行或背书。<br/>'
        '<a href="https://code.videolan.org/videolan/vlc/">上游源码</a> · '
        '播放器及本版修改：GPL-2.0-or-later；LibVLC：LGPL-2.1-or-later；'
        '其余组件按各自随附许可，详见包内开源说明。<br/>'
        '优化 Windows 启动、语言、显示名称、影院界面与视频比例适配；'
        '播放内核和解码插件保持不变。<br/>'
        '<a href="https://haoke.org">浩科工具站</a></p>')
    english_prefix = (
        '<p><b>' + ('Haoke optimized edition ' if formal else 'Haoke QA ') + VERSION + '</b><br/>'
        'An independent distribution based on VideoLAN VLC; not an official or endorsed VideoLAN release.<br/>'
        '<a href="https://code.videolan.org/videolan/vlc/">Upstream source</a>. '
        'Player and Haoke changes: GPL-2.0-or-later; LibVLC: LGPL-2.1-or-later. '
        'Other components retain their respective bundled licenses; see the included open-source notice.<br/>'
        'Windows startup, language, display-name, cinema UI and aspect-fit window '
        'improvements; the media core and decoder plugins are unchanged.<br/>'
        '<a href="https://haoke.org">Haoke Tools</a></p>')
    for about in abouts:
        about.msgstr = chinese_prefix + about.msgstr
        english.append(polib.POEntry(msgid=about.msgid, msgstr=english_prefix + about.msgid))
    assert chinese.find('VLC media player').msgstr == '全能影音播放器'
    assert chinese.find('VLC media player updates').msgstr == '全能影音播放器 更新'
    return {'zh_CN': chinese, 'en_US': english}
