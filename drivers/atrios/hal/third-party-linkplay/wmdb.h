#ifndef LINKPLAY_A98_WMDB_H
#define LINKPLAY_A98_WMDB_H
/*
    Header file for Linkplay A98 libwmdb.so propietary library.
    To link libwmdb.so in your application, use "-lnvram -lwmdb -ljson-c"
*/

extern "C" int wm_db_get(char *param, char *value);

#endif // LINKPLAY_A98_WMDB_H
