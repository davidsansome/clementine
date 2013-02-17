/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>
   2013, Hans Oesterholt <debian@oesterholt.net>

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

#include "musicstorage.h"
#include "organise.h"
#include "taskmanager.h"
#include "core/logging.h"
#include "core/tagreaderclient.h"
#include "core/segmenter.h"
#include "core/song.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QUrl>

#include <boost/bind.hpp>

const int Organise::kBatchSize = 10;
const int Organise::kTranscodeProgressInterval = 500;

Organise::Organise(TaskManager* task_manager,
                   boost::shared_ptr<MusicStorage> destination,
                   const OrganiseFormat &format, bool copy, bool overwrite,
                   //const QStringList& files, const SegmentPairList & segments,
                   const SongOrFilePairList & songs_or_files,
                   bool eject_after)
                     : thread_(NULL),
                       task_manager_(task_manager),
                       transcoder_(new Transcoder(this)),
                       destination_(destination),
                       format_(format),
                       copy_(copy),
                       overwrite_(overwrite),
                       eject_after_(eject_after),
                       task_count_(songs_or_files.count()),
                       transcode_suffix_(1),
                       tasks_complete_(0),
                       started_(false),
                       task_id_(0),
                       current_copy_progress_(0)
{
  original_thread_ = thread();

  foreach (const SongOrFilePair & sof, songs_or_files ) {
      tasks_pending_ << Task(sof);
  }
}

void Organise::Start() {
  if (thread_)
    return;

  task_id_ = task_manager_->StartTask(tr("Organising files"));
  task_manager_->SetTaskBlocksLibraryScans(true);

  thread_ = new QThread;
  connect(thread_, SIGNAL(started()), SLOT(ProcessSomeFiles()));
  connect(transcoder_, SIGNAL(JobComplete(QString,bool)), SLOT(FileTranscoded(QString,bool)));

  moveToThread(thread_);
  thread_->start();
}

void Organise::ProcessSomeFiles() {
  if (!started_) {
    transcode_temp_name_.open();

    if (!destination_->StartCopy(&supported_filetypes_)) {
      // Failed to start - mark everything as failed :(
      foreach (const Task& task, tasks_pending_) {
          SongOrFilePair sp=task.song_or_file_;
          files_with_errors_ << sp.DisplayName();
      }
      tasks_pending_.clear();
    }
    started_ = true;
  }

  // None left?
  if (tasks_pending_.isEmpty()) {
    if (!tasks_transcoding_.isEmpty()) {
      // Just wait - FileTranscoded will start us off again in a little while
      qLog(Debug) << "Waiting for transcoding jobs";
      transcode_progress_timer_.start(kTranscodeProgressInterval, this);
      return;
    }

    UpdateProgress();

    destination_->FinishCopy(files_with_errors_.isEmpty());
    if (eject_after_)
      destination_->Eject();

    task_manager_->SetTaskFinished(task_id_);

    emit Finished(files_with_errors_);

    // Move back to the original thread so deleteLater() can get called in
    // the main thread's event loop
    moveToThread(original_thread_);
    deleteLater();

    // Stop this thread
    thread_->quit();
    return;
  }

  // We process files in batches so we can be cancelled part-way through.
  for (int i=0 ; i<kBatchSize ; ++i) {
    SetSongProgress(0);

    if (tasks_pending_.isEmpty())
      break;

    Task task = tasks_pending_.takeFirst();
    SongOrFilePair song_or_file=task.song_or_file_;
    //qLog(Info) << "Processing" << task.filename_;

    qLog(Info) << "Processing " << song_or_file.DisplayName();

    // Is it a directory?
    if (song_or_file.IsFile() && QFileInfo(song_or_file.file()).isDir()) {
      QDir dir(song_or_file.file());
      foreach (const QString& entry, dir.entryList(
          QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable)) {
        SongOrFilePair p(song_or_file.file()+"/"+entry);
        tasks_pending_ << Task(p);
        task_count_ ++;
      }
      continue;
    }

    // Read metadata from the file
    Song song;
    if (song_or_file.IsFile()) {
        TagReaderClient::Instance()->ReadFileBlocking(song_or_file.file(), &song);
        if (!song.is_valid())
          continue;
    } else {
        song=song_or_file.song();
    }

    // Maybe this file is one that's been transcoded already?
    if (!task.transcoded_filename_.isEmpty()) {
      qLog(Debug) << "This file has already been transcoded";

      // Set the new filetype on the song so the formatter gets it right
      song.set_filetype(task.new_filetype_);

      // Fiddle the filename extension as well to match the new type
      song.set_url(QUrl::fromLocalFile(FiddleFileExtension(song.url().toLocalFile(), task.new_extension_)));
      song.set_basefilename(FiddleFileExtension(song.basefilename(), task.new_extension_));

      // Have to set this to the size of the new file or else funny stuff happens
      song.set_filesize(QFileInfo(task.transcoded_filename_).size());
    } else {
      // Figure out if we need to transcode it
      Song::FileType dest_type = CheckTranscode(song.filetype());
      if (dest_type != Song::Type_Unknown) {
        // Get the preset
        TranscoderPreset preset = Transcoder::PresetForFileType(dest_type);
        qLog(Debug) << "Transcoding with" << preset.name_;

        // Get a temporary name for the transcoded file
        task.transcoded_filename_ = transcode_temp_name_.fileName() + "-" +
                                    QString::number(transcode_suffix_++);
        task.new_extension_ = preset.extension_;
        task.new_filetype_ = dest_type;
        //tasks_transcoding_[task.filename_] = task;
        tasks_transcoding_[song_or_file.file()] = task;

        qLog(Debug) << "Transcoding to" << task.transcoded_filename_;

        // Start the transcoding - this will happen in the background and
        // FileTranscoded() will get called when it's done.  At that point the
        // task will get re-added to the pending queue with the new filename.
        //transcoder_->AddJob(task.filename_, preset, task.transcoded_filename_);
        QString filename=(song_or_file.IsFile()) ? song_or_file.file() :
                                                   song_or_file.song().url().toLocalFile();
        transcoder_->AddJob(filename, preset, task.transcoded_filename_);
        transcoder_->Start();
        continue;
      }
    }


    MusicStorage::CopyJob job;
    QString _filename=song_or_file.IsFile() ? song_or_file.file() :
                                              song_or_file.song().url().toLocalFile();
    job.source_ = task.transcoded_filename_.isEmpty() ?
                  _filename : task.transcoded_filename_;
    job.destination_ = format_.GetFilenameForSong(song);
    job.metadata_ = song;
    job.overwrite_ = overwrite_;
    job.remove_original_ = !copy_;
    job.progress_ = boost::bind(&Organise::SetSongProgress,
                                this, _1, !task.transcoded_filename_.isEmpty());

    // Here we can split the transcoded file if we need to, because we want
    // for songs that are part of a cue only the segment that we need.
    QString segment_file;
    qLog(Debug) << "File must be segmented?" << song.cue_path();
    bool needs_segment=false;
    if (song.has_cue()) {
        qLog(Debug) << "File Needs Segmenting " << song.url();
        needs_segment=true;
        Segmenter *S=new Segmenter(job);
        if (S->CanSegment()) {
            if (S->Create()) {
                segment_file=S->CreatedFileName();
                job.source_=segment_file;
            } else {
                qLog(Error) << "Segment could not be created";
            }
        } else {
            qLog(Info) << "Cannot create segment of " << job.source_;
        }
        delete S;
    }

    // if we're part of a cue we want the segment,
    // and if we can't get it, it's no use writing the
    // whole MP3 file for each song again.
    if (needs_segment && segment_file.isEmpty()) {
        files_with_errors_ << song_or_file.DisplayName();
    } else {
        if (!destination_->CopyToStorage(job)) {
          files_with_errors_ << song_or_file.DisplayName();
        }
    }

    // Clean up the temporary transcoded file
    if (!task.transcoded_filename_.isEmpty())
      QFile::remove(task.transcoded_filename_);

    // Clean up the segmented file if it is there
    if (!segment_file.isEmpty())
        QFile::remove(segment_file);

    tasks_complete_++;
  }
  SetSongProgress(0);

  QTimer::singleShot(0, this, SLOT(ProcessSomeFiles()));
}

Song::FileType Organise::CheckTranscode(Song::FileType original_type) const {
  if (original_type == Song::Type_Stream)
    return Song::Type_Unknown;

  const MusicStorage::TranscodeMode mode = destination_->GetTranscodeMode();
  const Song::FileType format = destination_->GetTranscodeFormat();

  switch (mode) {
    case MusicStorage::Transcode_Never:
      return Song::Type_Unknown;

    case MusicStorage::Transcode_Always:
      if (original_type == format)
        return Song::Type_Unknown;
      return format;

    case MusicStorage::Transcode_Unsupported:
      if (supported_filetypes_.isEmpty() || supported_filetypes_.contains(original_type))
        return Song::Type_Unknown;

      if (format != Song::Type_Unknown)
        return format;

      // The user hasn't visited the device properties page yet to set a
      // preferred format for the device, so we have to pick the best
      // available one.
      return Transcoder::PickBestFormat(supported_filetypes_);
  }
  return Song::Type_Unknown;
}

void Organise::SetSongProgress(float progress, bool transcoded) {
  const int max = transcoded ? 50 : 100;
  current_copy_progress_ = (transcoded ? 50 : 0) +
                           qBound(0, int(progress * max), max-1);
  UpdateProgress();
}

void Organise::UpdateProgress() {
  const int total = task_count_ * 100;

  // Update transcoding progress
  QMap<QString, float> transcode_progress = transcoder_->GetProgress();
  foreach (const QString& filename, transcode_progress.keys()) {
    if (!tasks_transcoding_.contains(filename))
      continue;
    tasks_transcoding_[filename].transcode_progress_ = transcode_progress[filename];
  }

  // Count the progress of all tasks that are in the queue.  Files that need
  // transcoding total 50 for the transcode and 50 for the copy, files that
  // only need to be copied total 100.
  int progress = tasks_complete_ * 100;

  foreach (const Task& task, tasks_pending_) {
    progress += qBound(0, int(task.transcode_progress_ * 50), 50);
  }
  foreach (const Task& task, tasks_transcoding_.values()) {
    progress += qBound(0, int(task.transcode_progress_ * 50), 50);
  }

  // Add the progress of the track that's currently copying
  progress += current_copy_progress_;

   task_manager_->SetTaskProgress(task_id_, progress, total);
}

void Organise::FileTranscoded(const QString& filename, bool success) {
  qLog(Info) << "File finished" << filename << success;
  transcode_progress_timer_.stop();

  Task task = tasks_transcoding_.take(filename);
  if (!success) {
    files_with_errors_ << filename;
  } else {
    tasks_pending_ << task;
  }
  QTimer::singleShot(0, this, SLOT(ProcessSomeFiles()));
}

QString Organise::FiddleFileExtension(const QString& filename, const QString& new_extension) {
  if (filename.section('/', -1, -1).contains('.'))
    return filename.section('.', 0, -2) + "." + new_extension;
  return filename + "." + new_extension;
}

void Organise::timerEvent(QTimerEvent* e) {
  QObject::timerEvent(e);

  if (e->timerId() == transcode_progress_timer_.timerId()) {
    UpdateProgress();
  }
}

//////////////////////////////////////////////////////////////////////////
// Implementation of SongOrFilePair
//////////////////////////////////////////////////////////////////////////

SongOrFilePair::SongOrFilePair(const Song & s) {
    song_=s;type_=1;
}

SongOrFilePair::SongOrFilePair(const QString & f) {
    file_=f;type_=2;
}

SongOrFilePair::SongOrFilePair() {
    type_=0;
}

Song & SongOrFilePair::song() {
    return song_;
}

QString & SongOrFilePair::file() {
    return file_;
}

bool SongOrFilePair::IsFile() {
    return type_==2;
}

bool SongOrFilePair::IsSong() {
    return type_==1;
}

QString SongOrFilePair::DisplayName() {
    if (this->IsSong()) {
        return song_.artist() + ", " + song_.title();
    } else {
        return file_;
    }
}

QString SongOrFilePair::GetFile() {
    if (this->IsFile()) {
        return file_;
    } else {
        return song_.url().toLocalFile();
    }
}
