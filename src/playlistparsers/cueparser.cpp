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

   ********************************************************************

   Rewrite 2013, Hans Oesterholt <debian@oesterholt.net>

*/

#include "cueparser.h"
#include "core/logging.h"
#include "core/timeconstants.h"
#include "version.h"

#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QStringBuilder>
#include <QRegExp>
#include <QTextCodec>
#include <QTextStream>
#include <QtDebug>
#include <QUrl>
#include <cstdio>
#include <iostream>
#include <fstream>

#include <string.h>
#include <malloc.h>

/*
  The point of this cuesheet parser is to be as tolerant as possible for all kinds of
  formats of cuesheets and just extract as many information that we can get to get
  a decent songlist. We follow the spec at http://wiki.hydrogenaudio.org/index.php?title=Cue_sheet
 */

/******************************************************************************************
 * Declare some internal supporting functions for parsing cuesheets
 ******************************************************************************************/

/*
  Convert indexes to nanoseconds.

  Indexes are in mm:ss:ff (minutes, seconds, frames/second);
  They need to be to nanoseconds. The frames are in 75 parts
  per second. We calculate these to the second fraction and
  next calculate the index in nanoseconds.
*/
static qint64 IndexToNano(const QString _index);

/*
  Convert nanoseconds back to index format (mm:ss:ff)
*/
static QString NanoToIndex(const qint64 time_in_nano);


/******************************************************************************************
 *  CueSheetEntry class implementation.
 *
 *  This is a straightforward implementation of a very simple entry class.
 ******************************************************************************************/

CueSheetEntry::CueSheetEntry()
{
  Clear(-1, "");
}

CueSheetEntry::CueSheetEntry(int tracknr, QString mediafile)
{
  Clear(tracknr, mediafile);
}

CueSheetEntry::CueSheetEntry(const CueSheetEntry & e)
{
  operator =(e);
}

CueSheetEntry::CueSheetEntry(const Song & s)
{
  operator =(s);
}

// Let's keep this getter code compact
QString CueSheetEntry::media_file() { return media_file_; }

QString CueSheetEntry::album_title() {  return album_title_; }
QString CueSheetEntry::album_performer() { return album_performer_; }
QString CueSheetEntry::album_composer() { return album_composer_; }
QString CueSheetEntry::album_image() { return album_image_; }
QString CueSheetEntry::album_genre() { return album_genre_; }

int CueSheetEntry::track() { return track_; }
QString CueSheetEntry::title() { return title_; }
QString CueSheetEntry::performer() { return performer_; }
QString CueSheetEntry::composer() { return composer_; }
QString CueSheetEntry::piece() { return piece_; }
int CueSheetEntry::year() { return year_; }

qint64 CueSheetEntry::begin_offset() { return begin_offset_; }
qint64 CueSheetEntry::end_offset() { return end_offset_; }

// Let's keep this setter code compact
void CueSheetEntry::set_media_file(const QString abs_filename) { media_file_ = abs_filename; }
void CueSheetEntry::set_album_title(const QString title) { album_title_ = title; }
void CueSheetEntry::set_album_performer(const QString performer) { album_performer_ = performer; }
void CueSheetEntry::set_album_composer(const QString composer) { album_composer_ = composer; }
void CueSheetEntry::set_album_image(const QString image_file) { album_image_ = image_file; }
void CueSheetEntry::set_album_genre(const QString genre) { album_genre_ = genre; }

void CueSheetEntry::set_track(int t) { track_ = t; }
void CueSheetEntry::set_year(int y) { year_ = y; }
void CueSheetEntry::set_title(const QString title) { title_ = title; }
void CueSheetEntry::set_performer(const QString performer) { performer_ = performer; }
void CueSheetEntry::set_composer(const QString composer) { composer_ = composer; }
void CueSheetEntry::set_piece(const QString piece) { piece_ = piece; }

void CueSheetEntry::set_begin_offset(qint64 offset) { begin_offset_ = offset; }
void CueSheetEntry::set_end_offset(qint64 offset) { end_offset_ = offset; }

void CueSheetEntry::Clear(int tracknr, QString mediafile)
{
  track_ = tracknr;
  media_file_ = mediafile;
  album_title_ = "";
  album_performer_ = "";
  album_composer_ = "";
  album_image_ = "";
  album_genre_ = "";
  title_ = "";
  piece_ = "";
  composer_ = "";
  performer_ = "";
  begin_offset_ = -1;
  end_offset_ = -1;
  year_ = -1;
}

void CueSheetEntry::operator =(const CueSheetEntry & e)
{
  media_file_ = e.media_file_;

  album_title_ = e.album_title_;
  album_performer_ = e.album_performer_;
  album_composer_ = e.album_composer_;
  album_image_ = e.album_image_;
  album_genre_ = e.album_genre_;

  track_ = e.track_;
  title_ = e.title_;
  piece_ = e.piece_;
  composer_ = e.composer_;
  performer_ = e.performer_;
  year_ = e.year_;

  begin_offset_ = e.begin_offset_;
  end_offset_ = e.end_offset_;
}

void CueSheetEntry::operator =(const Song & s)
{
  media_file_ = s.url().toLocalFile();

  album_title_ = s.album();
  album_performer_ = s.albumartist();
  album_composer_ = s.composer();
  album_image_ = s.art_manual();
  album_genre_ = s.genre();

  track_ = s.track();
  title_ = s.title();
  QString piece = s.comment();
  piece_ = piece.replace("\n", " ");
  composer_ = s.composer();
  performer_ = s.artist();
  year_ = s.year();

  begin_offset_ = s.beginning_nanosec();
  end_offset_ = s.end_nanosec();
}

/******************************************************************************************
 * Implementation of the CueParser class
 ******************************************************************************************/

CueParser::CueParser(LibraryBackendInterface * library, QObject * parent)
 : ParserBase(library, parent)
{
  Init();
}

CueParser::CueParser()
  : ParserBase(NULL, NULL)
{
  Init();
}

CueParser::CueParser(const QString & cue_file)
  : ParserBase(NULL, NULL),
    cue_file_(cue_file)
{
  Init();
  Parse();
}

CueParser::CueParser(QIODevice * device, const QString & cue_file)
  : ParserBase(NULL, NULL),
    cue_file_(cue_file)
{
  Init();
  Parse(device);
}

void CueParser::Init()
{
  album_keywords << Keyword("performer")
                 << Keyword("songwriter")
                 << Keyword("title")
                 << Keyword("composer", true)
                 << Keyword("genre", true)
                 << Keyword("date", true)
                 << Keyword("image", true)
                 << Keyword("file", false, true)
                 << Keyword("track");

  track_keywords << Keyword("performer")
                 << Keyword("songwriter")
                 << Keyword("composer", true)
                 << Keyword("piece", true)
                 << Keyword("date", true)
                 << Keyword("title")
                 << Keyword("file", true)
                 << Keyword("index")
                 << Keyword("track");

  QFileInfo info(cue_file_);
  cue_dir_ = QDir(info.dir());
}

void CueParser::Parse()
{
  if (cue_file_.open(QIODevice::ReadOnly)) {
    Parse(&cue_file_);
    cue_file_.close();
  }
}

void CueParser::Parse(QIODevice * device)
{

  QTextStream text_stream(device);
  text_stream.setCodec(QTextCodec::codecForUtfText(device->peek(1024), QTextCodec::codecForName("UTF-8")));

  songs_.clear();

  QFile media_file;                 // FILE "<name>" <type>
  media_file_ = "";                 // cuesheet associated primary media file
  QString album_title;              // title of this album
  QString album_performer;          // performer of this album
  QString album_composer;           // composer of this album
  int year = -1;                    // year of publishing
  QFile album_image;                // cover art
  QString album_genre = "unknown";  // genre of this music

  QString line;
  bool in_tracks = false;

  CueSheetEntry song;
  int songnr = 0;

  // Parse the cuesheet
  while (!(line = text_stream.readLine()).isNull()) {
    if (!in_tracks) {
      QString keyword, result;
      if (ProcessLine(line, album_keywords, keyword, result)) {
        if (keyword == "performer") {
          album_performer = result;
        } else if (keyword == "title") {
          album_title = result;
        } else if (keyword == "composer" || keyword == "songwriter") {
          album_composer = result;
        } else if (keyword == "image") {
          album_image.setFileName(
                             QDir::isAbsolutePath(result) ? result
                                      : cue_dir_.absoluteFilePath(result));
        } else if (keyword == "file") {
          media_file.setFileName(QDir::isAbsolutePath(result) ? result
                     : cue_dir_.absoluteFilePath(result));
          media_file_ = media_file.fileName();
        } else if (keyword == "genre") {
          // Make sure genre is always Capital followed by lowers.
          album_genre = result.left(1).toUpper() + result.mid(1).toLower();
        } else if (keyword == "date") {
          year = result.toInt();
          if (year == 0) {
            year = -1;
          }
        } else if (keyword == "track") {
          in_tracks = true;
        } else {
          // We don't know this keyword, just skip over this line
        }
      }
    }

    if (in_tracks) {
      QString keyword, result;
      if (ProcessLine(line, track_keywords, keyword, result)) {
        if (keyword == "track") {
          if (songnr > 0) {
            songs_ << song;
          }
          songnr += 1;
          song.Clear(songnr, media_file.fileName());

          song.set_album_title(album_title);
          song.set_album_genre(album_genre);
          song.set_album_composer(album_composer);
          song.set_performer(album_performer);
          song.set_composer(album_composer);
          song.set_album_performer(album_performer);
          song.set_album_image(album_image.fileName());
          song.set_year(year);

        } else if (keyword == "title") {
          song.set_title(result);
        } else if (keyword == "performer") {
          song.set_performer(result);
        } else if (keyword == "composer" || keyword == "songwriter") {
          song.set_composer(result);
        } else if (keyword == "date") {
          year = result.toInt();
          song.set_year(year);
        } else if (keyword == "piece") {
          song.set_piece(result);
        } else if (keyword == "index") {
          qint64 offset_in_nanosec = IndexToNano(result);
          song.set_begin_offset(offset_in_nanosec);
        } else if (keyword == "file") {
          // If we find the 'file' keyword, we apply it to the next track.
          media_file.setFileName(QDir::isAbsolutePath(result) ? result
                     : cue_dir_.absoluteFilePath(result));
        } else {
          //  We don't know this keyword, just skip over this line
        }
      }
    }
  }

  // if we read any songs, we have one left to add now
  if (songnr > 0) {
    songs_ << song;
  }

  // Now update the end offset for all songs (except for the last one).
  {
    int i, N;
    for (i = 0, N = songs_.length() - 1 ; i < N ; ++i) {
      CueSheetEntry & s1 = songs_[i];
      CueSheetEntry & s2 = songs_[i+1];
      if (s1.media_file() == s2.media_file()) {
        s1.set_end_offset(s2.begin_offset());
      } else {
        // Do nothing
      }
    }
  }
  // We're done

}

CueSheetEntry & CueParser::operator[](int i) {
  return songs_[i];
}

CueSheetEntry & CueParser::Entry(int i)
{
  return songs_[i];
}

int CueParser::Count()
{
  return songs_.length();
}

/*
  This method will convert a CueSheetEntry to a Clementine Song
*/
Song CueParser::GetSong(int i)
{
  Song song;
  CueSheetEntry e = Entry(i);

  QFileInfo media_info(e.media_file());
  song = LoadSong(media_info.absoluteFilePath(), 0, media_info.dir());

  QFileInfo cue_info(cue_file_);
  song.set_cue_path(cue_info.absoluteFilePath());

  song.set_track(e.track());
  song.set_album(e.album_title());
  song.set_albumartist(e.album_performer());
  song.set_composer(e.album_composer());
  song.set_art_manual(e.album_image());
  song.set_art_automatic(e.album_image());
  song.set_genre(e.album_genre());
  if (e.year() > 0) { song.set_year(e.year()); }
  song.set_title(e.title());
  song.set_artist(e.performer());
  song.set_composer(e.composer());
  // No metadata for that, so put it in the comment
  song.set_comment(e.piece());

  song.set_beginning_nanosec(e.begin_offset());
  if (e.end_offset() >= 0) {
    song.set_end_nanosec(e.end_offset());
  }

  return song;
}

/*
  This method will accept a clementine song and convert it to a CueSheetEntry
*/
void CueParser::SetSong(int i, const Song & s)
{
  songs_[i]=s;
}

/*
  This method converts all CueSheetEntries to Clementine Songs
*/
SongList CueParser::ToSongs()
{
  SongList songs;
  int i, N;
  for (i = 0, N = this->Count(); i < N; ++i) {
    songs << GetSong(i);
  }
  return songs;
}

/*
  Return the cuesheet filename
*/
QString CueParser::CueFile()
{
  return cue_file_.fileName();
}

/*
  Return the main mediafile of the cuesheet
*/
QString CueParser::MediaFile()
{
  return media_file_;
}

/*
  Search a Clementine song in this cuesheet
*/
int CueParser::indexOf(const Song & s)
{
  if (s.cue_path() != CueFile()) {
    // If the cuesheets aren't the same there's no much use in searching
    return -1;
  } else {
    int i, N, index = -1;
    bool not_found;
    for (i = 0, N = Count(), not_found = true; i < N && not_found; ++i) {
      CueSheetEntry e = this->Entry(i);
      if (e.track() == s.track()) {
        not_found = false;
        index = i;
      }
    }
    return index;
  }
}


/*
  Write the cuesheet back to the cuesheet
*/
bool CueParser::Write()
{
  // create a backup of the cuesheet file
  QFileInfo cue_info(cue_file_);
  QFileInfo nfile_info(cue_info.absoluteDir(),cue_info.baseName()+".bcue");
  QFile nfile(nfile_info.absoluteFilePath());
  cue_file_.copy(nfile.fileName());

  // Write out the cuesheet file (straightforward)
  QString lastfile = this->MediaFile();

  // Only write out the cuesheet if the number of entries > 0
  if (Count() > 0) {
    if (!cue_file_.open(QIODevice::WriteOnly)) {
      // If the cue_file is not writable, don't write it and return failure
      return false;
    }
    else {
      CueSheetEntry s = this->Entry(0);
      QTextStream fh(&cue_file_);

      // Write out the header
      fh << "REM Clementine CueSheet Writer, version " << CLEMENTINE_VERSION_DISPLAY << "\n";
      fh << "REM DATE \"" << s.year() << "\"\n";
      if (s.album_image() != "") {
        QFileInfo img_info(s.album_image());
        fh << "REM IMAGE \"" << img_info.fileName() << "\"\n";
      }
      fh << "REM GENRE \"" << s.album_genre() << "\"\n";
      fh << "REM COMPOSER \"" << s.album_composer() << "\"\n";
      fh << "TITLE \"" << s.album_title() << "\"\n";
      fh << "PERFORMER \"" << s.album_performer() << "\"\n";
      {
        QFileInfo mf(lastfile);
        fh << "FILE \"" << mf.fileName() << "\" WAVE\n";
      }

      // Write out the entries
      int i, N;
      for (i = 0, N = this->Count(); i < N; i++) {
        CueSheetEntry s = this->Entry(i);
        if (s.media_file() != this->MediaFile()) {
          lastfile = s.media_file();
          QFileInfo mf(lastfile);
          fh << "FILE \"" << mf.fileName() << "\" WAVE\n";
        }
        fh << "  TRACK " << QString().sprintf("%02d", i + 1) << " AUDIO\n";
        fh << "    TITLE \"" << s.title() << "\"\n";
        fh << "    PERFORMER \"" << s.performer() << "\"\n";
        fh << "    REM PIECE \"" << s.piece() << "\"\n";
        fh << "    REM COMPOSER \"" << s.composer() << "\"\n";
        fh << "    REM DATE \"" << s.year() << "\"\n";
        fh << "    REM END_OFFSET " << NanoToIndex(s.end_offset()) << "\n";
        if (s.begin_offset() >= 0) {
          fh << "    INDEX 01 " << NanoToIndex(s.begin_offset()) << "\n";
        }
      }
      cue_file_.close();
      return true;
    }
  }
  return false;
}


/*
  Now that we've got the basis for cuesheet parsing, the inherited parser methods are simple.
*/

SongList CueParser::Load(QIODevice * device, const QString & cue_file, const QDir & dir) const
{
  CueParser cp(device, cue_file);
  cp.Parse();
  return cp.ToSongs();
}

// Save is not implemented.
void CueParser::Save(const SongList & songs, QIODevice * device, const QDir & dir) const
{
  qLog(Debug) << "Save called, not implemented";
}

/*
  This static member function loads a cuesheet file and rewrites it with
  the given song metadata
*/
void CueParser::SaveSong(const Song & song)
{
  CueParser cp(song.cue_path());
  cp.Parse();
  int index = cp.indexOf(song);
  if (index >= 0) {
    cp.SetSong(index, song);
    cp.Write();
  }
}

// Looks for a track starting with one of the .cue's keywords.
bool CueParser::TryMagic(const QByteArray & data) const
{
  QStringList splitted = QString::fromUtf8(data.constData()).split('\n');
  QString keyword, result;

  CueParser cp;
  for (int i = 0; i < splitted.length(); i++) {
    if (cp.ProcessLine(splitted[i], cp.album_keywords, keyword, result)) {
      return true;
    }
  }

  return false;
}

// process a line. If it matches a keyword, return the keyword and
// the unquoted contents.
bool CueParser::ProcessLine(QString & line, QList<Keyword> & keys,
                              QString & keyword, QString & result)
{
  int i,N;
  for (i = 0, N = keys.size(); i < N ; ++i) {
    if (keys[i].eq(line)) {
      keyword = keys[i].keyword();
      result = keys[i].unquote(line);
      return true;
    }
  }
  return false;
}


/******************************************************************************************
 *  Implementation of the supporting classes and functions declared above.
 ******************************************************************************************/

CueParser::Keyword::Keyword(const char *keyword, bool is_rem, bool has_end_keyword)
  : keyword_(keyword),
    has_end_keyword_(has_end_keyword),
    rem_(is_rem)
{
  endkw_ = QRegExp("[\"][^\"]*$");
  QString re = QString("^")
               + QString( (rem_) ? "\\s*rem\\s*" : "\\s*" )
               + keyword_
               + QString( "\\s+" );
  kw_ = QRegExp(re, Qt::CaseInsensitive);
}

bool CueParser::Keyword::eq(const QString line)
{
  // keyword regexp: returns -1 (no match) or 0 (match)
  // because keywords are matched from the beginning of
  // the line
  return kw_.indexIn(line)!=-1;
}

const QString CueParser::Keyword::keyword() {
  return keyword_;
}


QString CueParser::Keyword::unquote(const QString line) {
  QString ln=line;
  ln=ln.replace(kw_,"").trimmed();
  if (ln.length()>0) {
    if (ln[0]=='"') {
      ln=ln.mid(1);
    }
    ln=ln.replace(endkw_,"");
  }
  return ln;
}



static qint64 IndexToNano(const QString _index)
{
  static QRegExp skip("^\\s*[0-9]+\\s+");
  QString index(_index);
  index.replace(skip, "");
  // Skip the 00 / 01 part
  QStringList parts = index.split(":");
  int min = 0, sec = 0, frames = 0;
  if (parts.length() > 0) {
    min = parts[0].toInt();
  }
  if (parts.length() > 1) {
    sec = parts[1].toInt();
  }
  if (parts.length() > 2) {
    frames = parts[2].toInt();
  }

  qint64 totaltime_in_hs = min * 60 * 100 + sec * 100 +
                    ((int)((100.0 * ((double)frames)) / 75.0));
  qint64 total_time_in_ns = totaltime_in_hs * kNsecPerHsec;

  return total_time_in_ns;
}



static QString NanoToIndex(const qint64 time_in_nano)
{
  qint64 t = time_in_nano / kNsecPerHsec;
  double f = (double)(t % 100);
  f *= 75.0;
  f /= 100.0;
  int frames = (int)f;
  int sec = (t / 100) % 60;
  int min = (t / 100 / 60);
  return QString().sprintf("%02d:%02d:%02d", min, sec, frames);
}



