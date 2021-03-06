/*
 * @file	NfcPcd.c
 * @brief	NFCのPCD(Proximity Coupling Device)アクセス実装.
 * 			RC-S620/S用(今のところ).
 */
/*
 * Copyright (c) 2012-2012, hiro99ma
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *         this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *         this list of conditions and the following disclaimer
 *         in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "NfcPcd.h"
#include "HkNfcRwIn.h"
#include "hk_devaccess.h"
#include "hk_misc.h"

// ログ用
#define LOG_TAG "NfcPcd"
#include "nfclog.h"
//#define ENABLE_FRAME_LOG
//#define ENABLE_LOG_DETAIL

/*
 * definition
 */
#define RWBUF_MAX			((uint16_t)(DATA_MAX + 10))
#define NORMAL_DATA_MAX		((uint16_t)255)		//Normalフレームのデータ部最大
#define RESHEAD_LEN			((uint16_t)2)
#define POS_RESDATA			(2)
#define POS_NORMALFRM_DATA	(5)
#define POS_EXTENDFRM_DATA	(8)

/*
 * const
 */
static const uint8_t ACK[] = { 0x00, 0x00, 0xff, 0x00, 0xff, 0x00 };

/*
 * prototype
 */
static bool sendCmd(
		const uint8_t* pCommand, uint16_t CommandLen,
		uint8_t* pResponse, uint16_t* pResponseLen, bool bRecv);
static bool recvResp(uint8_t* pResponse, uint16_t* pResponseLen, uint8_t CmdCode);
static void sendAck(void);
static uint8_t calcDcs(const uint8_t* data, uint16_t len);
static bool inJump(uint8_t Cmd, DepInitiatorParam* pParam);


/*
 * variable
 */
struct {
    bool	opened;	        		///< オープンしているかどうか
    uint8_t	cmdbuf[RWBUF_MAX];  	///< PCDへの送信バッファ
    uint8_t	resbuf[RWBUF_MAX];  	///< PCDからの受信バッファ
    uint8_t	nfcid[MAX_NFCID_LEN];	///< 取得したNFCID
    uint8_t	idlen;					///< 取得済みのNFCID長。0の場合は未取得。

    /// 送信バッファ
    uint8_t sendbuf[RWBUF_MAX];
    /// Normalフレームのデータ部
    uint8_t* frmbuf;
} mNfcMng;

///////////////////////////////////////////////////////////////////////////
// 実装
///////////////////////////////////////////////////////////////////////////


/**
 * コマンドバッファ取得.
 * アドレスを直接取得するので、あまりよろしくないが、いい方法が思いつくまでこのままとする.
 * mNfcMng.sendbufと統合してしまいたいが、後回し.
 *
 * @return		コマンドバッファ(サイズ:DATA_MAX)
 */
uint8_t* NfcPcd_CommandBuf(void)
{
	return mNfcMng.cmdbuf;
}


/**
 * レスポンスバッファ取得.
 * アドレスを直接取得するので、あまりよろしくないが、いい方法が思いつくまでこのままとする.
 * mNfcMng.resbufと統合してしまいたいが、後回し.
 *
 * @return		レスポンスバッファ(サイズ:DATA_MAX)
 */
uint8_t* NfcPcd_ResponseBuf(void)
{
	return mNfcMng.resbuf;
}


/**
 * 保持しているNFCIDサイズ取得.
 * ポーリングによって更新される.
 * NfcPcd_ClrNfcId(void)によって0クリアされる.
 *
 * @return		現在のNFCID長
 */
uint8_t NfcPcd_NfcIdLen(void)
{
	return mNfcMng.idlen;
}


/**
 * 保持しているNFCID取得
 * ポーリングによって更新される.
 * NfcPcd_ClrNfcId(void)によって0クリアされる.
 * 長さは NfcPcd_NfcIdLen(void) で取得すること.
 *
 * @return	NFCID
 */
const uint8_t* NfcPcd_NfcId(void)
{
	return mNfcMng.nfcid;
}


/**
 * 保持しているNFCIDの設定.
 *
 * @param[in]	pNfcId		設定するNFCID
 * @param[in]	len			設定するNFCID長
 */
void NfcPcd_SetNfcId(const uint8_t* pNfcId, uint8_t len)
{
	hk_memcpy(mNfcMng.nfcid, pNfcId, len);
	mNfcMng.idlen = len;
}


/**
 * 保持しているNFCIDのクリア
 */
void NfcPcd_ClrNfcId(void)
{
	mNfcMng.idlen = 0;
	hk_memset(mNfcMng.nfcid, 0x00, MAX_NFCID_LEN);
}


/// オープン済みかどうか
bool NfcPcd_IsOpened(void)
{
	return mNfcMng.opened;
}


/**
 * シリアルポートオープン
 *
 * @retval	true		成功
 * @retval	false		失敗
 */
bool NfcPcd_PortOpen(void)
{
	if(mNfcMng.opened) {
		HkNfcRw_SetLastError(HKNFCERR_ALREADY_OPEN);
		return false;
	}
	mNfcMng.sendbuf[0] = 0x00;
	mNfcMng.sendbuf[1] = 0x00;
	mNfcMng.sendbuf[2] = 0xff;
    mNfcMng.frmbuf     = &(mNfcMng.sendbuf[POS_NORMALFRM_DATA]);
	mNfcMng.opened = hk_nfcrw_open();
	if(!mNfcMng.opened) {
		HkNfcRw_SetLastError(HKNFCERR_LOWLEVEL_OPEN);
	}
	return mNfcMng.opened;
}


/**
 * シリアルポートクローズ
 */
void NfcPcd_PortClose(void)
{
	if(mNfcMng.opened) {
		hk_nfcrw_close();
		mNfcMng.opened = false;
	}
}


/**
 * デバイス初期化
 *
 * @retval	true		初期化成功(=使用可能)
 * @retval	false		初期化失敗
 * @attention			初期化失敗時には、#rfOff()を呼び出すこと
 */
bool NfcPcd_Init(void)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	bool ret;
	uint16_t res_len;

	sendAck();
	hk_msleep(100);

	ret = NfcPcd_Reset();
	if(!ret) {
        LOGE("[init]fail Reset\n");
		return false;
	}

	// RF通信のT/O
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x32;
	mNfcMng.frmbuf[2] = 0x02;		// various timings
	mNfcMng.frmbuf[3] = 0x00;		// RFU
	mNfcMng.frmbuf[4] = 0x00;		// ATR_RES(Initiator mode) : no timeout(デフォルトは0x0B)
	mNfcMng.frmbuf[5] = 0x00;		// 非DEP通信時(InCommunicateThru) : no timeout(デフォルトは0x0A)
	ret = sendCmd(mNfcMng.frmbuf, 6, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("d4 32 02\n");
		HkNfcRw_SetLastError(HKNFCERR_PCD_INIT);
		return false;
	}

	// Target捕捉時のRF通信リトライ回数
	mNfcMng.frmbuf[2] = 0x05;		// Retry
	mNfcMng.frmbuf[3] = 0x00;		// ATR_REQ/RES : only once
	mNfcMng.frmbuf[4] = 0x00;		// PSL_REQ/RES : only once
	mNfcMng.frmbuf[5] = 0x00;		// InListPassiveTarget : only once
	ret = sendCmd(mNfcMng.frmbuf, 6, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("d4 32 05(%d)%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INIT);
		return false;
	}

	// RF出力ONからTargetID取得コマンド送信までの追加ウェイト時間
	mNfcMng.frmbuf[2] = 0x81;		// wait
	mNfcMng.frmbuf[3] = 0xb7;		// 0.1 x prm + 5 + 0.7 [ms]
									// 0xb7は、サンプルソースの値
	ret = sendCmd(mNfcMng.frmbuf, 4, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("d4 32 81(%d)%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INIT);
		return false;
	}

	// DEPターゲットでのタイムアウト
	mNfcMng.frmbuf[2] = 0x82;		// DEP Target Timeout
	mNfcMng.frmbuf[3] = 0x08;		// gbyAtrResTo
										// 00h :    302us
										// 01h :    604us
										// 02h :   1.208ms
										// 03h :   2.417ms
										// 04h :   4.833ms
										// 05h :   9.666ms
										// 06h :  19.332ms
										// 07h :  38.664ms
										// 08h :  77.329ms ★
										// 09h : 154.657ms
										// 0ah : 309.314ms
										// 0bh : 618.629ms
										// 0ch :   1.237s
										// 0dh :   2.474s
										// 0eh :   4.949s
	mNfcMng.frmbuf[4] = 0x01;		// gbyRtox
										// RWT x gbyRtox
	mNfcMng.frmbuf[5] = 0x08;		// gbyTargetStoTo
										// 00h : 951us
										// 01h : 1.1ms
										// 02h : 1.4ms
										// 03h : 2.0ms
										// 04h : 3.2ms
										// 05h : 5.6ms
										// 06h : 10.4ms
										// 07h : 20.1ms
										// 08h : 39.4ms ★
										// 09h : 78.1ms
										// 0ah : 155.4ms
										// 0bh : 310.0ms
										// 0ch : 619.sms
										// 0dh : 1.237s
										// 0eh : 2.474s
	ret = sendCmd(mNfcMng.frmbuf, 6, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("d4 32 82(%d)%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INIT);
		return false;
	}


#if 0
	// nfcpyのまね4
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x32;		//RFConfiguration
	mNfcMng.frmbuf[2] = 0x0b;
	hk_memcpy(mNfcMng.frmbuf + 3, mNfcMng.resbuf + POS_RESDATA, READREG_LEN);
	ret = sendCmd(mNfcMng.frmbuf, 3+READREG_LEN, mNfcMng.resbuf, &res_len, true);
	if(!ret || res_len != RESHEAD_LEN) {
		LOGE("32 0b(%d) / %d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INIT);
		return false;
	}
#endif

	return ret;
}


/**
 * 搬送波停止
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_RfOff(void)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	uint8_t prm = 0x00;		// bit1 : Auto RFCA : OFF
							// bit0 : RF ON/OFF : OFF

	return NfcPcd_RfConfiguration(0x01, &prm, 1);
}


/**
 * RFConfiguration
 *
 * @param[in]	cmd				コマンド
 * @param[in]	pCommand		送信するコマンド引数
 * @param[in]	CommandLen		pCommandの長さ
 * @retval	true		成功
 * @retval	false		失敗
 */
bool NfcPcd_RfConfiguration(uint8_t cmd, const uint8_t* pCommand, uint8_t CommandLen)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x32;		//RFConfiguration
	mNfcMng.frmbuf[2] = cmd;
	hk_memcpy(mNfcMng.frmbuf + 3, pCommand, CommandLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3 + CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("rfConfiguration [cmd:0x%02x]ret=%d, ren=%d\n", cmd, ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_CFG);
		return false;
	}

	return true;
}


/**
 * Reset
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_Reset(void)
{
	//LOGD("%s\n", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x18;		//Reset
	mNfcMng.frmbuf[2] = 0x01;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("reset ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_RESET);
		return false;
	}
	
	// execute
	sendAck();
	hk_msleep(10);

	return true;
}


/**
 * Diagnose
 *
 * @param[in]	Cmd				Diagnoseコマンド
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_Diagnose(uint8_t Cmd, const uint8_t* pCommand, uint8_t CommandLen,
											uint8_t* pResponse, uint8_t* pResponseLen)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x00;			//Diagnose
	mNfcMng.frmbuf[2] = Cmd;
	hk_memcpy(mNfcMng.frmbuf + 3, pCommand, CommandLen);
	
	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3 + CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN)) {
		LOGE("diagnose ret=%d/%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_DIAG);
		return false;
	}
	
	*pResponseLen = res_len - RESHEAD_LEN;
	hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN, *pResponseLen);

	return true;
}


/**
 * SetParameters
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_SetParameters(uint8_t val)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x12;
	mNfcMng.frmbuf[2] = val;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("setParam ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_SETP);
		return false;
	}

	return true;
}


/**
 * WriteRegister
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_WriteRegister(const uint8_t* pCommand, uint8_t CommandLen)
{
	//LOGD("%s", __PRETTY_FUNCTION__);
	int i;

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x08;
	hk_memcpy(mNfcMng.frmbuf + 2, pCommand, CommandLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2 + CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN + CommandLen/2)) {
		LOGE("writeReg ret=%d res_len=%d\n", ret, res_len);
	} else {
		for(i=0; i<CommandLen/2; i++) {
			if(mNfcMng.resbuf[2+i] != 0x00) {
				LOGE("writeReg [%02x]\n", mNfcMng.resbuf[2+i]);
				HkNfcRw_SetLastError(HKNFCERR_PCD_WRITEREG);
				return false;
			}
		}
	}

	return true;
}


bool NfcPcd_PowerDown(uint8_t wake)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x16;
	mNfcMng.frmbuf[2] = wake;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN)) {
		LOGE("powerDown ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_POWERDOWN);
		return false;
	}

	return true;
}


/**
 * GetFirmwareVersion
 *
 * @param[out]	pResponse		レスポンス(GF_XXでアクセス)
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_GetFirmwareVersion(uint8_t* pResponse)
{
	//LOGD("%s\n", __PRETTY_FUNCTION__);

	const uint16_t RES_LEN = 4;
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x02;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN + RES_LEN)) {
		LOGE("getFirmware ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_GETFW);
		return false;
	}
	
	if(pResponse) {
		hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN, RES_LEN);
	}

	return true;
}


/**
 * GetGeneralStatus
 *
 * @param[out]	pResponse		レスポンス(GGS_XXでアクセス)
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_GetGeneralStatus(uint8_t* pResponse)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x04;

	const uint16_t RES_LEN = 5;
	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN + RES_LEN)) {
		LOGE("getGeneralStatus ret=%d/%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_GETSTAT);
		return false;
	}
	
	hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN, RES_LEN);

	return true;
}

#if 0
/**
 * CommunicateThruEX
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_CommunicateThruEx0(void)
{
	//LOGD("%s : [%d]", __PRETTY_FUNCTION__, CommandLen);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0xa0;		//CommunicateThruEX
	mNfcMng.frmbuf[2] = 0x00;
	mNfcMng.frmbuf[3] = 0x00;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 4, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1)) {
		LOGE("communicateThruEx ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_COMMTHRUEX);
		return false;
	}

	return true;
}


/**
 * CommunicateThruEX
 *
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_CommunicateThruEx2(
			const uint8_t* pCommand, uint8_t CommandLen,
			uint8_t* pResponse, uint8_t* pResponseLen)
{
	//LOGD("%s : [%d]", __PRETTY_FUNCTION__, CommandLen);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0xa0;		//CommunicateThruEX
	hk_memcpy(mNfcMng.frmbuf + 2, pCommand, CommandLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2 + CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1)) {
		LOGE("communicateThruEx ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_COMMTHRUEX);
		return false;
	}
	if(res_len == 3) {
		//Statusを返す
		pResponse[0] = mNfcMng.resbuf[2];
		*pResponseLen = 1;
	} else {
		if((mNfcMng.resbuf[2] != 0x00) || (res_len != (3 + mNfcMng.resbuf[3]))) {
			HkNfcRw_SetLastError(HKNFCERR_PCD_COMMTHRUEX);
			return false;
		}
		//Statusは返さない
		*pResponseLen = (uint8_t)(mNfcMng.resbuf[3] - 1);
		hk_memcpy(pResponse, mNfcMng.resbuf + 4, *pResponseLen);
	}

	return true;
}
#endif


/**
 * CommunicateThruEX
 *
 * @param[in]	Timeout			タイムアウト値[msec]
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 *
 * @note		-# Timeoutは往復分の時間を設定すること.
 */
bool NfcPcd_CommunicateThruEx(
			uint16_t Timeout,
			const uint8_t* pCommand, uint8_t CommandLen,
			uint8_t* pResponse, uint8_t* pResponseLen)
{
	//LOGD("%s : (%d)", __PRETTY_FUNCTION__, CommandLen);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0xa0;		//CommunicateThruEX
	mNfcMng.frmbuf[2] = l16(Timeout);
	mNfcMng.frmbuf[3] = h16(Timeout);
	if(CommandLen) {
		hk_memcpy(mNfcMng.frmbuf + 4, pCommand, CommandLen);
		CommandLen += 4;
	}

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1)) {
		LOGE("communicateThruEx ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_COMMTHRUEX);
		return false;
	}
	if(res_len == 3) {
		//Statusを返す
		pResponse[0] = mNfcMng.resbuf[POS_RESDATA];
		*pResponseLen = 1;
	} else {
		//Statusは返さない
		*pResponseLen = (uint8_t)(res_len - 3);
		hk_memcpy(pResponse, mNfcMng.resbuf + POS_RESDATA + 1, *pResponseLen);
	}

	return true;
}


/////////////////////////////////////////////////////////////////////////
// Initiator Command
/////////////////////////////////////////////////////////////////////////

#ifdef HKNFCRW_USE_SNEP_INITIATOR
/**
 * InJumpForDEP
 *
 * @param[in,out]	pParam		DEP initiatorパラメータ
 */
bool NfcPcd_InJumpForDep(DepInitiatorParam* pParam)
{
	//LOGD("%s", __PRETTY_FUNCTION__);
	return inJump(0x56, pParam);
}


/**
 * InJumpForPSL
 *
 * @param[in,out]	pParam		DEP initiatorパラメータ
 */
bool NfcPcd_InJumpForPsl(DepInitiatorParam* pParam)
{
	//LOGD("%s", __PRETTY_FUNCTION__);
	return inJump(0x46, pParam);
}
#endif	//HKNFCRW_USE_SNEP_INITIATOR


/**
 * InDataExchange
 *
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_InDataExchange(
			const uint8_t* pCommand, uint8_t CommandLen,
			uint8_t* pResponse, uint8_t* pResponseLen)
{
	if(CommandLen > 252) {
		LOGE("Too large\n");
		HkNfcRw_SetLastError(HKNFCERR_PCD_INDTEX);
		return false;
	}

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x40;			//InDataExchange
	mNfcMng.frmbuf[2] = 0x01;			//Tg
#if 0
	if(bCoutinue) {
		mNfcMng.frmbuf[2] |= 0x40;	//MI
	}
#endif
	hk_memcpy(mNfcMng.frmbuf + 3, pCommand, CommandLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3 + CommandLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("ret=%d / len=%d / code=%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INDTEX);
		return false;
	}

	*pResponseLen = res_len - (RESHEAD_LEN+1);
	hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN+1, *pResponseLen);

	return true;
}


/**
 * InListPassiveTarget
 *
 * @param[in]	pInitData		InListPassiveTargetの引数
 * @param[in]	InitLen			pInitDataの長さ
 * @param[out]	ppTgData		InListPassiveTargeの戻り値(内部バッファを返す)
 * @param[out]	pTgLen			*ppTgDataの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_InListPassiveTarget(
			const uint8_t* pInitData, uint8_t InitLen,
			uint8_t** ppTgData, uint8_t* pTgLen)
{
	uint16_t responseLen;
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x4a;				//InListPassiveTarget
	mNfcMng.frmbuf[2] = 0x01;
	hk_memcpy(mNfcMng.frmbuf + 3, pInitData, InitLen);

	bool ret = sendCmd(mNfcMng.frmbuf, 3+InitLen, mNfcMng.resbuf, &responseLen, true);
	if(!ret || mNfcMng.resbuf[POS_RESDATA] != 0x01) {	//NbTg==1
		HkNfcRw_SetLastError(HKNFCERR_PCD_INPSV);
		return false;
	}
	*ppTgData = mNfcMng.resbuf;
	*pTgLen = (uint8_t)responseLen;

	return true;
}


/**
 * InCommunicateThru
 *
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_InCommunicateThru(
			const uint8_t* pCommand, uint8_t CommandLen,
			uint8_t* pResponse, uint8_t* pResponseLen)
{
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x42;			//InCommunicateThru
	hk_memcpy(mNfcMng.frmbuf + 2, pCommand, CommandLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2 + CommandLen, mNfcMng.resbuf, &res_len, true);
//	for(int i=0; i<res_len; i++) {
//		LOGD("%02x \n", mNfcMng.resbuf[i]);
//	}
	if(!ret || (res_len < RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("InCommunicateThru ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INCOMMTHRU);
		return false;
	}

	*pResponseLen = res_len - (RESHEAD_LEN+1);
	hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN+1, *pResponseLen);

	return true;
}


/**
 * InRelease
 *
 * DEP用。Targetの解放
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_InRelease()
{
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x52;			//InRelease
	mNfcMng.frmbuf[2] = 0x00;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 3, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("inRelease ret=%d / len=%d / code=%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INREL);
		return false;
	}

	return true;
}


/////////////////////////////////////////////////////////////////////////
// Target Command
/////////////////////////////////////////////////////////////////////////

#ifdef HKNFCRW_USE_SNEP_TARGET
/**
 * TgInitAsTarget
 *
 * ターゲットとして起動する.
 * 応答はTgResponseToInitiatorで返す.
 *
 * [in,out]		pParam			ターゲットパラメータ
 * 
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_TgInitAsTarget(TargetParam* pParam)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	uint16_t len = 0;

	mNfcMng.frmbuf[len++] = 0xd4;
	mNfcMng.frmbuf[len++] = 0x8c;				//TgInitAsTarget

	//mode
	//mNfcMng.frmbuf[len++] = 0x00;		// Card Emu. OK && active OK
	//mNfcMng.frmbuf[len++] = 0x01;		// Card Emu. OK && Passive Only
	mNfcMng.frmbuf[len++] = 0x02;		// DEP only && active OK
	//mNfcMng.frmbuf[len++] = 0x03;		// DEP only && Passive Only

	//MIFARE
	//SENS_RES
	mNfcMng.frmbuf[len++] = 0x00;
	mNfcMng.frmbuf[len++] = 0x04;		//0x00はだめ
	//NFCID1
#if 0
	if(pParam->pUId) {
		hk_memcpy(mNfcMng.frmbuf + len, pParam->pUId, 3);
		len += 3;
	} else {
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
	}
#else
	mNfcMng.frmbuf[len++] = 0x12;
	mNfcMng.frmbuf[len++] = 0x34;
	mNfcMng.frmbuf[len++] = 0x56;
#endif
	//SEL_RES
	mNfcMng.frmbuf[len++] = 0x40;		//固定

	//FeliCaParams
	//IDm
	uint8_t idm_pos = len;
#if 0
	if(pParam->pIDm) {
		hk_memcpy(mNfcMng.frmbuf + len, pParam->pIDm, NFCID2_LEN);
		len += NFCID2_LEN;
	} else {
		//自動生成
		mNfcMng.frmbuf[len++] = 0x01;
		mNfcMng.frmbuf[len++] = 0xfe;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
		mNfcMng.frmbuf[len++] = rand() & 0xff;
	}
#else
	//自動生成
	mNfcMng.frmbuf[len++] = 0x01;
	mNfcMng.frmbuf[len++] = 0xfe;
	mNfcMng.frmbuf[len++] = 0x12;
	mNfcMng.frmbuf[len++] = 0x34;
	mNfcMng.frmbuf[len++] = 0x56;
	mNfcMng.frmbuf[len++] = 0x78;
	mNfcMng.frmbuf[len++] = 0x9a;
	mNfcMng.frmbuf[len++] = 0xbc;
#endif
	//PMm
	hk_memset(mNfcMng.frmbuf + len, 0xff, 8);
	len += 8;
	//SystemCode
#if 0
	mNfcMng.frmbuf[len++] = h16(pParam->SystemCode);
	mNfcMng.frmbuf[len++] = l16(pParam->SystemCode);
#else
	mNfcMng.frmbuf[len++] = 0xff;
	mNfcMng.frmbuf[len++] = 0xff;
#endif

	//NFCID3t
	hk_memcpy(mNfcMng.frmbuf + len, mNfcMng.frmbuf + idm_pos, NFCID2_LEN);
	len += NFCID2_LEN;
	mNfcMng.frmbuf[len++] = 0x00;
	mNfcMng.frmbuf[len++] = 0x00;

	//General Bytes
	if(pParam->GbLen) {
		hk_memcpy(mNfcMng.frmbuf + len, pParam->pGb, pParam->GbLen);
		len += pParam->GbLen;
	}


//	for(int i=0; i<len; i++) {
//		LOGD("%02x ", mNfcMng.frmbuf[i]);
//	}

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, len, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1)) {
		LOGE("fail : TgInitAsTarget ret=%d/len=%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGINIT);
		return false;
	}

	if(pParam->pCommand) {
		//Activated情報以下
		pParam->CommandLen = (uint8_t)(res_len - 2);
		hk_memcpy(pParam->pCommand, mNfcMng.resbuf + 2, pParam->CommandLen);
	}

	return true;
}


bool NfcPcd_TgSetGeneralBytes(const TargetParam* pParam)
{
	if(pParam->GbLen > 48) {
		LOGE("Too large\n");
		return false;
	}

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x92;				//TgSetGeneralBytes
	if(pParam->GbLen) {
		hk_memcpy(mNfcMng.frmbuf + 2, pParam->pGb, pParam->GbLen);
	}

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2 + pParam->GbLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("tgSetGeneralBytes ret=%d / len=%d / code=%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGSETGB);
		return false;
	}

	return true;
}


/**
 * TgGetData
 *
 * DEP用。Initiatorからのデータを取得する。
 *
 * @param[out]	pCommand		Initiatorからの送信データ
 * @param[out]	pCommandLen		pCommandの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_TgGetData(uint8_t* pCommand, uint8_t* pCommandLen)
{
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x86;				//TgGetData

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("tgGetData ret=%d / len=%d / status=0x%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
#ifdef ENABLE_LOG_DETAIL
		if(mNfcMng.resbuf[POS_RESDATA] == 0x29) {
			LOGI("initiator released\n");
		}
#endif //ENABLE_LOG_DETAIL
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGGETDT);
		return false;
	}

	*pCommandLen = res_len - (RESHEAD_LEN+1);
	hk_memcpy(pCommand, mNfcMng.resbuf + RESHEAD_LEN+1, *pCommandLen);

	return true;
}


/**
 * TgSetData
 *
 * DEP用。Initiatorへデータを返す。
 *
 * @param[in]	pResponse		Initiatorに送信するコマンド
 * @param[in]	ResponseLen		pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_TgSetData(const uint8_t* pResponse, uint8_t ResponseLen)
{
	if(ResponseLen > 252) {
		LOGE("Too large\n");
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGSETDT);
		return false;
	}

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x8e;			//TgSetData
	hk_memcpy(mNfcMng.frmbuf + 2, pResponse, ResponseLen);

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2 + ResponseLen, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("tgSetData ret=%d / len=%d / code=%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGSETDT);
		return false;
	}

	return true;
}
#endif	//HKNFCRW_USE_SNEP_TARGET



/**
 * TgResponseToInitiator
 *
 * TgInitAsTargetおよびTgGetInitiatorCommandのレスポンスに対するカードとしての応答
 *
 * @param[in]	pData			応答データ
 * @param[in]	DataLen			pDataの長さ
 * @param[out]	pResponse		レスポンス
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_TgResponseToInitiator(
			const uint8_t* pData, uint8_t DataLen,
			uint8_t* pResponse, uint8_t* pResponseLen)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	uint16_t len = 0;

	mNfcMng.frmbuf[len++] = 0xd4;
	mNfcMng.frmbuf[len++] = 0x90;				//TgResponseToInitiator

	//応答
//ここは練り直した方がいい
	mNfcMng.frmbuf[len++] = 0xd5;
	hk_memcpy(&(mNfcMng.frmbuf[len]), pData, DataLen);
	len += DataLen;

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, len, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len != 3) || mNfcMng.resbuf[POS_RESDATA] != 0x00) {
		LOGE("tgResponseToInitiator ret=%d\n", ret);
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGRES);
		return false;
	}

	if(pResponse && pResponseLen) {
		*pResponseLen = (uint8_t)(res_len - (RESHEAD_LEN+1));
		hk_memcpy(pResponse, &(mNfcMng.resbuf[POS_RESDATA+1]), *pResponseLen);
	}

	return true;
}


/**
 * TgGetInitiatorCommand
 *
 * Initiatorからのコマンドを取得する。
 * 応答はTgResponseToInitiatorで返す。
 *
 * @param[out]	pResponse		応答データ
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
bool NfcPcd_TgGetInitiatorCommand(uint8_t* pResponse, uint8_t* pResponseLen)
{
	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = 0x88;				//TgGetInitiatorCommand

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, 2, mNfcMng.resbuf, &res_len, true);
	if(!ret || (res_len < RESHEAD_LEN+1) || (mNfcMng.resbuf[POS_RESDATA] != 0x00)) {
		LOGE("tgGetInitiatorCommand ret=%d / len=%d / code=%02x\n", ret, res_len, mNfcMng.resbuf[POS_RESDATA]);
		HkNfcRw_SetLastError(HKNFCERR_PCD_TGGETINI);
		return false;
	}

	*pResponseLen = res_len - (RESHEAD_LEN+1);
	hk_memcpy(pResponse, mNfcMng.resbuf + RESHEAD_LEN+1, *pResponseLen);

	return true;
}


/////////////////////////////////////////////////////////////////////////
// RC-S620/Sとのやりとり
/////////////////////////////////////////////////////////////////////////


/**
 * パケット送受信
 *
 * @param[in]	pCommand		送信するコマンド
 * @param[in]	CommandLen		pCommandの長さ
 * @param[out]	pResponse		レスポンス(0xd5含む)
 * @param[out]	pResponseLen	pResponseの長さ
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
static bool sendCmd(
			const uint8_t* pCommand, uint16_t CommandLen,
			uint8_t* pResponse, uint16_t* pResponseLen,
			bool bRecv)
{
#ifdef ENABLE_FRAME_LOG
	int i;
#endif

	//LOGD("CommandLen : %d", CommandLen);
	*pResponseLen = 0;

	if(CommandLen > DATA_MAX) {
		LOGE("too large\n");
		return false;
	}

	uint16_t send_len = 3;
	uint8_t dcs = calcDcs(pCommand, CommandLen);

	if(CommandLen <= NORMAL_DATA_MAX) {
		// Normalフレーム
		mNfcMng.sendbuf[send_len++] = CommandLen;						//[3]
		mNfcMng.sendbuf[send_len++] = (uint8_t)(0 - mNfcMng.sendbuf[3]);	//[4]
		if(pCommand != mNfcMng.sendbuf + POS_NORMALFRM_DATA) {
			hk_memcpy(mNfcMng.sendbuf + send_len, pCommand, CommandLen);
		}
	} else {
		// Extendedフレーム
		mNfcMng.sendbuf[send_len++] = 0xff;							//[3]
		mNfcMng.sendbuf[send_len++] = 0xff;							//[4]
		mNfcMng.sendbuf[send_len++] = (uint8_t)(CommandLen >> 8);		//[5]
		mNfcMng.sendbuf[send_len++] = (uint8_t)CommandLen;			//[6]
		mNfcMng.sendbuf[send_len++] = (uint8_t)(0 - mNfcMng.sendbuf[5] - mNfcMng.sendbuf[6]);
		if(pCommand != mNfcMng.sendbuf + POS_EXTENDFRM_DATA) {
			hk_memcpy(mNfcMng.sendbuf + send_len, pCommand, CommandLen);
		}
	}
	send_len += CommandLen;
	mNfcMng.sendbuf[send_len++] = dcs;
	mNfcMng.sendbuf[send_len++] = 0x00;

#ifdef ENABLE_FRAME_LOG
	LOGD("------------\n");
	for(i=0; i<send_len; i++) {
		LOGD("[W]%02x \n", mNfcMng.sendbuf[i]);
	}
	LOGD("------------\n");
#endif

	if(hk_nfcrw_write(mNfcMng.sendbuf, send_len) != send_len) {
		LOGE("write error.\n");
		hk_nfcrw_close();
		hk_msleep(500);
		bool op = hk_nfcrw_open();
		if(!op) {
			LOGE("reopen error.\n");
			HkNfcRw_SetLastError(HKNFCERR_LOWLEVEL_OPEN);
		}
		return false;
	}

	//ACK受信
	uint8_t res_buf[6];
	hk_nfcrw_read_timeout(30);		/* 30msタイムアウト(115200bps時) */
									/*
									 * 送信がバッファにためるだけのことがあるので、
									 * コマンドの最大長(265byte)を送信し、
									 * ACK応答までの最大時間(3.5ms)を加え、
									 * ACK(5byte)を返信する時間まで考慮した。
									 * 
									 * 送信   :23.0ms
									 * ACK応答: 3.5ms
									 * ACK返信: 0.5ms
									 * あわせると27ms強なので、30msにしておく。
									 */
	uint16_t ret_len = hk_nfcrw_read(res_buf, 6);
	if((ret_len != 6) || (hk_memcmp(res_buf, ACK, sizeof(ACK)) != 0)) {
		LOGE("sendCmd 0: ret=%d\n", ret_len);
#ifdef ENABLE_FRAME_LOG
		LOGD("------------\n");
		for(i=0; i<ret_len; i++) {
			LOGD("[ack]%02x \n", res_buf[i]);
		}
		LOGD("------------\n");
#endif
		sendAck();
		return false;
	}
	hk_nfcrw_read_timeout(0);		/* タイムアウト終了 */

	// レスポンス
	return (bRecv) ? recvResp(pResponse, pResponseLen, pCommand[1]) : true;
}


/**
 * レスポンス受信
 *
 * @param[out]	pResponse		レスポンス(0xd5, cmd+1含む)
 * @param[out]	pResponseLen	pResponseの長さ
 * @param[in]	CmdCode			送信コマンド(省略可)
 *
 * @retval		true			成功
 * @retval		false			失敗
 */
static bool recvResp(uint8_t* pResponse, uint16_t* pResponseLen, uint8_t CmdCode)
{
#ifdef ENABLE_FRAME_LOG
	int i;
#endif
	uint8_t res_buf[6];
	uint16_t ret_len = hk_nfcrw_read(res_buf, 5);
	if(ret_len != 5) {
		LOGE("recvResp 1: ret=%d\n", ret_len);
		sendAck();
		return false;
	}

	if ((res_buf[0] != 0x00) || (res_buf[1] != 0x00) || (res_buf[2] != 0xff)) {
		LOGE("recvResp 2\n");
		return false;
	}
	if((res_buf[3] == 0xff) && (res_buf[4] == 0xff)) {
		// extend frame
		ret_len = hk_nfcrw_read(res_buf, 3);
		if((ret_len != 3) || (((res_buf[0] + res_buf[1] + res_buf[2]) & 0xff) != 0)) {
			LOGE("recvResp 3: ret=%d\n", ret_len);
			return false;
		}
		*pResponseLen = (((uint16_t)res_buf[0] << 8) | res_buf[1]);
	} else {
		// normal frame
		if(((res_buf[3] + res_buf[4]) & 0xff) != 0) {
			LOGE("recvResp 4\n");
			return false;
		}
		*pResponseLen = res_buf[3];
	}
	if(*pResponseLen > RWBUF_MAX) {
		LOGE("recvResp 5  len:%d\n", *pResponseLen);
		return false;
	}

	ret_len = hk_nfcrw_read(pResponse, *pResponseLen);

#ifdef ENABLE_FRAME_LOG
	LOGD("------------\n");
	for(i=0; i<ret_len; i++) {
		LOGD("[R]%02x \n", pResponse[i]);
	}
	LOGD("------------\n");
#endif

	if((ret_len != *pResponseLen) || (pResponse[0] != 0xd5)) {
		if((ret_len == 1) && (pResponse[0] == 0x7f)) {
			LOGE("recvResp 6 : Error Frame\n");
		} else {
			LOGE("recvResp 6 :[%02x] ret_len=%d\n", pResponse[0], ret_len);
		}
		sendAck();
		return false;
	}
	if((CmdCode != 0xff) && (pResponse[1] != CmdCode+1)) {
		LOGE("recvResp 7 : ret=%d\n", pResponse[1]);
		sendAck();
		return false;
	}

	uint8_t dcs = calcDcs(pResponse, *pResponseLen);
	ret_len = hk_nfcrw_read(res_buf, 2);
	if((ret_len != 2) || (res_buf[0] != dcs) || (res_buf[1] != 0x00)) {
		LOGE("recvResp 8\n");
		sendAck();
		return false;
	}

	return true;
}


/**
 * DCS計算
 *
 * @param[in]		data		計算元データ
 * @param[in]		len			dataの長さ
 *
 * @return			DCS値
 */
static uint8_t calcDcs(const uint8_t* data, uint16_t len)
{
	uint8_t sum = 0;
	uint16_t i;
	for(i = 0; i < len; i++) {
		sum += data[i];
	}
	return (uint8_t)(0 - (sum & 0xff));
}


/**
 * ACK送信
 */
static void sendAck(void)
{
	//LOGD("%s\n", __PRETTY_FUNCTION__);

	hk_nfcrw_write(ACK, sizeof(ACK));

	// wait 1ms
	hk_msleep(1);
}



#ifdef HKNFCRW_USE_SNEP_INITIATOR
/**
 * InJumpForDEP or InJumpForPSL
 *
 * @param[in]	pParam		DEP initiatorパラメータ
 */
static bool inJump(uint8_t Cmd, DepInitiatorParam* pParam)
{
	//LOGD("%s", __PRETTY_FUNCTION__);

	mNfcMng.frmbuf[0] = 0xd4;
	mNfcMng.frmbuf[1] = Cmd;
	mNfcMng.frmbuf[2] = (uint8_t)pParam->Ap;
	mNfcMng.frmbuf[3] = (uint8_t)pParam->Br;
	mNfcMng.frmbuf[4] = 0x00;		//Next
	uint8_t len = 5;
	if(pParam->Ap == AP_PASSIVE) {
		if(pParam->Br == BR_106K) {
//			mNfcMng.frmbuf[4] |= 0x01;
//			const uint8_t known_id[4] = { 0x08, 0x01, 0x02, 0x03 };
//			hk_memcpy(&(mNfcMng.frmbuf[len]), known_id, sizeof(known_id));
//			len += sizeof(known_id);
		} else {
			mNfcMng.frmbuf[4] |= 0x01;
			const uint8_t pol_req[5] = { 0x00, 0xff, 0xff, 0x01, 0x00 };
			hk_memcpy(&(mNfcMng.frmbuf[len]), pol_req, sizeof(pol_req));
			len += sizeof(pol_req);
		}
	}
	if(pParam->pNfcId3) {
		mNfcMng.frmbuf[4] |= 0x02;
		hk_memcpy(&(mNfcMng.frmbuf[len]), pParam->pNfcId3, NFCID3_LEN);
		len += NFCID3_LEN;
	}
	if(pParam->pGb && pParam->GbLen) {
		mNfcMng.frmbuf[4] |= 0x04;
		hk_memcpy(&(mNfcMng.frmbuf[len]), pParam->pGb, pParam->GbLen);
		len += pParam->GbLen;
	}

	uint16_t res_len;
	bool ret = sendCmd(mNfcMng.frmbuf, len, mNfcMng.resbuf, &res_len, true);

	if(!ret || (res_len < RESHEAD_LEN+17)) {
		LOGE("inJumpForDep ret=%d/len=%d\n", ret, res_len);
		HkNfcRw_SetLastError(HKNFCERR_PCD_INJUMP);
		return false;
	}

	if(pParam->pResponse) {
		pParam->ResponseLen = (uint8_t)(res_len - (RESHEAD_LEN + 1));
		hk_memcpy(pParam->pResponse, mNfcMng.resbuf + RESHEAD_LEN+1, pParam->ResponseLen);
	}

	return true;
}
#endif	//HKNFCRW_USE_SNEP_INITIATOR
