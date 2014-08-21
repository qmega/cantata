/*
 * Cantata
 *
 * Copyright (c) 2011-2014 Craig Drummond <craig.p.drummond@gmail.com>
 *
 */
/*
 * Copyright (c) 2008 Sander Knopper (sander AT knopper DOT tk) and
 *                    Roeland Douma (roeland AT rullzer DOT com)
 *
 * This file is part of QtMPC.
 *
 * QtMPC is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * QtMPC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QtMPC.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MUSIC_LIBRARY_ITEM_ARTIST_H
#define MUSIC_LIBRARY_ITEM_ARTIST_H

#include <QList>
#include <QVariant>
#include <QHash>
#include "musiclibraryitem.h"
#include "mpd/song.h"

class QPixmap;
class MusicLibraryItemRoot;
class MusicLibraryItemAlbum;

class MusicLibraryItemArtist : public MusicLibraryItemContainer
{
public:
    static bool lessThan(const MusicLibraryItem *a, const MusicLibraryItem *b);

    MusicLibraryItemArtist(const QString &data, const QString &artistName, MusicLibraryItemContainer *parent = 0);
    virtual ~MusicLibraryItemArtist() { }

    MusicLibraryItemAlbum * album(const Song &s, bool create=true);
    MusicLibraryItemAlbum * createAlbum(const Song &s);
    const QString & baseArtist() const;
    bool isVarious() const { return m_various; }
    bool allSingleTrack() const;
    void addToSingleTracks(MusicLibraryItemArtist *other);
    bool isFromSingleTracks(const Song &s) const;
    void remove(MusicLibraryItemAlbum *album);
    void updateIndexes();
    Type itemType() const { return Type_Artist; }
    #ifdef ENABLE_UBUNTU
    const QString & cover() const;
    #endif
    // 'data' could be 'Composer' if we are set to use that, but need to save real artist...
    const QString & actualArtist() const { return m_actualArtist; }
    #ifdef ENABLE_UBUNTU
    void setCover(const QString &c);
    const QString & coverName() { return m_coverName; }
    #endif
    Song coverSong() const;

private:
    #ifdef ENABLE_UBUNTU
    mutable QString m_coverName;
    mutable bool m_coverRequested;
    #endif
    bool m_various;
    QString m_nonTheArtist;
    QString m_actualArtist;
    QHash<QString, int> m_indexes;
};

#endif
