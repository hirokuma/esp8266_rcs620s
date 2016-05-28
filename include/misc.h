#ifndef MISC_H__
#define MISC_H__

#include "esp8266_headers.h"

#define TASK_PRIOR_MAIN         (0)

#define REQ_WIFI_CONN_START     (1)
#define REQ_GET_HOST_BY_NAME    (2)
#define REQ_TCP_CONN_START      (3)
#define REQ_MAIN_PROC           (4)
#define REQ_MAIN_LOOP           (5)
#define REQ_REBOOT              (6)


#define DBG_PRINTF(...)     os_printf(__VA_ARGS__)
#define DBG_FUNCNAME()      do {\
    DBG_PRINTF("[[ %s() ]]\n", __func__);       \
} while (0)

#endif /* MISC_H__ */
