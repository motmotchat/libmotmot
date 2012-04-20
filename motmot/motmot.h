/*
 * motmot.h
 * header files for motmot
 */

#define MOTMOT_WEBSITE "http://www.motmotchat.com"
#define DISPLAY_VERSION "0.1"

typedef struct _mm_conn{
    PurpleAccount *act;
    int write_fd;
} motmot_conn;
