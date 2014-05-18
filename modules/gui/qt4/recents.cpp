/*****************************************************************************
 * recents.cpp : Recents MRL (menu)
 *****************************************************************************
 * Copyright © 2008-2014 VideoLAN and VLC authors
 * $Id$
 *
 * Authors: Ludovic Fauvet <etix@l0cal.com>
 *          Jean-baptiste Kempf <jb@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "qt4.hpp"
#include "recents.hpp"
#include "dialogs_provider.hpp"
#include "menus.hpp"
#include "util/qt_dirs.hpp"

#include <QStringList>
#include <QRegExp>
#include <QSignalMapper>

#ifdef _WIN32
    #include <shlobj.h>
    /* typedef enum  {
        SHARD_PIDL              = 0x00000001,
        SHARD_PATHA             = 0x00000002,
        SHARD_PATHW             = 0x00000003,
        SHARD_APPIDINFO         = 0x00000004,
        SHARD_APPIDINFOIDLIST   = 0x00000005,
        SHARD_LINK              = 0x00000006,
        SHARD_APPIDINFOLINK     = 0x00000007,
        SHARD_SHELLITEM         = 0x00000008 
    } SHARD; */
    #define SHARD_PATHW 0x00000003

    #include <vlc_charset.h>
#endif

RecentsMRL::RecentsMRL( intf_thread_t *_p_intf ) : p_intf( _p_intf )
{
    stack = new QStringList;

    signalMapper = new QSignalMapper( this );
    CONNECT( signalMapper,
            mapped(const QString & ),
            this,
            playMRL( const QString & ) );

    /* Load the filter psz */
    char* psz_tmp = var_InheritString( p_intf, "qt-recentplay-filter" );
    if( psz_tmp && *psz_tmp )
        filter = new QRegExp( psz_tmp, Qt::CaseInsensitive );
    else
        filter = NULL;
    free( psz_tmp );

    load();
    isActive = var_InheritBool( p_intf, "qt-recentplay" );
    if( !isActive ) clear();
}

RecentsMRL::~RecentsMRL()
{
    delete filter;
    delete stack;
}

void RecentsMRL::addRecent( const QString &mrl )
{
    if ( !isActive || ( filter && filter->indexIn( mrl ) >= 0 ) )
        return;

#ifdef _WIN32
    /* Add to the Windows 7 default list in taskbar */
    char* path = make_path( qtu( mrl ) );
    if( path )
    {
        wchar_t *wmrl = ToWide( path );
        SHAddToRecentDocs( SHARD_PATHW, wmrl );
        free( wmrl );
        free( path );
    }
#endif

    int i_index = stack->indexOf( mrl );
    if( 0 <= i_index )
    {
        /* move to the front */
        stack->move( i_index, 0 );
    }
    else
    {
        stack->prepend( mrl );
        if( stack->count() > RECENTS_LIST_SIZE )
            stack->takeLast();
    }
    VLCMenuBar::updateRecents( p_intf );
    save();
}

void RecentsMRL::clear()
{
    if ( stack->isEmpty() )
        return;

    stack->clear();
    if( isActive ) VLCMenuBar::updateRecents( p_intf );
    save();
}

QStringList RecentsMRL::recents()
{
    return *stack;
}

void RecentsMRL::load()
{
    /* Load from the settings */
    QStringList list = getSettings()->value( "RecentsMRL/list" ).toStringList();

    /* And filter the regexp on the list */
    for( int i = 0; i < list.count(); ++i )
    {
        if ( !filter || filter->indexIn( list.at(i) ) == -1 )
            stack->append( list.at(i) );
    }
}

void RecentsMRL::save()
{
    getSettings()->setValue( "RecentsMRL/list", *stack );
}

playlist_item_t *RecentsMRL::toPlaylist(int length)
{
    playlist_item_t *p_node_recent = playlist_NodeCreate(THEPL, _("Recently Played"), THEPL->p_root, PLAYLIST_END, PLAYLIST_RO_FLAG, NULL);

    if ( p_node_recent == NULL )  return NULL;

    if (length == 0 || stack->count() < length)
        length = stack->count();

    for (int i = 0; i < length; i++)
    {
        input_item_t *p_input = input_item_New(qtu(stack->at(i)), NULL);
        playlist_NodeAddInput(THEPL, p_input, p_node_recent, PLAYLIST_APPEND, PLAYLIST_END, false);
    }

    return p_node_recent;
}

void RecentsMRL::playMRL( const QString &mrl )
{
    Open::openMRL( p_intf, mrl );
}

int Open::openMRL( intf_thread_t *p_intf,
                    const QString &mrl,
                    bool b_start,
                    bool b_playlist)
{
    return openMRLwithOptions( p_intf, mrl, NULL, b_start, b_playlist );
}

int Open::openMRLwithOptions( intf_thread_t* p_intf,
                     const QString &mrl,
                     QStringList *options,
                     bool b_start,
                     bool b_playlist,
                     const char *title)
{
    /* Options */
    const char **ppsz_options = NULL;
    int i_options = 0;

    if( options != NULL && options->count() > 0 )
    {
        ppsz_options = (const char **)malloc( options->count() );
        if( ppsz_options ) {
            for( int j = 0; j < options->count(); j++ ) {
                QString option = colon_unescape( options->at(j) );
                if( !option.isEmpty() ) {
                    ppsz_options[j] = qtu(option);
                    i_options++;
                }
            }
        }
    }

    /* Add to playlist */
    int i_ret = playlist_AddExt( THEPL,
                  qtu(mrl), title,
                  PLAYLIST_APPEND | (b_start ? PLAYLIST_GO : PLAYLIST_PREPARSE),
                  PLAYLIST_END,
                  -1,
                  i_options, ppsz_options, VLC_INPUT_OPTION_TRUSTED,
                  b_playlist,
                  pl_Unlocked );

    /* Add to recent items, only if played */
    if( i_ret == VLC_SUCCESS && b_start && b_playlist )
        RecentsMRL::getInstance( p_intf )->addRecent( mrl );

    return i_ret;
}


