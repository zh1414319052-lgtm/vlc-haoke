/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
// SPDX-License-Identifier: GPL-2.0-or-later
// Haoke cinema UI. Noto Sans CJK SC: SIL OFL 1.1, commercial use allowed.
#ifndef HAOKE_UI_THEME_HPP
#define HAOKE_UI_THEME_HPP
#include <QApplication>
#include <QFontDatabase>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QRegExp>
#include <QLocale>
#include <QVariant>
#include <QMenu>
#include <QDesktopWidget>
#include <cmath>

namespace HaokeUI {
inline bool dark() { return qApp->property("haokeCinemaDark").toBool(); }
inline qreal scale() {
    const qreal s = qApp->property("haokeCinemaScale").toReal();
    return s > 0 ? s : 1.0;
}
inline qreal metrics() {
    const qreal s = qApp->property("haokeCinemaMetrics").toReal();
    return s > 0 ? s : 1.0;
}
inline int px(qreal value) { return qRound(value * scale() * metrics()); }
inline QString localized(const char *zh, const char *en) {
    return QLocale().language() == QLocale::Chinese ? QString::fromUtf8(zh) : QString::fromUtf8(en);
}
inline QIcon icon(const QString &resource, bool primary = false) {
    QIcon original(resource), result;
    const qreal dpr = qApp->primaryScreen() ? qApp->primaryScreen()->devicePixelRatio() : 1.0;
    const int sizes[] = {16,20,24,26,32,40,48,64};
    for (int size : sizes) {
        for (int mode = 0; mode < 2; ++mode) {
            QPixmap pixmap = original.pixmap(qRound(size*dpr), qRound(size*dpr));
            if (pixmap.isNull()) continue;
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            const QColor color = mode ? QColor(dark() ? "#626d7b" : "#8b939d") :
                (primary ? QColor("#101419") : qApp->palette().color(QPalette::WindowText));
            painter.fillRect(pixmap.rect(), color);
            painter.end();
            pixmap.setDevicePixelRatio(dpr);
            result.addPixmap(pixmap, mode ? QIcon::Disabled : QIcon::Normal);
        }
    }
    return result;
}
inline void popupAbove(QMenu *menu, QWidget *anchorWidget) {
    menu->ensurePolished();
    const QSize popup = menu->sizeHint();
    const QRect work = QApplication::desktop()->availableGeometry(anchorWidget);
    const QPoint anchor = anchorWidget->mapToGlobal(QPoint(anchorWidget->width()/2,0));
    const int x = qBound(work.left(),anchor.x()-popup.width()/2,
                         qMax(work.left(),work.right()-popup.width()+1));
    const int y = qBound(work.top(),anchor.y()-popup.height()-px(8),
                         qMax(work.top(),work.bottom()-popup.height()+1));
    menu->exec(QPoint(x,y));
}
inline bool install(QApplication &app, bool useDark) {
    const int fontId = QFontDatabase::addApplicationFont(":/haoke/NotoSansCJKsc-Regular.otf");
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) return false; // no unlicensed fallback in a shipped build
    qreal s = 1.0, metricScale = 1.0;
    if (app.primaryScreen()) {
        const QSize work = app.primaryScreen()->availableGeometry().size();
        // Existing product authorization: 64% area and media aspect-fit, retained.
        // Qt 5.6 rounds fractional automatic DPR. Its point fonts still use
        // logical DPI; convert pixel metrics exactly once to the same units.
        metricScale = app.primaryScreen()->logicalDotsPerInch()/96.0;
        s = qBound(qreal(0.90), std::sqrt(work.width()*qreal(work.height())*0.64/(1280.*800.))/metricScale, qreal(1.20));
    }
    app.setProperty("haokeCinemaScale", s);
    app.setProperty("haokeCinemaMetrics", metricScale);
    app.setProperty("haokeCinemaDark", useDark);
    QFont font(families.first()); font.setPointSizeF(10.0*s);
    app.setFont(font);
    if (useDark) app.setStyle("Fusion");
    if (!useDark) return true;
    QPalette palette;
    const QPalette::ColorGroup groups[] = {QPalette::Active,QPalette::Inactive,QPalette::Disabled};
    for (QPalette::ColorGroup group : groups) {
        const bool disabled = group == QPalette::Disabled;
        palette.setColor(group,QPalette::Window,QColor("#191f27"));
        palette.setColor(group,QPalette::Base,QColor("#101419"));
        palette.setColor(group,QPalette::AlternateBase,QColor("#151b22"));
        palette.setColor(group,QPalette::Button,QColor("#212a35"));
        palette.setColor(group,QPalette::WindowText,QColor(disabled ? "#626d7b" : "#edf0f4"));
        palette.setColor(group,QPalette::Text,QColor(disabled ? "#626d7b" : "#edf0f4"));
        palette.setColor(group,QPalette::ButtonText,QColor(disabled ? "#626d7b" : "#edf0f4"));
        palette.setColor(group,QPalette::Highlight,QColor(disabled ? "#28313c" : "#ff963b"));
        palette.setColor(group,QPalette::HighlightedText,QColor("#101419"));
        palette.setColor(group,QPalette::Link,QColor("#ff963b"));
        palette.setColor(group,QPalette::Light,QColor("#3c4857"));
        palette.setColor(group,QPalette::Mid,QColor("#2b333d"));
        palette.setColor(group,QPalette::Dark,QColor("#101419"));
    }
    app.setPalette(palette);
    QFile file(":/haoke/theme.qss");
    if (!file.open(QIODevice::ReadOnly)) return false;
    QString css = QString::fromUtf8(file.readAll());
    QRegExp dimensions("@([0-9]+)@");
    int pos = 0;
    while ((pos = dimensions.indexIn(css,pos)) >= 0) {
        const QString replacement = QString::number(px(dimensions.cap(1).toInt())) + "px";
        css.replace(pos, dimensions.matchedLength(), replacement);
        pos += replacement.size();
    }
    app.setStyleSheet(css);
    return true;
}
}
#endif
