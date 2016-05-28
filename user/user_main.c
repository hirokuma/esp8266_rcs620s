#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"

#include "main_proc.h"
#include "misc.h"

#define M_SZ_QUEUE      (5)


static const char M_SSID[] = MY_SSID;
static const char M_PASSWD[] = MY_PASSWD;

static os_event_t       mQueue[M_SZ_QUEUE];
static struct espconn   mConn;
static ip_addr_t        mHostIp;
static esp_tcp          mTcp;

static void ICACHE_FLASH_ATTR wifi_eventcb(System_Event_t *evt);
static void ICACHE_FLASH_ATTR dns_donecb(const char *pName, ip_addr_t *pIpAddr, void *pArg);
static void ICACHE_FLASH_ATTR tcp_connectedcb(void *pArg);
static void ICACHE_FLASH_ATTR event_handler(os_event_t *pEvent);


////////////////////////////////////////////////////////////////
// public
////////////////////////////////////////////////////////////////

/******************************************************************************
 * FunctionName : uart1_tx_one_char
 * Description  : Internal used function
 *                Use uart1 interface to transfer one char
 * Parameters   : uint8 TxChar - character to tx
 * Returns      : OK
*******************************************************************************/
static void ICACHE_FLASH_ATTR uart1_tx_one_char(uint8 TxChar)
{
    while (READ_PERI_REG(UART_STATUS(UART1)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S)) {
    }

    WRITE_PERI_REG(UART_FIFO(UART1) , TxChar);
}

/******************************************************************************
 * FunctionName : uart1_write_char
 * Description  : Internal used function
 *                Do some special deal while tx char is '\r' or '\n'
 * Parameters   : char c - character to tx
 * Returns      : NONE
*******************************************************************************/
static void ICACHE_FLASH_ATTR uart1_write_char(char c)
{
    if (c == '\n') {
        uart1_tx_one_char('\r');
        uart1_tx_one_char('\n');
    } else if (c == '\r') {
    } else {
        uart1_tx_one_char(c);
    }
}


void user_rf_pre_init(void)
{
}


void user_init(void)
{
    uart_init(BIT_RATE_115200, BIT_RATE_115200);

    //出力をUART1に変更
    os_install_putc1((void *)uart1_write_char);

    DBG_PRINTF("\nReady.\n");

    //リセット要因はuser_init()にならないと取得できない
    struct rst_info *pInfo = system_get_rst_info();
    switch (pInfo->reason) {
    case REASON_DEFAULT_RST:
        DBG_PRINTF("  REASON_DEFAULT_RST\n");
        break;
    case REASON_WDT_RST:
        DBG_PRINTF("  REASON_WDT_RST\n");
        break;
    case REASON_EXCEPTION_RST:
        DBG_PRINTF("  REASON_EXCEPTION_RST\n");
        break;
    case REASON_SOFT_WDT_RST:
        DBG_PRINTF("  REASON_SOFT_WDT_RST\n");
        break;
    case REASON_SOFT_RESTART:
        DBG_PRINTF("  REASON_SOFT_RESTART\n");
        break;
    case REASON_DEEP_SLEEP_AWAKE:
        DBG_PRINTF("  REASON_DEEP_SLEEP_AWAKE\n");
        break;
    case REASON_EXT_SYS_RST:
        DBG_PRINTF("  REASON_EXT_SYS_RST\n");
        break;
    default:
        DBG_PRINTF("  unknown reason : %x\n", pInfo->reason);
        break;
    }

    //タスク
    system_os_task(event_handler, TASK_PRIOR_MAIN, mQueue, M_SZ_QUEUE);
    system_os_post(TASK_PRIOR_MAIN, REQ_WIFI_CONN_START, 0);
}


////////////////////////////////////////////////////////////////
// private
////////////////////////////////////////////////////////////////

static void ICACHE_FLASH_ATTR wifi_eventcb(System_Event_t *evt)
{
    DBG_FUNCNAME();

    switch (evt->event) {
    case EVENT_STAMODE_CONNECTED:
        DBG_PRINTF("[CONN] SSID[%s] CH[%d]\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        DBG_PRINTF("[DISC] SSID[%s] REASON[%d]\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        DBG_PRINTF("[CHG_AUTH]\n");
        break;
    case EVENT_STAMODE_GOT_IP:
        DBG_PRINTF("[GOT_IP] IP[" IPSTR "] MASK[" IPSTR "] GW[" IPSTR "]\n",
                        IP2STR(&evt->event_info.got_ip.ip),
                        IP2STR(&evt->event_info.got_ip.mask),
                        IP2STR(&evt->event_info.got_ip.gw));
        system_os_post(TASK_PRIOR_MAIN, REQ_GET_HOST_BY_NAME, 0);
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        break;
    default:
        break;
    }
}


static void ICACHE_FLASH_ATTR dns_donecb(const char *pName, ip_addr_t *pIpAddr, void *pArg)
{
    struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    DBG_PRINTF("ip=%08x, IP=%08x\n", (int)pIpAddr, (int)&mHostIp);

    if ((pConn != NULL) && (pIpAddr != NULL)) {
        DBG_PRINTF("  result: " IPSTR "\n", IP2STR(&pIpAddr->addr));
        os_memcpy(&mHostIp, pIpAddr, sizeof(*pIpAddr));
        system_os_post(TASK_PRIOR_MAIN, REQ_TCP_CONN_START, 0);
    }
    else {
        DBG_PRINTF("DNS lookup fail.\n");
    }
}


static void ICACHE_FLASH_ATTR tcp_connectedcb(void *pArg)
{
    struct espconn *pConn = (struct espconn *)pArg;

    DBG_FUNCNAME();

    if (pConn != NULL) {
        system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_PROC, 0);
    }
    else {
        DBG_PRINTF("TCP connect fail.\n");
    }
}


static void ICACHE_FLASH_ATTR event_handler(os_event_t *pEvent)
{
    err_t err;

    switch (pEvent->sig) {
    case REQ_WIFI_CONN_START:
        {
            struct station_config stconf;

            os_memcpy(stconf.ssid, M_SSID, sizeof(M_SSID));
            os_memcpy(stconf.password, M_PASSWD, sizeof(M_PASSWD));
            stconf.bssid_set = 0;

            wifi_set_opmode(STATION_MODE);
            //wifi_set_opmode(STATIONAP_MODE);
            wifi_station_set_config(&stconf);

            wifi_set_event_handler_cb(wifi_eventcb);
        }
        //この後、IPアドレス取得により、wifi_eventcb()のEVENT_STAMODE_GOT_IPまで進む
        break;

    case REQ_GET_HOST_BY_NAME:
        DBG_PRINTF("REQ_GET_HOST_BY_NAME\n");
        err = espconn_gethostbyname(&mConn, MY_HOST, &mHostIp, dns_donecb);
        switch (err) {
        case ESPCONN_OK:
            //キャッシュで保持しているので、値がmHostIpに入る
            system_os_post(TASK_PRIOR_MAIN, REQ_TCP_CONN_START, 0);
            break;
        case ESPCONN_INPROGRESS:
            //キャッシュに保持していないので、lookupしにいく(mHostIpは使われない)
            DBG_PRINTF("espconn_gethostbyname : lookup...\n");
            break;
        default:
            DBG_PRINTF("espconn_gethostbyname fail : %d\n", err);
        }
        //この後、名前解決完了により、dns_donecb()が呼ばれる
        break;

    case REQ_TCP_CONN_START:
        DBG_PRINTF("REQ_TCP_CONN_START: " IPSTR "\n", IP2STR(&mHostIp.addr));
        mConn.type = ESPCONN_TCP;
        mConn.state = ESPCONN_NONE;
        mConn.proto.tcp = &mTcp;
        mConn.proto.tcp->local_port = espconn_port();
        mConn.proto.tcp->remote_port = MY_PORT;
        os_memcpy(mConn.proto.tcp->remote_ip, &mHostIp.addr, 4);

        espconn_regist_connectcb(&mConn, tcp_connectedcb);
        espconn_regist_disconcb(&mConn, tcp_disconnectedcb);
        espconn_regist_sentcb(&mConn, data_sentcb);
        espconn_regist_recvcb(&mConn, data_receivedcd);
        err = espconn_connect(&mConn);
        if (err != ESPCONN_OK) {
            DBG_PRINTF("espconn_connect fail : %d\n", err);
        }
        //この後、TCP接続完了により、tcp_connectedcb()が呼ばれる
        break;

    case REQ_MAIN_PROC:
        DBG_PRINTF("REQ_MAIN_PROC\n");
        main_proc(&mConn);
        system_os_post(TASK_PRIOR_MAIN, REQ_MAIN_LOOP, 0);
        break;

    case REQ_MAIN_LOOP:
        system_soft_wdt_feed();
        os_delay_us(1000);
        main_loop(&mConn);
        break;

    case REQ_REBOOT:
        DBG_PRINTF("REQ_REBOOT\n");
        os_delay_us(10000);
        system_restart();
        while (1) { }
        break;

    default:
        DBG_PRINTF("default\n");
        break;
    }
}
