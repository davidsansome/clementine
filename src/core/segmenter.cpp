/* This file is part of Clementine.
   Copyright 2013, Hans Oesterholt <debian@oesterholt.net>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "segmenter.h"
extern "C" {
#include <libmp3splt/mp3splt.h>
}

#include "core/logging.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryFile>
#include <QString>


//////////////////////////////////////////////////////////////////////////////
// Supporting functions
//////////////////////////////////////////////////////////////////////////////

#define NANO_TO_HECTO(a)    (a/10000000)

static bool MakeSegmentMp3Splt(MusicStorage::CopyJob & job, QString & result_file) {
    QFileInfo info(job.source_);
    QString ext=info.suffix().toLower();

    QString templ(QDir::tempPath()+"/clementine_XXXXXX."+ext);
    QTemporaryFile file(templ);
    file.open();
    file.close();
    result_file=file.fileName();
    file.remove();

    QString filename;
    {
        QFileInfo info(result_file);
        filename=info.baseName();
    }

    qLog(Debug) << "Creating temp file " << filename;

    Song song=job.metadata_;
    int begin_offset_in_hs=NANO_TO_HECTO(song.beginning_nanosec());
    int end_offset_in_hs=-1;
    if (song.end_nanosec()>=0)  {
        end_offset_in_hs=NANO_TO_HECTO(song.end_nanosec());
    }

    splt_state *state        = mp3splt_new_state(NULL);
    mp3splt_find_plugins(state);
    mp3splt_set_filename_to_split(state,job.source_.toUtf8().constData());
    mp3splt_set_path_of_split(state,QDir::tempPath().toUtf8().constData());
    mp3splt_set_int_option(state,SPLT_OPT_OUTPUT_FILENAMES,SPLT_OUTPUT_CUSTOM);

    const char *fn=filename.toUtf8().constData();
    mp3splt_append_splitpoint(state,begin_offset_in_hs,fn,SPLT_SPLITPOINT);
    if (end_offset_in_hs>=0) {
        mp3splt_append_splitpoint(state,end_offset_in_hs,"",SPLT_SKIPPOINT);
    }

    {
        char *title=strdup(song.title().toUtf8().constData());
        char *artist=strdup(song.artist().toUtf8().constData());
        char *album=strdup(song.album().toUtf8().constData());
        char *performer=strdup(song.albumartist().toUtf8().constData());
        char *year=strdup(QString().sprintf("%d",song.year()).toUtf8().constData());
        char *comment=strdup(song.comment().toUtf8().constData());
        char *genre=strdup(song.genre().toUtf8().constData());
        int track=song.track();

        mp3splt_append_tags(state,title,artist,album,performer,year,comment,track,genre);

        free(title);
        free(artist);
        free(album);
        free(performer);
        free(year);
        free(comment);
        free(genre);
    }

    int error = SPLT_OK;
    error=mp3splt_split(state);
    int err;
    mp3splt_free_state(state,&err);

    qLog(Debug) << "mp3splt_split result = " << error << "( ok=" << SPLT_OK << ")";

    if (error==SPLT_OK_SPLIT) {
        result_file=QDir::tempPath()+"/"+filename+"."+ext;
        return true;
    } else {
        result_file="";
        return false;
    }
}

static bool MakeSegmentMp3(MusicStorage::CopyJob & job, QString & result_file) {
    return MakeSegmentMp3Splt(job,result_file);
}

static bool MakeSegmentOgg(MusicStorage::CopyJob & job, QString & result_file) {
    return MakeSegmentMp3Splt(job,result_file);
}

//////////////////////////////////////////////////////////////////////////////
// Class implementation
//////////////////////////////////////////////////////////////////////////////

Segmenter::Segmenter(MusicStorage::CopyJob & jb) {
    job_=jb;
}

Segmenter::Type Segmenter::CheckSource() {
    QFileInfo info(job_.source_);
    QString ext=info.suffix().toLower();
    if (ext=="mp3") {
        return Segmenter::Type::MP3;
    } else if (ext=="ogg") {
        return Segmenter::Type::OGG;
    }

    return Segmenter::Type::UNSUPPORTED;
}

bool Segmenter::CanSegment() {
    return CheckSource()!=Segmenter::Type::UNSUPPORTED;
}

bool Segmenter::Create() {
    Segmenter::Type tp=CheckSource();
    QString ext;
    switch (tp) {
        case MP3: ext="mp3";
            return MakeSegmentMp3(job_,segment_file_);
        break;
        case OGG: ext="ogg";
            return MakeSegmentOgg(job_,segment_file_);
        break;
        default:
            return false;
        break;
    }

    return false;
}

QString Segmenter::CreatedFileName() {
    return segment_file_;
}



