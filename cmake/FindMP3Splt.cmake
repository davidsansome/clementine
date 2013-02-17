# 
# Try to find libmp3splt  
# Once run this will define: 
# 
# MP3SPLT_FOUND
# MP3SPLT_INCLUDE_DIR 
# MP3SPLT_LIBRARIES
# MP3SPLT_LINK_DIRECTORIES
#
# Hans Oesterholt 2/2013
# --------------------------------

 FIND_PATH(MP3SPLT_INCLUDE_DIRS mp3splt.h
   ${MP3SPLT_DIR}/include
   $ENV{MP3SPLT_DIR}/include
   ${MP3SPLT_DIR}/include/libmp3splt
   $ENV{MP3SPLT_DIR}/include/libmp3splt
   $ENV{MP3SPLT_DIR}/include
   /usr/include
   /usr/local/include
   /usr/include/libmp3splt
   /usr/local/include/libmp3splt
   $ENV{SOURCE_DIR}/mp3splt
   $ENV{SOURCE_DIR}/mp3splt/include
   $ENV{SOURCE_DIR}/mp3splt/include/libmp3splt
 )

SET(MP3SPLT_POSSIBLE_LIBRARY_PATH
  ${MP3SPLT_DIR}/lib
  $ENV{MP3SPLT_DIR}/lib
  /usr/lib
  /usr/local/lib
  $ENV{SOURCE_DIR}/mp3splt
  $ENV{SOURCE_DIR}/mp3splt/lib
)

  
FIND_LIBRARY(MP3SPLT_LIBRARY
  NAMES libmp3splt.so
  PATHS 
  ${MP3SPLT_POSSIBLE_LIBRARY_PATH}
  )
#MESSAGE("DBG MP3SPLT_LIBRARY=${MP3SPLT_LIBRARY}")

# --------------------------------
# select one of the above
# default: 
IF (MP3SPLT_LIBRARY)
  SET(MP3SPLT_LIBRARIES ${MP3SPLT_LIBRARY})
ENDIF (MP3SPLT_LIBRARY)

# --------------------------------

IF(MP3SPLT_LIBRARIES)
  IF (MP3SPLT_INCLUDE_DIRS)

    # OK, found all we need
    SET(MP3SPLT_FOUND TRUE)
    GET_FILENAME_COMPONENT(MP3SPLT_LINK_DIRECTORIES ${MP3SPLT_LIBRARIES} PATH)
    MESSAGE("Found libmp3splt")
    
  ELSE (MP3SPLT_INCLUDE_DIRS)
    MESSAGE("MP3SPLT include dir not found. Set MP3SPLT_DIR to find it.")
  ENDIF(MP3SPLT_INCLUDE_DIRS)
ELSE(MP3SPLT_LIBRARIES)
  MESSAGE("MP3SPLT lib not found. Set MP3SPLT_DIR to find it.")
ENDIF(MP3SPLT_LIBRARIES)


MARK_AS_ADVANCED(
  MP3SPLT_INCLUDE_DIRS
  MP3SPLT_LIBRARIES
  MP3SPLT_LIBRARY
  MP3SPLT_LINK_DIRECTORIES
)


