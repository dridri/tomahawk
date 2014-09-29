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

#ifndef MEDIASTREAM_H
#define MEDIASTREAM_H

#include "../Typedefs.h"

#include "DllMacro.h"
#include "utils/Logger.h"
#include <stdint.h>

#include <QUrl>

class DLLEXPORT MediaStream
{

public:
    enum MediaType { Unknown = -1, Empty = 0, Url = 1, Stream = 2 };

    MediaStream();
    MediaStream( const QUrl &url );
    virtual ~MediaStream();

    MediaType type();
    QUrl url();

    void setStreamSize( qint64 size );
    qint64 streamSize();

    virtual void seekStream( qint64 offset ) { (void)offset; }
    virtual size_t needData ( void** buffer ) { (void)buffer; tDebug() << Q_FUNC_INFO; return 0; }

    static int readCallback ( void* data, const char* cookie, int64_t* dts, int64_t* pts, unsigned* flags, size_t* bufferSize, void** buffer );
    static int readDoneCallback ( void *data, const char *cookie, size_t bufferSize, void *buffer );
    static int seekCallback ( void *data, const uint64_t pos );

protected:
    void endOfData();

    MediaType m_type;
    QUrl m_url;

    bool m_eos;
    qint64 m_pos;
    qint64 m_streamSize;
};

#endif // MEDIASTREAM_H
