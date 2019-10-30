#pragma once
//*****************************************************************************
// Eventクラス
//*****************************************************************************
#include "Type.h"


class CEvent
{
public:
	typedef enum
	{
		RESULT_SUCCESS = 0x00000000,					// 正常
		RESULT_RECIVE_EVENT = 0x01111111,				// イベント待ちにてイベントを受信
		RESULT_WAIT_TIMEOUT = 0x09999999,				// イベント待ちにてタイムアウトが発生
		RESULT_ERROR_EVENT_FD = 0xE0000001,				// イベントファイルディスクリプタが取得できなかった
		RESULT_ERROR_EVENT_SET = 0xE0000002,			// イベント設定失敗
		RESULT_ERROR_EVENT_RESET = 0xE0000003,			// イベントリセット失敗
		RESULT_ERROR_EVENT_WAIT = 0xE0000004,			// イベント待ち失敗
		RESULT_ERROR_SYSTEM = 0xE9999999,				// システム異常
	} RESULT_ENUM;


private:
	int								m_efd;				// イベントファイルディスクリプタ
	int								m_errno;			// エラー番号

public:
	CEvent();
	~CEvent();
	RESULT_ENUM Init();
	int GetEdf();
	int GetErrorNo();
	RESULT_ENUM SetEvent();
	RESULT_ENUM ResetEvent();
	RESULT_ENUM Wait(DWORD dwTimeout = 0);
};

