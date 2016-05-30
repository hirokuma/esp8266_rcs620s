#include "hk_devaccess.h"
#include "hk_misc.h"
#include "misc.h"
#include "uart.h"

// デバッグ設定
//#define DBG_WRITEDATA
//#define DBG_READDATA
//#define HKNFCRW_ENABLE_DEBUG

#ifdef HKNFCRW_ENABLE_DEBUG
#include <stdio.h>
#define LOGI	printf
#define LOGE	printf
#define LOGD	printf

#else
#define LOGI(...)
#define LOGE(...)
#define LOGD(...)

#endif	//HKNFCRW_ENABLE_DEBUG


/**
 * ポートオープン
 *
 * @retval	true	オープン成功
 */
bool  hk_nfcrw_open(void)
{
	return true;
}


/**
 * ポートクローズ
 */
void  hk_nfcrw_close(void)
{
}


/**
 * ポート送信
 *
 * @param[in]	data		送信データ
 * @param[in]	len			dataの長さ
 * @return					送信したサイズ
 */
uint16_t  hk_nfcrw_write(const uint8_t* data, uint16_t len)
{
//    int i;
//    for (i = 0; i < len; i++) {
//        DBG_PRINTF("%02x ", data[i]);
//    }
//    DBG_PRINTF("\n");

	uart0_tx_buffer((uint8*)data, len);

	return len;
}

/**
 * 受信
 *
 * @param[out]	data		受信バッファ
 * @param[in]	len			受信サイズ
 *
 * @return					受信したサイズ
 *
 * @attention	- len分のデータを受信するか失敗するまで処理がブロックされる。
 */
uint16_t  hk_nfcrw_read(uint8_t* data, uint16_t len)
{
	uint16_t ret_len = len;
    int loop = 500;

    while (loop--) {
        Uart_rx_buff_enq();
        uint16_t sz = rx_buff_deq((char*)data, len);
        len -= sz;
        data += sz;
        if (len == 0) {
            break;
        }
        hk_msleep(2);
    }

	return ret_len - len;
}

/**
 * ポート受信タイムアウト時間設定
 * 
 * タイムアウト処理が可能な場合、受信タイムアウト時間を設定する。
 * タイムアウトがない場合は、何も処理しないし、#hk_nfcrw_read()にも影響はない。
 *
 * @param[in]	msec		タイムアウト時間(ミリ秒)。0のときはタイムアウト解除。
 */
void hk_nfcrw_read_timeout(uint16_t msec)
{
}
