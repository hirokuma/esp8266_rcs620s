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

    struct espconn *pConn = (struct espconn *)pArg;
    espconn_disconnect(pConn);
}


void ICACHE_FLASH_ATTR data_receivedcb(void *pArg, char *pData, unsigned short Len)
{
    //struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    DBG_PRINTF("\n----------\n");
    DBG_PRINTF(pData);
    DBG_PRINTF("\n----------\n");
}


void ICACHE_FLASH_ATTR tcp_disconnectedcb(void *pArg)
{
    DBG_FUNCNAME();

    wifi_station_disconnect();
}


void ICACHE_FLASH_ATTR main_proc(struct espconn *pConn)
{
    //ゴミが送られているので、とりあえず空データを送信しておく
    os_memset(mBuffer, 0x00, 10);
    uart0_tx_buffer(mBuffer, 10);

    bool b = HkNfcRw_Open();
    if (!b) {
        DBG_PRINTF("Open fail\n");
        return;
    }
    HkNfcType type = HKNFCTYPE_NONE;
    int timeout = 100;
    while (timeout--) {
        system_soft_wdt_feed();
        type = HkNfcRw_Detect(true, true, true);
        if (type != HKNFCTYPE_NONE) {
            break;
        }
        os_delay_us(50000);     //50msec
    }
    DBG_PRINTF("type=%d\n", type);
    uint8_t len = 5;

    if (type != HKNFCTYPE_NONE) {
        mBuffer[0] = (uint8_t)type;
        len = HkNfcRw_GetNfcId(mBuffer + 1);
        len++;      //先頭にtypeも付ける
        uint8_t i;
        for (i = 0; i < len; i++) {
            DBG_PRINTF("%02X", mBuffer[i]);
        }
        DBG_PRINTF("\n");
    }
    HkNfcRw_Close();


//    char *p = (char *)mBuffer;
//    os_sprintf(p,
//            "GET " MY_GET " HTTP/1.1\r\n"
//            "Host: " MY_HOST "\r\n"
//            "\r\n");
//    DBG_PRINTF("[%s]\n", p);
    err_t err = espconn_send(pConn, mBuffer, len);
    if (err != 0) {
        DBG_PRINTF("espconn_send fail: %d\n", err);
    }

    //メインループに移行
    system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
}


void ICACHE_FLASH_ATTR main_loop(struct espconn *pConn)
{
    system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
}
