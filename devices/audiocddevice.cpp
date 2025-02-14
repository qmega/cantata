/*
 * Cantata
 *
 * Copyright (c) 2011-2021 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "audiocddevice.h"
#ifdef CDDB_FOUND
#include "cddbinterface.h"
#endif
#ifdef MUSICBRAINZ5_FOUND
#include "musicbrainz.h"
#endif
#include "models/musiclibraryitemsong.h"
#include "models/mpdlibrarymodel.h"
#include "models/playqueuemodel.h"
#include "support/utils.h"
#include "extractjob.h"
#include "mpd-interface/mpdconnection.h"
#include "gui/covers.h"
#include "gui/settings.h"
#include "widgets/icons.h"
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include "solid-lite/block.h"

const QLatin1String AudioCdDevice::constAnyDev("-");

QString AudioCdDevice::coverUrl(QString udi)
{
    udi.replace(" ", "_");
    udi.replace("\n", "_");
    udi.replace("\t", "_");
    udi.replace("/", "_");
    udi.replace(":", "_");
    return Song::constCddaProtocol+udi;
}

QString AudioCdDevice::getDevice(const QUrl &url)
{
    if (QLatin1String("cdda")==url.scheme()) {
        QUrlQuery q(url);
        if (q.hasQueryItem("dev")) {
            return q.queryItemValue("dev");
        }
        return constAnyDev;
    }

    QString path=url.path();
    if (path.startsWith("/run/user/")) {
        const QString marker=QLatin1String("/gvfs/cdda:host=");
        int pos=path.lastIndexOf(marker);
        if (-1!=pos) {
            return QLatin1String("/dev/")+path.mid(pos+marker.length());
        }
    }
    return QString();
}

AudioCdDevice::AudioCdDevice(MusicLibraryModel *m, Solid::Device &dev)
    : Device(m, dev, false, true)
    #ifdef CDDB_FOUND
    , cddb(0)
    #endif
    #ifdef MUSICBRAINZ5_FOUND
    , mb(0)
    #endif
    , year(0)
    , disc(0)
    , time(0xFFFFFFFF)
    , lookupInProcess(false)
    , autoPlay(false)
{
    icn=Icons::self()->albumMonoIcon;
    drive=dev.parent().as<Solid::OpticalDrive>();
    Solid::Block *block=dev.as<Solid::Block>();
    if (block) {
        device=block->device();
    } else { // With UDisks2 we cannot get block from device :-(
        QStringList parts=dev.udi().split("/", CANTATA_SKIP_EMPTY);
        if (!parts.isEmpty()) {
            parts=parts.last().split(":");
            if (!parts.isEmpty()) {
                device="/dev/"+parts.first();
            }
        }
    }
    if (!device.isEmpty()) {
        static bool registeredTypes=false;
        if (!registeredTypes) {
            qRegisterMetaType<CdAlbum >("CdAlbum");
            qRegisterMetaType<QList<CdAlbum> >("QList<CdAlbum>");
            registeredTypes=true;
        }
        devPath=Song::constCddaProtocol+device+QChar('/');
        #if defined CDDB_FOUND && defined MUSICBRAINZ5_FOUND
        connectService(Settings::self()->useCddb());
        #else
        connectService(true);
        #endif
        detailsString=tr("Reading disc");
        setStatusMessage(detailsString);
        lookupInProcess=true;
        connect(Covers::self(), SIGNAL(cover(const Song &, const QImage &, const QString &)),
                this, SLOT(setCover(const Song &, const QImage &, const QString &)));
        emit lookup(Settings::self()->cdAuto());
    }
}

AudioCdDevice::~AudioCdDevice()
{
    #ifdef CDDB_FOUND
    if (cddb) {
        cddb->deleteLater();
        cddb=0;
    }
    #endif
    #ifdef MUSICBRAINZ5_FOUND
    if (mb) {
        mb->deleteLater();
        mb=0;
    }
    #endif
    // Remove any downloaded cover image...
    if (!coverImage.fileName.isEmpty() && coverImage.fileName.startsWith(Utils::cacheDir(Covers::constCddaCoverDir, false))) {
        QFile::remove(coverImage.fileName);
    }
}

void AudioCdDevice::dequeue()
{
    QList<Song> tracks;
    for (const MusicLibraryItem *item: childItems()) {
        if (MusicLibraryItem::Type_Song==item->itemType()) {
            Song song=static_cast<const MusicLibraryItemSong *>(item)->song();
            song.file=path()+song.file;
            tracks.append(song);
        }
    }

    if (!tracks.isEmpty()) {
        PlayQueueModel::self()->remove(tracks);
    }
}

bool AudioCdDevice::isAudioDevice(const QString &dev) const
{
    return constAnyDev==dev || device==dev;
}

void AudioCdDevice::connectService(bool useCddb)
{
    #if defined CDDB_FOUND && defined MUSICBRAINZ5_FOUND
    if (cddb && !useCddb) {
        cddb->deleteLater();
        cddb=0;
    }
    if (mb && useCddb) {
        mb->deleteLater();
        mb=0;
    }
    #else
    Q_UNUSED(useCddb)
    #endif

    #ifdef CDDB_FOUND
    if (!cddb
            #ifdef MUSICBRAINZ5_FOUND
            && useCddb
            #endif
            ) {
        cddb=new CddbInterface(device);
        connect(cddb, SIGNAL(error(QString)), this, SIGNAL(error(QString)));
        connect(cddb, SIGNAL(initialDetails(CdAlbum)), this, SLOT(setDetails(CdAlbum)));
        connect(cddb, SIGNAL(matches(const QList<CdAlbum> &)), SLOT(cdMatches(const QList<CdAlbum> &)));
        connect(this, SIGNAL(lookup(bool)), cddb, SLOT(lookup(bool)));
    }
    #endif

    #ifdef MUSICBRAINZ5_FOUND
    if (!mb
            #ifdef CDDB_FOUND
            && !useCddb
            #endif
            ) {
        mb=new MusicBrainz(device);
        connect(mb, SIGNAL(error(QString)), this, SIGNAL(error(QString)));
        connect(mb, SIGNAL(initialDetails(CdAlbum)), this, SLOT(setDetails(CdAlbum)));
        connect(mb, SIGNAL(matches(const QList<CdAlbum> &)), SLOT(cdMatches(const QList<CdAlbum> &)));
        connect(this, SIGNAL(lookup(bool)), mb, SLOT(lookup(bool)));
    }
    #endif
}

void AudioCdDevice::rescan(bool useCddb)
{
    if (!device.isEmpty()) {
        connectService(useCddb);
        lookupInProcess=true;
        emit lookup(true);
    }
}

void AudioCdDevice::toggle()
{
    if (drive) {
        stop();
        drive->eject();
        PlayQueueModel::self()->removeCantataStreams(true);
    }
}

void AudioCdDevice::stop()
{
}

void AudioCdDevice::copySongTo(const Song &s, const QString &musicPath, bool overwrite, bool copyCover)
{
    jobAbortRequested=false;
    if (!isConnected()) {
        emit actionStatus(NotConnected);
        return;
    }

    needToFixVa=opts.fixVariousArtists && s.isVariousArtists();

    if (!overwrite) {
        Song check=s;

        if (needToFixVa) {
            Device::fixVariousArtists(QString(), check, false);
        }
        if (MpdLibraryModel::self()->songExists(check)) {
            emit actionStatus(SongExists);
            return;
        }
    }

    DeviceOptions mpdOpts;
    mpdOpts.load(MPDConnectionDetails::configGroupName(MPDConnection::self()->getDetails().name), true);

    Encoders::Encoder encoder=Encoders::getEncoder(mpdOpts.transcoderCodec);
    if (encoder.codec.isEmpty()) {
        emit actionStatus(CodecNotAvailable);
        return;
    }

    QString source=device;
    QString baseDir=MPDConnection::self()->getDetails().dir;
    currentDestFile=encoder.changeExtension(baseDir+musicPath);
    QDir dir(Utils::getDir(currentDestFile));
    if (!dir.exists() && !Utils::createWorldReadableDir(dir.absolutePath(), baseDir)) {
        emit actionStatus(DirCreationFaild);
        return;
    }

    currentSong=s;
    ExtractJob *job=new ExtractJob(encoder, mpdOpts.transcoderValue, source, currentDestFile, currentSong, copyCover ? coverImage.fileName : QString());
    connect(job, SIGNAL(result(int)), SLOT(copySongToResult(int)));
    connect(job, SIGNAL(percent(int)), SLOT(percent(int)));
    job->start();
}

quint32 AudioCdDevice::totalTime()
{
    if (0xFFFFFFFF==time) {
        time=0;
        for (MusicLibraryItem *i: childItems()) {
            time+=static_cast<MusicLibraryItemSong *>(i)->song().time;
        }
    }

    return time;
}

void AudioCdDevice::percent(int pc)
{
    if (jobAbortRequested && 100!=pc) {
        FileJob *job=qobject_cast<FileJob *>(sender());
        if (job) {
            job->stop();
        }
        return;
    }
    emit progress(pc);
}

void AudioCdDevice::copySongToResult(int status)
{
    ExtractJob *job=qobject_cast<ExtractJob *>(sender());
    FileJob::finished(job);
    if (jobAbortRequested) {
        if (job && job->wasStarted() && QFile::exists(currentDestFile)) {
            QFile::remove(currentDestFile);
        }
        return;
    }
    if (Ok!=status) {
        emit actionStatus(status);
    } else {
        currentSong.file=currentDestFile.mid(MPDConnection::self()->getDetails().dir.length());
        QString origPath;
        if (MPDConnection::self()->isMopidy()) {
            origPath=currentSong.file;
            currentSong.file=Song::encodePath(currentSong.file);
        }
        if (needToFixVa) {
            currentSong.revertVariousArtists();
        }
        Utils::setFilePerms(currentDestFile);
//        MusicLibraryModel::self()->addSongToList(currentSong);
//        DirViewModel::self()->addFileToList(origPath.isEmpty() ? currentSong.file : origPath,
//                                            origPath.isEmpty() ? QString() : currentSong.file);
        emit actionStatus(Ok, job && job->coverCopied());
    }
}

static const int constBytesPerSecond=44100*4;

void AudioCdDevice::setDetails(const CdAlbum &a)
{
    bool differentAlbum=album!=a.name || artist!=a.artist;
    lookupInProcess=false;
    setData(a.artist);
    album=a.name;
    artist=a.artist;
    composer=a.composer;
    genre=a.genre;
    year=a.year;
    disc=a.disc;
    update=new MusicLibraryItemRoot();
    int totalDuration=0;
    for (Song s: a.tracks) {
        totalDuration+=s.time;
        s.size=s.time*constBytesPerSecond;
        update->append(new MusicLibraryItemSong(s, update));
    }
    setStatusMessage(QString());
    detailsString=tr("%n Tracks (%1)", "", a.tracks.count()).arg(Utils::formatTime(totalDuration));
    emit updating(id(), false);
    if (differentAlbum && !a.isDefault) {
        Song s;
        s.artist=s.albumartist=artist;
        s.album=album;
        s.file=AudioCdDevice::coverUrl(id());
        s.title=id();
        s.type=Song::Cdda;
        Covers::Image img=Covers::self()->requestImage(s, true);
        if (!img.img.isNull()) {
            setCover(img);
        }
    }

    if (autoPlay) {
        autoPlay=false;
        playTracks();
    } else {
        updateDetails();
    }
}

void AudioCdDevice::cdMatches(const QList<CdAlbum> &albums)
{
    lookupInProcess=false;
    if (1==albums.count()) {
        setDetails(albums.at(0));
    } else if (albums.count()>1) {
        // More than 1 match, so prompt user!
        emit matches(id(), albums);
    }
}

void AudioCdDevice::setCover(const Covers::Image &img)
{
    coverImage=img;
    updateStatus();
}

void AudioCdDevice::scaleCoverPix(int size) const
{
    if (!coverImage.img.isNull()) {
        if (scaledCover.width()!=size && scaledCover.height()!=size) {
            scaledCover=QPixmap::fromImage(coverImage.img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}

void AudioCdDevice::setCover(const Song &song, const QImage &img, const QString &file)
{
    if (song.isCdda() && song.albumartist==artist && song.album==album) {
        setCover(Covers::Image(img, file));
    }
}

void AudioCdDevice::autoplay()
{
    if (childCount()) {
        playTracks();
    } else {
        autoPlay=true;
    }
}

void AudioCdDevice::playTracks()
{
    QList<Song> tracks;
    for (const MusicLibraryItem *item: childItems()) {
        if (MusicLibraryItem::Type_Song==item->itemType()) {
            Song song=static_cast<const MusicLibraryItemSong *>(item)->song();
            song.file=path()+song.file;
            tracks.append(song);
        }
    }

    if (!tracks.isEmpty()) {
        emit play(tracks);
    }
}

void AudioCdDevice::updateDetails()
{
    QList<Song> tracks;
    for (const MusicLibraryItem *item: childItems()) {
        if (MusicLibraryItem::Type_Song==item->itemType()) {
            Song song=static_cast<const MusicLibraryItemSong *>(item)->song();
            song.file=path()+song.file;
            tracks.append(song);
        }
    }

    if (!tracks.isEmpty()) {
        emit updatedDetails(tracks);
    }
}

#include "moc_audiocddevice.cpp"
