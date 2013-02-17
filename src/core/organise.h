/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

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

#ifndef ORGANISE_H
#define ORGANISE_H

#include <QBasicTimer>
#include <QObject>
#include <QTemporaryFile>
#include <QUrl>

#include <boost/shared_ptr.hpp>

#include "organiseformat.h"
#include "transcoder/transcoder.h"
#include "core/song.h"


class MusicStorage;
class TaskManager;

//////////////////////////////////////////////////////////////////////
// Instead of communicating files we try as much as possible to
// communicate songs to the organiser. Because, is a song is part
// of a cuesheet, it needs to be segmented and the metadata will be
// in the song, not in the file.
//////////////////////////////////////////////////////////////////////

class SongOrFilePair {
private:
    Song    song_;
    QString file_;
    int     type_;
public:
    SongOrFilePair(const Song & s);
    SongOrFilePair(const QString & f);
    SongOrFilePair();

    Song & song();
    QString & file();

    bool IsFile();
    bool IsSong();

    QString DisplayName();

    // Gets the songs file or file_
    QString GetFile();
};

typedef QList<SongOrFilePair>      SongOrFilePairList;

//////////////////////////////////////////////////////////////////////

class Organise : public QObject {
  Q_OBJECT

public:
  Organise(TaskManager* task_manager,
           boost::shared_ptr<MusicStorage> destination,
           const OrganiseFormat& format, bool copy, bool overwrite,
           const SongOrFilePairList & songOrFiles,
           const  bool eject_after);

  static const int kBatchSize;
  static const int kTranscodeProgressInterval;

  void Start();

signals:
  void Finished(const QStringList& files_with_errors);

protected:
  void timerEvent(QTimerEvent* e);

private slots:
  void ProcessSomeFiles();
  void FileTranscoded(const QString& filename, bool success);

private:
  void SetSongProgress(float progress, bool transcoded = false);
  void UpdateProgress();
  Song::FileType CheckTranscode(Song::FileType original_type) const;

  static QString FiddleFileExtension(const QString& filename, const QString& new_extension);

private:
  struct Task {
      explicit Task(const SongOrFilePair & song_or_file)
      : song_or_file_(song_or_file), transcode_progress_(0.0) {}
      explicit Task()
      : transcode_progress_(0.0) { }

    SongOrFilePair song_or_file_;
    //QString filename_;
    //SegmentPair segment_;

    float transcode_progress_;
    QString transcoded_filename_;
    QString new_extension_;
    Song::FileType new_filetype_;
  };

  QThread* thread_;
  QThread* original_thread_;
  TaskManager* task_manager_;
  Transcoder* transcoder_;
  boost::shared_ptr<MusicStorage> destination_;
  QList<Song::FileType> supported_filetypes_;

  const OrganiseFormat format_;
  const bool copy_;
  const bool overwrite_;
  const bool eject_after_;
  int task_count_;

  QBasicTimer transcode_progress_timer_;
  QTemporaryFile transcode_temp_name_;
  int transcode_suffix_;

  QList<Task> tasks_pending_;
  QMap<QString, Task> tasks_transcoding_;
  int tasks_complete_;

  bool started_;

  int task_id_;
  int current_copy_progress_;

  QStringList files_with_errors_;
};

#endif // ORGANISE_H
