/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2014, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *   Copyright 2010-2012, Jeff Mitchell <jeff@tomahawk-player.org>
 *   Copyright 2013,      Teo Mrnjavac <teo@kde.org>
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

#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include "../Typedefs.h"

#include "DllMacro.h"

#include "utils/MediaStream.h"

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

class DLLEXPORT AudioOutput : public QObject
{
Q_OBJECT

public:
    enum AudioState { Stopped = 0, Playing = 1, Paused = 2, Error = 3, Loading = 4, Buffering = 5 };

    explicit AudioOutput(QObject* parent = 0);
    ~AudioOutput();

    AudioState state();

    void setCurrentSource(MediaStream stream);
    void setCurrentSource(MediaStream* stream);

    void play();
    void pause();
    void stop();
    void seek(qint64 milliseconds);

    bool isMuted();
    void setMuted(bool m);
    void setVolume(qreal vol);
    qreal volume();

    void setDspCallback( void ( *cb ) ( signed short*, int, int ) );

    static AudioOutput* instance();

public slots:

signals:
    void stateChanged( AudioOutput::AudioState, AudioOutput::AudioState );

private:
    void setState( AudioState state );

    static void s_dspCallback( signed short* samples, int nb_channels, int nb_samples );
    void dspCallback( signed short* samples, int nb_channels, int nb_samples );

    void ( *dspPluginCallback ) ( signed short* samples, int nb_channels, int nb_samples );

    static AudioOutput* s_instance;
    AudioState currentState;
    MediaStream* currentStream;
    bool muted;
    qreal m_volume;

    libvlc_instance_t* vlcInstance;
    libvlc_media_player_t* vlcPlayer;
    libvlc_media_t* vlcMedia;
};

#endif // AUDIOOUTPUT_H
