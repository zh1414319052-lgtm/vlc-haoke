/* Modified by Haoke, 2026-11-02: bounded first-run layout and edition-specific update disclosure. */
/*****************************************************************************
 * firstrun.cpp : First Run dialogs
 ****************************************************************************
 * Copyright © 2009 VideoLAN
 * $Id$
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "dialogs/firstrun.hpp"
#include "../../../../../haoke/ui_theme.hpp"
#include <QTimer>

#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>

FirstRun::FirstRun( QWidget *_p, intf_thread_t *_p_intf  )
         : QVLCDialog( _p, _p_intf), p_intf( _p_intf )
{
    msg_Dbg( p_intf, "Boring first Run Wizard" );
    buildPrivDialog();
    const auto fitToWorkArea = [this]() {
        const QRect work = QApplication::desktop()->availableGeometry(this);
        setMinimumWidth(qMin(HaokeUI::px(480), work.width() - HaokeUI::px(32)));
        adjustSize();
        resize(size().boundedTo(work.size() - QSize(HaokeUI::px(16), HaokeUI::px(32))));
        move(work.center() - rect().center());
    };
    fitToWorkArea();
    setVisible( true );
    QTimer::singleShot(0, this, fitToWorkArea);
}

void FirstRun::save()
{
    config_PutInt( p_intf,  "metadata-network-access", checkbox->isChecked() );
#if defined(UPDATE_CHECK) && !defined(HAOKE_FORMAL_UPDATES)
    config_PutInt( p_intf,  "qt-updates-notif", checkbox2->isChecked() );
#endif
    config_PutInt( p_intf,  "qt-privacy-ask", 0 );

    /* FIXME Should not save here. This will not work as expected if another
     * plugin overwrote items of its own. */
#warning FIXME
    /* We have to save here because the user may not launch Prefs */
    config_SaveConfigFile( p_intf );
    close();
}

void FirstRun::buildPrivDialog()
{
    setWindowTitle( qtr( "Privacy and Network Access Policy" ) );
    setWindowRole( "vlc-privacy" );
    setWindowModality( Qt::ApplicationModal );
    setWindowFlags( Qt::Dialog );
    setAttribute( Qt::WA_DeleteOnClose );

    QGridLayout *gLayout = new QGridLayout( this );

    QGroupBox *blabla = new QGroupBox( qtr( "Privacy and Network Access Policy" ) );
    QGridLayout *blablaLayout = new QGridLayout( blabla );
    QLabel *text = new QLabel( qtr(
        "<p>In order to protect your privacy, <i>VLC media player</i> "
        "does <b>not</b> collect personal data or transmit them, "
        "not even in anonymized form, to anyone."
        "</p>\n"
        "<p>Nevertheless, <i>VLC</i> is able to automatically retrieve "
        "information about the media in your playlist from third party "
        "Internet-based services. This includes cover art, track names, "
        "artist names and other meta-data."
        "</p>\n"
        "<p>Consequently, this may entail identifying some of your media files to third party "
        "entities. Therefore the <i>VLC</i> developers require your express "
        "consent for the media player to access the Internet automatically."
        "</p>\n"
        ) );
    text->setWordWrap( true );
    text->setTextFormat( Qt::RichText );
#ifdef HAOKE_FORMAL_UPDATES
    text->setText(text->text() + qtr("<p>This Haoke edition checks signed version information from haoke.org while safely idle in the foreground. These checks do not upload your media list or content; the server still receives ordinary connection metadata such as your IP address. Manual checking is available from Help. Updates are not installed automatically.</p>"));
#endif

    blablaLayout->addWidget( text, 0, 0 ) ;

    QGroupBox *options = new QGroupBox( qtr( "Network Access Policy" ) );
    QGridLayout *optionsLayout = new QGridLayout( options );

    gLayout->addWidget( blabla, 0, 0, 1, 3 );
    gLayout->addWidget( options, 1, 0, 1, 3 );
    int line = 0;

    checkbox = new QCheckBox( qtr( "Allow metadata network access" ) );
    checkbox->setChecked( true );
    optionsLayout->addWidget( checkbox, line++, 0 );

#if defined(UPDATE_CHECK) && !defined(HAOKE_FORMAL_UPDATES)
    checkbox2 = new QCheckBox( qtr( "Regularly check for VLC updates" ) );
    checkbox2->setChecked( true );
    optionsLayout->addWidget( checkbox2, line++, 0 );
#endif

    QDialogButtonBox *buttonsBox = new QDialogButtonBox( this );
    buttonsBox->addButton( qtr( "Continue" ), QDialogButtonBox::AcceptRole );

    gLayout->addWidget( buttonsBox, 2, 0, 2, 3 );

    connect( buttonsBox, &QDialogButtonBox::accepted, this, &FirstRun::save );
    buttonsBox->setFocus();
}
