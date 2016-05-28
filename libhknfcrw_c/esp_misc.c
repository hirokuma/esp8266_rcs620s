#include "hk_misc.h"
#define LOGI(...)
#define LOGE(...)
#define LOGD(...)


/**
 *  @brief	ミリ秒スリープ
 *
 * @param	msec	待ち時間[msec]
 */
void hk_msleep(uint16_t msec) {
	os_delay_us(msec * 1000);
}


/**
 * タイマ開始
 *
 * @param[in]	tmval	タイムアウト時間[msec]
 */
void hk_start_timer(uint16_t tmval)
{
}


/**
 *  タイムアウト監視
 * 
 * @retval	true	タイムアウト発生
 */
bool hk_is_timeout()
{
	return false;
}
