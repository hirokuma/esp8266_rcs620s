#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"

#include "main_proc.h"
#include "misc.h"

#include "HkNfcRw.h"

#define MY_GET      "/~hiro99ma/index.html"

static uint8_t          mBuffer[2048];



void ICACHE_FLASH_ATTR data_sentcb(void *pArg)
{
    DBG_FUNCNAME();
}


void ICACHE_FLASH_ATTR data_receivedcd(void *pArg, char *pData, unsigned short Len)
{
    struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    uart0_sendStr("----------\n");
    uart0_sendStr(pData);
    uart0_sendStr("\n----------\n");

    espconn_disconnect(pConn);
}


void ICACHE_FLASH_ATTR tcp_disconnectedcb(void *pArg)
{
    DBG_FUNCNAME();

    wifi_station_disconnect();
}


void ICACHE_FLASH_ATTR main_proc(struct espconn *pConn)
{
//    char *p = (char *)mBuffer;
//    os_sprintf(p,
//            "GET " MY_GET " HTTP/1.1\r\n"
//            "Host: " MY_HOST "\r\n"
//            "\r\n");
//    DBG_PRINTF("[%s]\n", p);
//    err_t err = espconn_send(pConn, mBuffer, os_strlen(p));
//    if (err != 0) {
//        DBG_PRINTF("espconn_send fail: %d\n", err);
//    }

    //ゴミが送られているので、とりあえず空データを送信しておく
    os_memset(mBuffer, 0x00, 10);
    uart0_tx_buffer(mBuffer, 10);

    bool b = HkNfcRw_Open();
    if (!b) {
        DBG_PRINTF("Open fail\n");
        return;
    }
    HkNfcRw_Close();

    //メインループに移行
    system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
}


void ICACHE_FLASH_ATTR main_loop(struct espconn *pConn)
{
    system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
}
