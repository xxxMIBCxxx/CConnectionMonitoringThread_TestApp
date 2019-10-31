//*****************************************************************************
// クライアント応答スレッドクラス
//*****************************************************************************
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "CClientResponseThread.h"


#define _CCLIENT_RESPONSE_THREAD_DEBUG_
#define CLIENT_CONNECT_NUM							( 5 )						// クライアント接続可能数
#define EPOLL_MAX_EVENTS							( 10 )						// epoll最大イベント


//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CClientResponseThread::CClientResponseThread(CLIENT_INFO_TABLE& tClientInfo, CEvent* pcClientResponseThread_EndEvent)
{
	m_bInitFlag = false;
	m_ErrorNo = 0;
	memset(&m_tClientInfo, 0x00, sizeof(m_tClientInfo));
	memset(m_szIpAddr, 0x00, sizeof(m_szIpAddr));
	m_Port = 0;
	m_epfd = -1;
	memset(m_szRecvBuf, 0x00, sizeof(m_szRecvBuf));
	m_bThreadEndFlag = false;
	m_pcClientResponseThread_EndEvent = NULL;


	// クライアント応答スレッド終了イベント
	if (pcClientResponseThread_EndEvent == NULL)
	{
		return;
	}
	m_pcClientResponseThread_EndEvent = pcClientResponseThread_EndEvent;

	// クライアント情報を取得
	memcpy(&m_tClientInfo, &tClientInfo, sizeof(CLIENT_INFO_TABLE));
	sprintf(m_szIpAddr, "%s", inet_ntoa(m_tClientInfo.tAddr.sin_addr));			// IPアドレス取得
	m_Port = ntohs(m_tClientInfo.tAddr.sin_port);								// ポート番号取得

	// 初期化完了
	m_bInitFlag = true;
}



//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
CClientResponseThread::~CClientResponseThread()
{
	// クライアント応答スレッド終了漏れを考慮
	this->Stop();

	// クライアント側のソケットを解放
	if (m_tClientInfo.Socket != -1)
	{
		close(m_tClientInfo.Socket);
		m_tClientInfo.Socket = -1;
	}
}



//-----------------------------------------------------------------------------
// クライアント応答スレッド開始
//-----------------------------------------------------------------------------
CClientResponseThread::RESULT_ENUM CClientResponseThread::Start()
{
	bool						bRet = false;
	CThread::RESULT_ENUM		eThreadRet = CThread::RESULT_SUCCESS;


	// 初期化処理が完了していない場合
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// 既にスレッドが動作している場合
	bRet = this->IsActive();
	if (bRet == true)
	{
		return RESULT_ERROR_ALREADY_STARTED;
	}

	// クライアント接続監視スレッド開始
	eThreadRet = CThread::Start();
	if (eThreadRet != CThread::RESULT_SUCCESS)
	{
		m_ErrorNo = CThread::GetErrorNo();
		return (CClientResponseThread::RESULT_ENUM)eThreadRet;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// クライアント応答スレッド停止
//-----------------------------------------------------------------------------
CClientResponseThread::RESULT_ENUM CClientResponseThread::Stop()
{
	bool						bRet = false;


	// 初期化処理が完了していない場合
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// 既にスレッドが停止している場合
	bRet = this->IsActive();
	if (bRet == false)
	{
		return RESULT_SUCCESS;
	}

	// クライアント接続監視スレッド停止
	CThread::Stop();

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// クライアント応答スレッド
//-----------------------------------------------------------------------------
void CClientResponseThread::ThreadProc()
{
	int							iRet = 0;
	struct epoll_event			tEvent;
	struct epoll_event			tEvents[EPOLL_MAX_EVENTS];
	bool						bLoop = true;
	ssize_t						ReadNum = 0;


	// スレッドが終了する際に呼ばれる関数を登録
	pthread_cleanup_push(ThreadProcCleanup, this);

	// epollファイルディスクリプタ生成
	m_epfd = epoll_create(EPOLL_MAX_EVENTS);
	if (m_epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		perror("CClientResponseThread - epoll_create");
#endif	// #ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		return;
	}

	// スレッド終了要求イベントを登録
	memset(&tEvent, 0x00, sizeof(tEvent));
	tEvent.events = EPOLLIN;
	tEvent.data.fd = this->GetEdfThreadEndReqEvent();
	iRet = epoll_ctl(m_epfd, EPOLL_CTL_ADD, this->GetEdfThreadEndReqEvent(), &tEvent);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		perror("CClientResponseThread - epoll_ctl[ThreadEndReqEvent]");
#endif	// #ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		return;
	}

	// クライアント側のソケットを登録
	memset(&tEvent, 0x00, sizeof(tEvent));
	tEvent.events = EPOLLIN;
	tEvent.data.fd = this->m_tClientInfo.Socket;
	iRet = epoll_ctl(m_epfd, EPOLL_CTL_ADD, this->m_tClientInfo.Socket, &tEvent);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		perror("CClientResponseThread - epoll_ctl[Client Socket]");
#endif	// #ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
		return;
	}

	// スレッド開始イベントを送信
	this->m_cThreadStartEvent.SetEvent();


	// ▼--------------------------------------------------------------------------▼
	// スレッド終了要求が来るまでループ
	// ※勝手にループを抜けるとスレッド終了時にタイムアウトで終了となるため、スレッド終了要求以外は勝手にループを抜けないでください
	while (bLoop) {
		memset(tEvents, 0x00, sizeof(tEvents));
		int nfds = epoll_wait(this->m_epfd, tEvents, EPOLL_MAX_EVENTS, -1);
		if (nfds == -1)
		{
			m_ErrorNo = errno;
#ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
			perror("CClientResponseThread - epoll_wait");
#endif	// #ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
			continue;
		}
		// タイムアウト
		else if (nfds == 0)
		{
			continue;
		}

		for (int i = 0; i < nfds; i++)
		{
			// スレッド終了要求イベント受信
			if (tEvents[i].data.fd == this->GetEdfThreadEndReqEvent())
			{
				bLoop = false;
				continue;
			}

			// クライアント側から送信された情報を取得
			if (tEvents[i].data.fd == this->m_tClientInfo.Socket)
			{
				memset(m_szRecvBuf, 0x00, sizeof(m_szRecvBuf));
				ReadNum = read(this->m_tClientInfo.Socket, m_szRecvBuf, RECV_BUFF_SIZE);
				if (ReadNum == -1)
				{
					m_ErrorNo = errno;
#ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
					perror("CClientResponseThread - read");
#endif	// #ifdef _CCLIENT_RESPONSE_THREAD_DEBUG_
					continue;
				}
				else if (ReadNum == 0)
				{
					// 接続側が切断したため、本スレッドを終了させる（勝手に終了できないため、スレッド終了イベントを送信して終了してもらう）
					this->m_bThreadEndFlag = true;
					this->m_pcClientResponseThread_EndEvent->SetEvent();
					continue;
				}
				printf("[%s (%d)] : %s\n", m_szIpAddr, m_Port, m_szRecvBuf);
			}
		}
	}
	// ▲--------------------------------------------------------------------------▲

	// スレッド終了イベントを送信
	this->m_cThreadEndEvent.SetEvent();

	pthread_cleanup_pop(1);
}


//-----------------------------------------------------------------------------
// クライアント応答スレッド終了時に呼ばれる処理
//-----------------------------------------------------------------------------
void CClientResponseThread::ThreadProcCleanup(void* pArg)
{
	CClientResponseThread* pcClientResponseThread = (CClientResponseThread*)pArg;


	// epollファイルディスクリプタ解放
	if (pcClientResponseThread->m_epfd != -1)
	{
		close(pcClientResponseThread->m_epfd);
		pcClientResponseThread->m_epfd = -1;
	}
}


//-----------------------------------------------------------------------------
// クライアント応答スレッド終了要求なのか調べる
//-----------------------------------------------------------------------------
bool CClientResponseThread::IsThreadEndRequest()
{
	return m_bThreadEndFlag;
}