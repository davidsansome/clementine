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
#ifndef SEGMENTER_H
#define SEGMENTER_H

#include "musicstorage.h"

class Segmenter {
private:
    enum Type {
        UNSUPPORTED, MP3, OGG
    };

private:
    MusicStorage::CopyJob job_;
    QString               segment_file_;
public:
    Segmenter(MusicStorage::CopyJob & job);


    Segmenter::Type CheckSource();
    bool            CanSegment();
    bool            Create();
    QString         CreatedFileName();
};

#endif // SEGMENTER_H
