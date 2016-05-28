#include "hk_misc.h"
#define LOGI(...)
#define LOGE(...)
#define LOGD(...)


/**
 *  @brief	�~���b�X���[�v
 *
 * @param	msec	�҂�����[msec]
 */
void hk_msleep(uint16_t msec) {
	os_delay_us(msec * 1000);
}


/**
 * �^�C�}�J�n
 *
 * @param[in]	tmval	�^�C���A�E�g����[msec]
 */
void hk_start_timer(uint16_t tmval)
{
}


/**
 *  �^�C���A�E�g�Ď�
 * 
 * @retval	true	�^�C���A�E�g����
 */
bool hk_is_timeout()
{
	return false;
}
