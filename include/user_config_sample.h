#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#define HKNFCRW_USE_LASTERR

#define WIFI_SSID       "xxx"
#define WIFI_PASSWD     "xxxx"

#define FIREBASE_AUTH   "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define FIREBASE_PRJ    "xxxxxxxxxxxxx"
#define FIREBASE_SAVE   "rcs620s"
#define FIREBASE_URL    FIREBASE_PRJ ".firebaseio.com"
#define FIREBASE_POST_PARAM     "/" FIREBASE_SAVE ".json?auth=" FIREBASE_AUTH

#endif

