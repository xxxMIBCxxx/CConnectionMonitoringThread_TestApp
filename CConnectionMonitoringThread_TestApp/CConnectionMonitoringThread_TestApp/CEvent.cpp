//*****************************************************************************
// Eventクラス
//*****************************************************************************
#include "CEvent.h"
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define _CEVENT_DEBUG_


//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CEvent::CEvent()
{
	m_efd = -1;
}


//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
CEvent::~CEvent()
{
	// イベントファイルディスクリプタを解放
	if (m_efd == -1)
	{
		close(m_efd);
		m_efd = -1;
	}
}


//-----------------------------------------------------------------------------
// 初期処理
//-----------------------------------------------------------------------------
CEvent::RESULT_ENUM CEvent::Init()
{
	// イベントファイルディスクリプタを生成
	m_efd = eventfd(0, 0);
	if (m_efd == -1)
	{
		m_errno = errno;
#ifdef _CEVENT_DEBUG_
		perror("CEvent - eventfd");
#endif	// #ifdef _CEVENT_DEBUG_
		return RESULT_ERROR_EVENT_FD;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// イベントファイルディスクリプタを取得
//-----------------------------------------------------------------------------
int CEvent::GetEdf()
{
	return m_efd;
}


//-----------------------------------------------------------------------------
// エラー番号を取得
//-----------------------------------------------------------------------------
int CEvent::GetErrorNo()
{
	return m_errno;
}


//-----------------------------------------------------------------------------
// イベント設定
//-----------------------------------------------------------------------------
CEvent::RESULT_ENUM CEvent::SetEvent()
{
	uint64_t				event = 1;
	int						iRet = 0;


	// イベントファイルディスクリプタのチェック
	if (m_efd == -1)
	{
		return RESULT_ERROR_EVENT_FD;
	}

	// イベント設定
	iRet = write(m_efd, &event, sizeof(event));
	if (iRet != sizeof(event))
	{
		m_errno = errno;
#ifdef _CEVENT_DEBUG_
		perror("CEvent - write");
#endif	// #ifdef _CEVENT_DEBUG_
		return RESULT_ERROR_EVENT_SET;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// イベントリセット
//-----------------------------------------------------------------------------
CEvent::RESULT_ENUM CEvent::ResetEvent()
{
	uint64_t				event = 0;
	int						iRet = 0;


	// イベントファイルディスクリプタのチェック
	if (m_efd == -1)
	{
		return RESULT_ERROR_EVENT_FD;
	}

	// イベントリセット
	iRet = read(m_efd, &event, sizeof(event));
	if (iRet < 0)
	{
		m_errno = errno;
#ifdef _CEVENT_DEBUG_
		perror("CEvent - read");
#endif	// #ifdef _CEVENT_DEBUG_
		return RESULT_ERROR_EVENT_RESET;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// イベントリセット
//-----------------------------------------------------------------------------
CEvent::RESULT_ENUM CEvent::Wait(DWORD dwTimeout)
{
	fd_set			fdRead;
	int				iRet = 0;
	timeval			timeout;


	// イベントファイルディスクリプタのチェック
	if (m_efd == -1)
	{
		return RESULT_ERROR_EVENT_FD;
	}

	// ウエイト処理
	FD_ZERO(&fdRead);
	FD_SET(m_efd, &fdRead);
	if (dwTimeout == 0)
	{
		iRet = select(m_efd + 1, &fdRead, NULL, NULL, NULL);
	}
	else
	{
		timeout.tv_sec = dwTimeout / 1000;
		timeout.tv_usec = (dwTimeout % 1000) * 1000;
		iRet = select(m_efd + 1, &fdRead, NULL, NULL, &timeout);
	}

	if (iRet < 0)					// エラー
	{
		m_errno = errno;
		return RESULT_ERROR_EVENT_WAIT;
	}
	else if (iRet == 0)				// タイムアウト
	{
		return RESULT_WAIT_TIMEOUT;
	}

	return RESULT_RECIVE_EVENT;
}