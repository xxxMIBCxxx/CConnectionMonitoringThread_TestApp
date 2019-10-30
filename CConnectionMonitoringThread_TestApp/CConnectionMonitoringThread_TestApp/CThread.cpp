//*****************************************************************************
// Threadクラス
// ※リンカオプションに「-pthread」を追加すること
//*****************************************************************************
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "CThread.h"


#define _CTHREAD_DEBUG_
#define	CTHREAD_START_TIMEOUT				( 3 * 1000 )			// スレッド開始待ちタイムアウト(ms)
#define	CTHREAD_END_TIMEOUT					( 3 * 1000 )			// スレッド終了待ちタイムアウト(ms)


//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CThread::CThread(const char* pszId)
{
	int							iRet = 0;
	CEvent::RESULT_ENUM			eRet = CEvent::RESULT_SUCCESS;


	m_strId = "";
	m_bInitFlag = false;
	m_iError = 0;
	m_hThread = 0;
	
	// クラス名を保持
	if (pszId != NULL)
	{
		m_strId = pszId;
	}

	// ミューテック属性の初期化
	pthread_mutexattr_init(&m_tMutexAttr);
	iRet = pthread_mutexattr_settype(&m_tMutexAttr,PTHREAD_PROCESS_PRIVATE);		// スレッド間の排他
	if (iRet != 0)
	{
		return;
	}

	// スレッド開始イベント初期化
	eRet = m_cThreadStartEvent.Init();
	if (eRet != CEvent::RESULT_SUCCESS)
	{
		return;
	}

	// スレッド終了要求イベント
	eRet = m_cThreadEndReqEvent.Init();
	if (eRet != CEvent::RESULT_SUCCESS)
	{
		return;
	}

	// スレッド終了用イベント初期化
	eRet = m_cThreadEndEvent.Init();
	if (eRet != CEvent::RESULT_SUCCESS)
	{
		return;
	}

	// 初期化完了
	m_bInitFlag = true;
}


//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
CThread::~CThread()
{
	// スレッドが停止していないことを考慮
	this->Stop();

	// ミューテックス属性オブジェクトを破棄
	pthread_mutexattr_destroy(&m_tMutexAttr);
}


//-----------------------------------------------------------------------------
// スレッド開始
//-----------------------------------------------------------------------------
CThread::RESULT_ENUM CThread::Start()
{
	int						iRet = 0;
	CEvent::RESULT_ENUM		eEventRet = CEvent::RESULT_SUCCESS;


	// 初期化処理で失敗している場合
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// 既に動作している場合
	if (m_hThread != 0)
	{
		return RESULT_ERROR_ALREADY_STARTED;
	}

	// スレッド開始
	this->m_cThreadStartEvent.ResetEvent();
	this->m_cThreadEndReqEvent.ResetEvent();
	iRet = pthread_create(&m_hThread, NULL, ThreadLauncher, this);
	if (iRet != 0)
	{
		m_iError = errno;
#ifdef _CTHREAD_DEBUG_
		perror("CThread::Start - pthread_create");
#endif	// #ifdef _CTHREAD_DEBUG_
		return RESULT_ERROR_START;
	}

	// スレッド開始イベント待ち
	eEventRet = this->m_cThreadStartEvent.Wait(CTHREAD_START_TIMEOUT);
	switch (eEventRet) {
	case CEvent::RESULT_RECIVE_EVENT:			// スレッド開始イベントを受信
		this->m_cThreadStartEvent.ResetEvent();
		break;

	case CEvent::RESULT_WAIT_TIMEOUT:			// タイムアウト
#ifdef _CTHREAD_DEBUG_
		printf("CThread::Start - WaitTimeout\n");
#endif	// #ifdef _CTHREAD_DEBUG_
		pthread_cancel(m_hThread);
		pthread_join(m_hThread, NULL);
		m_hThread = 0;
		return RESULT_ERROR_START_TIMEOUT;

	default:
#ifdef _CTHREAD_DEBUG_
		printf("CThread::Start - Wait Error. [0x%08X]\n", eEventRet);
#endif	// #ifdef _CTHREAD_DEBUG_
		pthread_cancel(m_hThread);
		pthread_join(m_hThread, NULL);
		m_hThread = 0;
		return RESULT_ERROR_SYSTEM;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// スレッド停止
//-----------------------------------------------------------------------------
CThread::RESULT_ENUM CThread::Stop()
{
	CEvent::RESULT_ENUM			eEventRet = CEvent::RESULT_SUCCESS;


	// 初期化処理で失敗している場合
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// 既に停止している場合
	if (m_hThread == 0)
	{
		return RESULT_SUCCESS;
	}

	// スレッドを停止させる（スレッド終了要求イベントを送信）
	this->m_cThreadEndEvent.ResetEvent();
	eEventRet = this->m_cThreadEndReqEvent.SetEvent();
	if (eEventRet != CEvent::RESULT_SUCCESS)
	{
#ifdef _CTHREAD_DEBUG_
		printf("CThread::Stop - SetEvent Error. [0x%08X]\n", eEventRet);
#endif	// #ifdef _CTHREAD_DEBUG_
		
		// スレッド停止に失敗した場合は、強制的に終了させる
		pthread_cancel(m_hThread);
	}
	else
	{
		// スレッド終了イベント待ち
		eEventRet = this->m_cThreadEndEvent.Wait(CTHREAD_END_TIMEOUT);
		switch (eEventRet) {
		case CEvent::RESULT_RECIVE_EVENT:			// スレッド終了イベントを受信
			this->m_cThreadEndEvent.ResetEvent();
			break;

		case CEvent::RESULT_WAIT_TIMEOUT:			// タイムアウト
#ifdef _CTHREAD_DEBUG_
			printf("CThread::Stop - Timeout\n");
#endif	// #ifdef _CTHREAD_DEBUG_
			pthread_cancel(m_hThread);
			break;

		default:
#ifdef _CTHREAD_DEBUG_
			printf("CThread::Stop - Wait Error. [0x%08X]\n", eEventRet);
#endif	// #ifdef _CTHREAD_DEBUG_
			pthread_cancel(m_hThread);
			break;
		}
	}
	pthread_join(m_hThread, NULL);
	m_hThread = 0;

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// エラー番号を取得
//-----------------------------------------------------------------------------
int CThread::GetErrorNo()
{
	return m_iError;
}


//-----------------------------------------------------------------------------
// スレッドが動作しているかの確認
//-----------------------------------------------------------------------------
bool CThread::IsActive()
{
	return ((m_hThread != 0) ? true : false);
}


//-----------------------------------------------------------------------------
// スレッド開始イベントファイルディスクリプタを取得
//-----------------------------------------------------------------------------
int CThread::GetEdfThreadStartEvent()
{
	return m_cThreadStartEvent.GetEdf();
}


//-----------------------------------------------------------------------------
// スレッド終了要求イベントファイルディスクリプタを取得
//-----------------------------------------------------------------------------
int CThread::GetEdfThreadEndReqEvent()
{
	return m_cThreadEndReqEvent.GetEdf();
}


//-----------------------------------------------------------------------------
// スレッド終了イベントファイルディスクリプタを取得
//-----------------------------------------------------------------------------
int CThread::GetEdfThreadEndEvent()
{
	return m_cThreadEndEvent.GetEdf();
}


//-----------------------------------------------------------------------------
// スレッド呼び出し
//-----------------------------------------------------------------------------
void* CThread::ThreadLauncher(void* pUserData)
{
	// スレッド処理呼び出し
	reinterpret_cast<CThread*>(pUserData)->ThreadProc();

	return (void *)NULL;
}


//-----------------------------------------------------------------------------
// スレッド処理（※サンプル※）
//-----------------------------------------------------------------------------
void CThread::ThreadProc()
{
	CEvent::RESULT_ENUM			eEventRet = CEvent::RESULT_SUCCESS;
	DWORD						dwCount = 1;
	bool						bLoop = true;
	DWORD						dwTimeout = 0;


	// スレッド開始イベントを送信
	m_cThreadStartEvent.SetEvent();
	printf("-- Thread %s Start --\n", m_strId.c_str());

	// pthread_testcancelが呼ばれるまで処理を続ける
	while (bLoop)
	{
		// Sleep時間を生成
		dwTimeout = ((rand() % 30) + 1) * 100;

		// スレッド終了要求イベントが通知されるまで待つ
		eEventRet = m_cThreadEndReqEvent.Wait(dwTimeout);
		switch (eEventRet) {
		case CEvent::RESULT_WAIT_TIMEOUT:
			printf("[%s] Count : %lu \n", this->m_strId.c_str(), dwCount++);
			break;

		case CEvent::RESULT_RECIVE_EVENT:
			m_cThreadEndReqEvent.ResetEvent();
			bLoop = false;
			break;

		default:
			printf("CEvent::Wait error. \n");
			bLoop = false;
			break;
		}
	}

	// スレッド終了イベントを送信
	m_cThreadEndEvent.SetEvent();
	printf("-- Thread %s End --\n", m_strId.c_str());
}
