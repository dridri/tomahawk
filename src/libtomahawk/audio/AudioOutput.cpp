/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2014, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *   Copyright 2010-2012, Jeff Mitchell <jeff@tomahawk-player.org>
 *   Copyright 2013,      Teo Mrnjavac <teo@kde.org>
 *   Copyright 2014,      Adrien Aubry <dridri85@gmail.com>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AudioEngine.h"
#include "AudioOutput.h"

#include "utils/Logger.h"

#include <QApplication>
#include <QVarLengthArray>
#include <QFile>
#include <QDir>

#include <vlc/libvlc.h>
#include <vlc/libvlc_media.h>
#include <vlc/libvlc_media_player.h>
#include <vlc/libvlc_events.h>
#include <vlc/libvlc_version.h>

static QString s_aeInfoIdentifier = QString( "AUDIOOUTPUT" );

static const int ABOUT_TO_FINISH_TIME = 2000;

AudioOutput* AudioOutput::s_instance = 0;


AudioOutput*
AudioOutput::instance()
{
    return AudioOutput::s_instance;
}


AudioOutput::AudioOutput( QObject* parent )
    : QObject( parent )
    , currentState( Stopped )
    , currentStream( nullptr )
    , seekable( true )
    , muted( false )
    , m_autoDelete ( true )
    , m_volume( 1.0 )
    , m_currentTime( 0 )
    , m_totalTime( 0 )
    , m_aboutToFinish( false )
    , m_justSeeked( false )
    , dspPluginCallback( nullptr )
    , vlcInstance( nullptr )
    , vlcPlayer( nullptr )
    , vlcMedia( nullptr )
{
    tDebug() << Q_FUNC_INFO;

    AudioOutput::s_instance = this;

    qRegisterMetaType<AudioOutput::AudioState>("AudioOutput::AudioState");
    
    const char* vlcArgs[] = {
        "--ignore-config",
        "--extraintf=logger",
        qApp->arguments().contains( "--verbose" ) ? "--verbose=3" : "",
        // "--no-plugins-cache",
        // "--no-media-library",
        // "--no-osd",
        // "--no-stats",
        // "--no-video-title-show",
        // "--no-snapshot-preview",
        // "--services-discovery=''",
        "--no-video",
        "--no-xlib"
    };

    // Create and initialize a libvlc instance (it should be done only once)
    vlcInstance = libvlc_new( sizeof(vlcArgs) / sizeof(*vlcArgs), vlcArgs );
    if ( !vlcInstance ) {
        tDebug() << "libVLC: could not initialize";
    }


    vlcPlayer = libvlc_media_player_new( vlcInstance );

    libvlc_event_manager_t* manager = libvlc_media_player_event_manager( vlcPlayer );
    libvlc_event_type_t events[] = {
        libvlc_MediaPlayerMediaChanged,
        libvlc_MediaPlayerNothingSpecial,
        libvlc_MediaPlayerOpening,
        libvlc_MediaPlayerBuffering,
        libvlc_MediaPlayerPlaying,
        libvlc_MediaPlayerPaused,
        libvlc_MediaPlayerStopped,
        libvlc_MediaPlayerForward,
        libvlc_MediaPlayerBackward,
        libvlc_MediaPlayerEndReached,
        libvlc_MediaPlayerEncounteredError,
        libvlc_MediaPlayerTimeChanged,
        libvlc_MediaPlayerPositionChanged,
        libvlc_MediaPlayerSeekableChanged,
        libvlc_MediaPlayerPausableChanged,
        libvlc_MediaPlayerTitleChanged,
        libvlc_MediaPlayerSnapshotTaken,
        //libvlc_MediaPlayerLengthChanged,
        libvlc_MediaPlayerVout
    };
    const int eventCount = sizeof(events) / sizeof( *events );
    for ( int i = 0 ; i < eventCount ; i++ ) {
        libvlc_event_attach( manager, events[ i ], &AudioOutput::vlcEventCallback, this );
    }

    tDebug() << "AudioOutput::AudioOutput OK !\n";
}


AudioOutput::~AudioOutput()
{
    tDebug() << Q_FUNC_INFO;

    if ( vlcPlayer != nullptr ) {
        libvlc_media_player_stop( vlcPlayer );
        libvlc_media_player_release( vlcPlayer );
        vlcPlayer = nullptr;
    }
    if ( vlcMedia != nullptr ) {
        libvlc_media_release( vlcMedia );
        vlcMedia = nullptr;
    }
    if ( vlcInstance != nullptr ) {
        libvlc_release( vlcInstance );
    }
}


void
AudioOutput::setAutoDelete ( bool ad )
{
    m_autoDelete = ad;
}

void
AudioOutput::setCurrentSource( const QUrl& stream )
{
    setCurrentSource( new MediaStream( stream ) );
}


void
AudioOutput::setCurrentSource( QIODevice* stream )
{
    setCurrentSource( new MediaStream( stream ) );
}


void
AudioOutput::setCurrentSource( MediaStream* stream )
{
    tDebug() << Q_FUNC_INFO;

    setState(Loading);

    if ( vlcMedia != nullptr ) {
        // Ensure playback is stopped, then release media
        libvlc_media_player_stop( vlcPlayer );
        libvlc_media_release( vlcMedia );
        vlcMedia = nullptr;
    }
    if ( m_autoDelete && currentStream != nullptr ) {
        delete currentStream;
    }

    currentStream = stream;
    m_totalTime = 0;
    m_currentTime = 0;
    m_justSeeked = false;
    seekable = true;

    QByteArray url;
    switch (stream->type()) {
        case MediaStream::Unknown:
            tDebug() << "MediaStream Type is Invalid:" << stream->type();
            break;

        case MediaStream::Empty:
            tDebug() << "MediaStream is empty.";
            break;

        case MediaStream::Url:
            tDebug() << "MediaStream::Url:" << stream->url();
            if ( stream->url().scheme().isEmpty() ) {
                url = "file:///";
                if ( stream->url().isRelative() ) {
                    url.append( QFile::encodeName( QDir::currentPath() ) + '/' );
                }
            }
            url += stream->url().toEncoded();
            break;

        case MediaStream::Stream:
        case MediaStream::IODevice:
            url = QByteArray( "imem://" );
            break;
    }

    tDebug() << "MediaStream::Final Url:" << url;


    vlcMedia = libvlc_media_new_location( vlcInstance, url.constData() );

    libvlc_event_manager_t* manager = libvlc_media_event_manager( vlcMedia );
    libvlc_event_type_t events[] = {
        libvlc_MediaDurationChanged,
    };
    const int eventCount = sizeof(events) / sizeof( *events );
    for ( int i = 0 ; i < eventCount ; i++ ) {
        libvlc_event_attach( manager, events[ i ], &AudioOutput::vlcEventCallback, this );
    }

    libvlc_media_player_set_media( vlcPlayer, vlcMedia );

    if ( stream->type() == MediaStream::Url )
    {
        m_totalTime = libvlc_media_get_duration( vlcMedia );
    }
    else if ( stream->type() == MediaStream::Stream || stream->type() == MediaStream::IODevice )
    {
        libvlc_media_add_option_flag(vlcMedia, "imem-cat=4", libvlc_media_option_trusted);
        const char* imemData = QString( "imem-data=%1" ).arg( (uintptr_t)stream ).toLatin1().constData();
        libvlc_media_add_option_flag(vlcMedia, imemData, libvlc_media_option_trusted);
        const char* imemGet = QString( "imem-get=%1" ).arg( (uintptr_t)&MediaStream::readCallback ).toLatin1().constData();
        libvlc_media_add_option_flag(vlcMedia, imemGet, libvlc_media_option_trusted);
        const char* imemRelease = QString( "imem-release=%1" ).arg( (uintptr_t)&MediaStream::readDoneCallback ).toLatin1().constData();
        libvlc_media_add_option_flag(vlcMedia, imemRelease, libvlc_media_option_trusted);
        const char* imemSeek = QString( "imem-seek=%1" ).arg( (uintptr_t)&MediaStream::seekCallback ).toLatin1().constData();
        libvlc_media_add_option_flag(vlcMedia, imemSeek, libvlc_media_option_trusted);
    }

    m_aboutToFinish = false;
    setState(Stopped);
}


AudioOutput::AudioState
AudioOutput::state()
{
    tDebug() << Q_FUNC_INFO;
    return currentState;
}


void
AudioOutput::setState( AudioState state )
{
    tDebug() << Q_FUNC_INFO;
    AudioState last = currentState;
    currentState = state;
    emit stateChanged ( state, last );
}


qint64
AudioOutput::currentTime()
{
    return m_currentTime;
}


void
AudioOutput::setCurrentTime( qint64 time )
{
    // FIXME : This is a bit hacky, but m_totalTime is only used to determine
    // if we are about to finish
    if ( m_totalTime == 0 ) {
        m_totalTime = AudioEngine::instance()->currentTrackTotalTime();
        seekable = true;
    }

    m_currentTime = time;
    emit tick( time );

//    tDebug() << "Current time : " << m_currentTime << " / " << m_totalTime;

    // FIXME pt 2 : we use temporary variable to avoid overriding m_totalTime
    // in the case it is < 0 (which means that the media is not seekable)
    qint64 total = m_totalTime;
    if ( total <= 0 ) {
        total = AudioEngine::instance()->currentTrackTotalTime();
    }

    if ( time < total - ABOUT_TO_FINISH_TIME ) {
        m_aboutToFinish = false;
    }
    if ( !m_aboutToFinish && total > 0 && time >= total - ABOUT_TO_FINISH_TIME ) {
        m_aboutToFinish = true;
        emit aboutToFinish();
    }
}


qint64
AudioOutput::totalTime()
{
    return m_totalTime;
}


void
AudioOutput::setTotalTime( qint64 time )
{
    tDebug() << Q_FUNC_INFO << " " << time;

    if ( time <= 0 ) {
        seekable = false;
    } else {
        m_totalTime = time;
        seekable = true;
        // emit current time to refresh total time
        emit tick( time );
    }
}


void
AudioOutput::play()
{
    tDebug() << Q_FUNC_INFO;

    if ( libvlc_media_player_is_playing ( vlcPlayer ) ) {
        libvlc_media_player_set_pause ( vlcPlayer, 0 );
    } else {
        libvlc_media_player_play ( vlcPlayer );
    }

    setState( Playing );
}


void
AudioOutput::pause()
{
    tDebug() << Q_FUNC_INFO;

    libvlc_media_player_set_pause ( vlcPlayer, 1 );

    setState( Paused );
}


void
AudioOutput::stop()
{
    tDebug() << Q_FUNC_INFO;
    libvlc_media_player_stop ( vlcPlayer );

    setState( Stopped );
}


void
AudioOutput::seek( qint64 milliseconds )
{
    tDebug() << Q_FUNC_INFO;

    // Even seek if reported as not seekable. VLC can seek in some cases where
    // it tells us it can't.
    // if ( !seekable ) {
    //     return;
    // }

    switch ( currentState ) {
        case Playing:
        case Paused:
        case Loading:
        case Buffering:
            break;
        default:
            return;
    }

//    tDebug() << "AudioOutput:: seeking" << milliseconds << "msec";

    m_justSeeked = true;
    libvlc_media_player_set_time ( vlcPlayer, milliseconds );
    setCurrentTime( milliseconds );
}


bool
AudioOutput::isSeekable()
{
    tDebug() << Q_FUNC_INFO;

    return seekable;
}


bool
AudioOutput::isMuted()
{
    tDebug() << Q_FUNC_INFO;

    return muted;
}


void
AudioOutput::setMuted(bool m)
{
    tDebug() << Q_FUNC_INFO;

    muted = m;
    if ( muted == true ) {
        libvlc_audio_set_volume( vlcPlayer, 0 );
    } else {
        libvlc_audio_set_volume( vlcPlayer, m_volume * 100.0 );
    }
}


qreal
AudioOutput::volume()
{
    tDebug() << Q_FUNC_INFO;

    return muted ? 0 : m_volume;
}


void 
AudioOutput::setVolume(qreal vol)
{
    tDebug() << Q_FUNC_INFO;

    m_volume = vol;
    if ( !muted ) {
        libvlc_audio_set_volume( vlcPlayer, m_volume * 100.0 );
    }
}


void
AudioOutput::vlcEventCallback( const libvlc_event_t* event, void* opaque )
{
//    tDebug() << Q_FUNC_INFO;

    AudioOutput* that = reinterpret_cast < AudioOutput * > ( opaque );
    Q_ASSERT( that );

    switch (event->type) {
        case libvlc_MediaPlayerTimeChanged:
            that->setCurrentTime( event->u.media_player_time_changed.new_time );
            break;
        case libvlc_MediaPlayerSeekableChanged:
         //   tDebug() << Q_FUNC_INFO << " : seekable changed : " << event->u.media_player_seekable_changed.new_seekable;
            break;
        case libvlc_MediaDurationChanged:
            that->setTotalTime( event->u.media_duration_changed.new_duration );
            break;
        case libvlc_MediaPlayerLengthChanged:
        //    tDebug() << Q_FUNC_INFO << " : length changed : " << event->u.media_player_length_changed.new_length;
            break;
        case libvlc_MediaPlayerNothingSpecial:
        case libvlc_MediaPlayerOpening:
        case libvlc_MediaPlayerBuffering:
        case libvlc_MediaPlayerPlaying:
        case libvlc_MediaPlayerPaused:
        case libvlc_MediaPlayerStopped:
            break;
        case libvlc_MediaPlayerEndReached:
            that->setState(Stopped);
            break;
        case libvlc_MediaPlayerEncounteredError:
            tDebug() << "LibVLC error : MediaPlayerEncounteredError. Stopping";
            if ( that->vlcPlayer != 0 ) {
                that->stop();
            }
            that->setState( Error );
            break;
        case libvlc_MediaPlayerVout:
        case libvlc_MediaPlayerMediaChanged:
        case libvlc_MediaPlayerForward:
        case libvlc_MediaPlayerBackward:
        case libvlc_MediaPlayerPositionChanged:
        case libvlc_MediaPlayerPausableChanged:
        case libvlc_MediaPlayerTitleChanged:
        case libvlc_MediaPlayerSnapshotTaken:
        default:
            break;
    }
}


void
AudioOutput::s_dspCallback( int frameNumber, float* samples, int nb_channels, int nb_samples )
{
//    tDebug() << Q_FUNC_INFO;

    int state = AudioOutput::instance()->m_justSeeked ? 1 : 0;
    AudioOutput::instance()->m_justSeeked = false;
    if ( AudioOutput::instance()->dspPluginCallback ) {
        AudioOutput::instance()->dspPluginCallback( state, frameNumber, samples, nb_channels, nb_samples );
    }
}


void
AudioOutput::setDspCallback( std::function< void( int, int, float*, int, int ) > cb )
{
    dspPluginCallback = cb;
}
