#ifndef __VERSION_H__
#define __VERSION_H__

#define _YED_VERSION_MAJOR_PART  16
#define _YED_VERSION_MINOR_PART  00


#define _XYED_VERSION_CAT2(x, y) x##y
#define XYED_VERSION_CAT2(x, y)  _XYED_VERSION_CAT2(x, y)

#define _YED_MAJOR_VERSION       XYED_VERSION_CAT2(_YED_VERSION_MAJOR_PART, 00)
#define _YED_VERSION             XYED_VERSION_CAT2(_YED_VERSION_MAJOR_PART, _YED_VERSION_MINOR_PART)

#define _XYED_VERSION_STR(x)     #x
#define XYED_VERSION_STR(x)      _XYED_VERSION_STR(x)


#define YED_VERSION_STR          XYED_VERSION_STR(_YED_VERSION)
#define YED_MAJOR_VERSION_STR    XYED_VERSION_STR(_YED_MAJOR_VERSION)
#define YED_VERSION              (_YED_VERSION)
#define YED_MAJOR_VERSION        (_YED_MAJOR_VERSION)

extern int yed_version;

#endif
