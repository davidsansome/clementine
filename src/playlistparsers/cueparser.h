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

   Rewrite 2013, Hans Oesterholt <debian@oesterholt.net>
*/

#ifndef CUEPARSER_H
#define CUEPARSER_H

#include "parserbase.h"
#include "core/song.h"

#include <QRegExp>

// This parser will try to detect the real encoding of a .cue file but there's
// a great chance it will fail so it's probably best to assume that the parser
// is UTF compatible only.

#define CUEPARSER_VERSION "0.0.1"

class CueSheetEntry {
 private:
  QString media_file_;

  QString album_title_;
  QString album_performer_;
  QString album_composer_;
  QString album_image_;
  QString album_genre_;

  int track_;
  QString title_;
  QString piece_;
  QString composer_;
  QString performer_;
  int year_;

  qint64 begin_offset_;
  qint64 end_offset_;

 public:
  QString media_file();

  QString album_title();
  QString album_performer();
  QString album_composer();
  QString album_image();
  QString album_genre();

  int track();
  QString title();
  QString performer();
  QString composer();
  QString piece();
  int year();

  qint64 begin_offset();
  qint64 end_offset();

 public:
  void set_media_file(const QString abs_filename);
  void set_album_title(const QString title);
  void set_album_performer(const QString performer);
  void set_album_composer(const QString composer);
  void set_album_image(const QString image_file);
  void set_album_genre(const QString genre);

  void set_track(int t);
  void set_title(const QString title);
  void set_performer(const QString performer);
  void set_composer(const QString composer);
  void set_piece(const QString piece);
  void set_year(int y);

  void set_begin_offset(qint64 offset);
  void set_end_offset(qint64 offset);

 public:
  void operator =(const Song &);
  void operator =(const CueSheetEntry &);
  void Clear(int tracknr, QString mediafile);

 public:
   CueSheetEntry();
   CueSheetEntry(int tracknr, QString mediafile);
   CueSheetEntry(const CueSheetEntry & e);
   CueSheetEntry(const Song & e);
};

class CueParser:public ParserBase {
 Q_OBJECT

private:
  QList < CueSheetEntry > songs_;
  QFile cue_file_;
  QDir cue_dir_;
  QString media_file_;

  /*
    This private class implements functionality to check whether a line equals a keyword and,
    if the keyword has been found to extract the data from it.
  */

  class Keyword {
   private:
    QRegExp endkw_;           // strips the part after a quoted string
    QRegExp kw_;              // matches a keyword or rem keyword
    QString keyword_;         // holds the keyword to be matched
    bool has_end_keyword_;    // tells the matcher if it should take in account something that follows the content
    bool rem_;                // tells the matcher that this keyword is prepended by REM
   public:
    Keyword(const char *keyword, bool is_rem = false, bool has_end_keyword = false);
    bool eq(const QString line);            // is this line a keyword_ line?
    const QString keyword();                // give me the keyword in this keyword matcher object
    QString unquote(const QString line);    // unquote the contents of a matched string. Precodition: eq(line).
  };

  QList<Keyword> album_keywords;
  QList<Keyword> track_keywords;

  void Parse();
  void Parse(QIODevice * device);

  /*
    processes a line for given keywords.
    if one of the keywords match, returns the unquoted result and the concerning keyword

    = false, no matching keyword found
    = true, found a matching keyword
  */
  bool ProcessLine(QString & line, QList<Keyword> & keywords,
                          QString & keyword, QString & result);

  // Helper for constructors
  void Init();

 private:
   // this one is only for keywords and line processing
   CueParser();

 public:
  // Constructor that loads a cuesheet directly, which makes it possible to
  // manipulate the cuesheet.
   CueParser(const QString & cue_file);
   CueParser(QIODevice * device, const QString & cue_file);

  // Constructor for the parser framework
   CueParser(LibraryBackendInterface * library, QObject * parent = NULL);

  // Default methods for the parser framework
  QString name() const { return "CUE"; }
  QStringList file_extensions() const { return QStringList() << "cue"; }
  QString mime_type() const { return "application/x-cue"; }
  bool TryMagic(const QByteArray & data)const;

  // Inherited methods
  SongList Load(QIODevice * device, const QString & cue_file = "", const QDir & dir = QDir())const;
  void Save(const SongList & songs, QIODevice * device, const QDir & dir = QDir())const;

  // Static public methods

  // Saves a song. Loads the cuesheet associated with it and writes it back
  // with the given song metadata.
  static void SaveSong(const Song & s);

  // Public methods

  // Returns the number of tracks in the cuesheet
  int Count();

  // Returns the ith cuesheet entry
  CueSheetEntry & operator[] (int i);
  CueSheetEntry & Entry(int i);

  // Searches a cuesheet entry with Song or CueSheetEntry metadata
  int indexOf(const Song & s);
  int indexOf(const CueSheetEntry & s);

  // Writes the cuesheet data back to the cuesheet file.
  bool Write();

  // Song interaction. Returns all cuesheet entries as a SongList
  SongList ToSongs();

  // Returns the ith cuesheet entry as a Song
  Song GetSong(int i);

  // Sets the ith cuesheet entry from a Song
  void SetSong(int i, const Song & s);

  // Returns the media filename associated with the cuesheet (absolute path)
  QString MediaFile();

  // Returns the cuesheet filename itself, absolute path
  QString CueFile();
};

#endif        // CUEPARSER_H
