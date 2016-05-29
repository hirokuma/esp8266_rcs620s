#ifndef MAIN_PROC_H__
#define MAIN_PROC_H__

#include "user_interface.h"
#include "user_config.h"
#include "espconn.h"

void ICACHE_FLASH_ATTR data_sentcb(void *pArg);
void ICACHE_FLASH_ATTR data_receivedcb(void *pArg, char *pData, unsigned short Len);
void ICACHE_FLASH_ATTR tcp_disconnectedcb(void *pArg);
void ICACHE_FLASH_ATTR main_proc(struct espconn *pConn);
void ICACHE_FLASH_ATTR main_loop(struct espconn *pConn);

#endif /* MAIN_PROC_H__ */
