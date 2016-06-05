#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"

#include "main_proc.h"
#include "misc.h"

#include "HkNfcRw.h"


static uint8_t mNfcId[HKNFCID_MAX];
static uint8_t mNfcIdLen;
static uint8_t mBuffer[1024];
static uint8_t mBody[256];
static bool    mSending = false;


void ICACHE_FLASH_ATTR data_sentcb(void *pArg)
{
    DBG_FUNCNAME();

//    struct espconn *pConn = (struct espconn *)pArg;
//    espconn_secure_disconnect(pConn);       //SSL
    mSending = false;
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


int ICACHE_FLASH_ATTR main_proc(struct espconn *pConn)
{
    bool b = HkNfcRw_Open();
    if (!b) {
        DBG_PRINTF("Open fail : %d\n", HkNfcRw_GetLastError());
        return ESPCONN_RTE;
    }

    return ESPCONN_OK;
}


void ICACHE_FLASH_ATTR main_loop(struct espconn *pConn)
{
    if (mSending) {
        system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
        return;
    }

    DBG_PRINTF("start Polling\n");

    HkNfcType type = HKNFCTYPE_NONE;
    system_soft_wdt_stop();
    type = HkNfcRw_Detect(true, true, true);
    system_soft_wdt_restart();
    HkNfcRw_Reset();
    if (type == HKNFCTYPE_NONE) {
        //未検出であればもう一度
        system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
        return;
    }

    DBG_PRINTF("type=%d\n", type);
    uint8_t len = 0;

    char nfcid[HKNFCID_MAX * 2 + 1];
    if (type != HKNFCTYPE_NONE) {
        len = HkNfcRw_GetNfcId(mBuffer);
        
        if ((len == mNfcIdLen) && (os_memcmp(mBuffer, mNfcId, mNfcIdLen) == 0)) {
            //前と同じであれば抜ける
            system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
            return;
        }
        mNfcIdLen = len;
        os_memcpy(mNfcId, mBuffer, mNfcIdLen);

        //文字列化
        uint8_t i;
        char *p = nfcid;
        for (i = 0; i < len; i++) {
            os_sprintf(p, "%02X", mNfcId[i]);
            p += 2;
        }
    }
    else {
        //リブート
        system_os_post(TASK_PRIOR_MAIN, REQ_REBOOT, 0);
        return;
    }

    DBG_PRINTF("send Firebase\n");

    os_sprintf((char *)mBody,
            "{\"post\":{"
                "\"now\":{"
                    "\".sv\":\"timestamp\"},"
                    "\"name\":\"hiro99ma\","
                    "\"type\":%d,"
                    "\"nfcid\":\"%s\""
                "}"
            "}",
        type, nfcid
    );

//    DBG_PRINTF("[\n%s\n]\n", (char *)mBody);

    os_sprintf((char *)mBuffer,
            "POST " FIREBASE_POST_PARAM " HTTP/1.1\r\n"
            "Host: " FIREBASE_URL "\r\n"
            "Accept: */*\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s", os_strlen((char *)mBody), (char *)mBody);

//    DBG_PRINTF("[\n%s\n]\n", (char *)mBuffer);

    err_t err = espconn_secure_send(pConn, mBuffer, os_strlen((char *)mBuffer));   //SSL
    if (err != 0) {
        DBG_PRINTF("espconn_secure_send fail: %d\n", err);
        system_os_post(TASK_PRIOR_MAIN, REQ_REBOOT, 0);
    }

    mSending = true;
    system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
}
