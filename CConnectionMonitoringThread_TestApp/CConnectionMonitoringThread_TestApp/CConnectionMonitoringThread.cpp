//*****************************************************************************
// クライアント接続監視スレッド
//*****************************************************************************
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "CConnectionMonitoringThread.h"


#define _CONNECTION_MONITORING_THREAD_DEBUG_
#define CLIENT_CONNECT_NUM							( 5 )						// クライアント接続可能数
#define EPOLL_MAX_EVENTS							( 10 )						// epoll最大イベント

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::CConnectionMonitoringThread()
{
	CEvent::RESULT_ENUM				eRet = CEvent::RESULT_SUCCESS;


	m_bInitFlag = false;
	m_ErrorNo = 0;
	memset(&m_tServerInfo, 0x00, sizeof(m_tServerInfo));
	m_tServerInfo.Socket = -1;
	m_epfd = -1;
	m_ClientResponseThreadList.clear();

	// クライアント応答スレッド終了イベント
	eRet = m_cClientResponseThread_EndEvent.Init();
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
CConnectionMonitoringThread::~CConnectionMonitoringThread()
{
	// クライアント接続監視スレッド停止漏れを考慮
	this->Stop();
}


//-----------------------------------------------------------------------------
// クライアント接続監視スレッド開始
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Start()
{
	bool						bRet = false;
	RESULT_ENUM					eRet = RESULT_SUCCESS;
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

	// サーバー接続初期化処理
	eRet = ServerConnectInit(m_tServerInfo);
	if (eRet != RESULT_SUCCESS)
	{
		return eRet;
	}

	// クライアント接続監視スレッド開始
	eThreadRet = CThread::Start();
	if (eThreadRet != CThread::RESULT_SUCCESS)
	{
		m_ErrorNo = CThread::GetErrorNo();
		return (CConnectionMonitoringThread::RESULT_ENUM)eThreadRet;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// クライアント接続監視スレッド停止
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Stop()
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

	// リストに登録されている、クライアント応答スレッドを全て解放する
	ClientResponseThreadList_Clear();

	// クライアント接続監視スレッド停止
	CThread::Stop();

	// サーバー側のソケットを解放
	if (m_tServerInfo.Socket != -1)
	{
		close(m_tServerInfo.Socket);
		memset(&m_tServerInfo, 0x00, sizeof(m_tServerInfo));
		m_tServerInfo.Socket = -1;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// サーバー接続初期化
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::ServerConnectInit(SERVER_INFO_TABLE& tServerInfo)
{
	int					iRet = 0;


	// サーバー側のソケットを生成
	tServerInfo.Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tServerInfo.Socket == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - socket");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return RESULT_ERROR_CREATE_SOCKET;
	}

	// closeしたら直ぐにソケットを解放するようにする（bindで「Address already in use」となるのを回避する）
	const int one = 1;
	setsockopt(tServerInfo.Socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));


	// サーバー側のIPアドレス・ポートを設定
	tServerInfo.tAddr.sin_family = AF_INET;
	tServerInfo.tAddr.sin_port = htons(12345);
	tServerInfo.tAddr.sin_addr.s_addr = INADDR_ANY;
	iRet = bind(tServerInfo.Socket, (struct sockaddr*) & tServerInfo.tAddr, sizeof(tServerInfo.tAddr));
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - bind");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		close(tServerInfo.Socket);
		tServerInfo.Socket = -1;
		return RESULT_ERROR_BIND;
	}

	// クライアント側からの接続を待つ
	iRet = listen(tServerInfo.Socket, CLIENT_CONNECT_NUM);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - listen");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		close(tServerInfo.Socket);
		tServerInfo.Socket = -1;
		return RESULT_ERROR_LISTEN;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// クライアント接続監視スレッド停止
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ThreadProc()
{
	int							iRet = 0;
	struct epoll_event			tEvent;
	struct epoll_event			tEvents[ EPOLL_MAX_EVENTS ];
	bool						bLoop = true;


	// スレッドが終了する際に呼ばれる関数を登録
	pthread_cleanup_push(ThreadProcCleanup, this);

	// epollファイルディスクリプタ生成
	m_epfd = epoll_create(EPOLL_MAX_EVENTS);
	if (m_epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - epoll_create");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
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
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - epoll_ctl[ThreadEndReqEvent]");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return;
	}

	// クライアント応答スレッド終了イベントを登録
	memset(&tEvent, 0x00, sizeof(tEvent));
	tEvent.events = EPOLLIN;
	tEvent.data.fd = this->m_cClientResponseThread_EndEvent.GetEdf();
	iRet = epoll_ctl(m_epfd, EPOLL_CTL_ADD, this->m_cClientResponseThread_EndEvent.GetEdf(), &tEvent);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - epoll_ctl[Server Socket]");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return;
	}

	// 接続要求
	memset(&tEvent, 0x00, sizeof(tEvent));
	tEvent.events = EPOLLIN;
	tEvent.data.fd = this->m_tServerInfo.Socket;
	iRet = epoll_ctl(m_epfd, EPOLL_CTL_ADD, this->m_tServerInfo.Socket, &tEvent);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - epoll_ctl[Server Socket]");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return;
	}

	// スレッド開始イベントを送信
	this->m_cThreadStartEvent.SetEvent();

	// スレッド終了要求が来るまでループ
	while (bLoop) {
		memset(tEvents, 0x00, sizeof(tEvents));
		int nfds = epoll_wait(this->m_epfd, tEvents, EPOLL_MAX_EVENTS, -1);
		if (nfds == -1)
		{
			m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
			perror("CConnectionMonitoringThread - epoll_wait");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
			continue;
		}
		else if (nfds == 0)
		{
			continue;
		}

		for (int i = 0; i < nfds; i++ )
		{
			// スレッド終了要求イベント受信
			if (tEvents[i].data.fd == this->GetEdfThreadEndReqEvent())
			{
				bLoop = false;
				continue;
			}

			// 接続要求
			if (tEvents[i].data.fd == this->m_tServerInfo.Socket)
			{
				CClientResponseThread::CLIENT_INFO_TABLE		tClentInfo;
				socklen_t len = sizeof(tClentInfo.tAddr);
				tClentInfo.Socket = accept(this->m_tServerInfo.Socket, (struct sockaddr*)&tClentInfo.tAddr, &len);

				CClientResponseThread* pcClientResponseThread = (CClientResponseThread*)new CClientResponseThread(tClentInfo, &m_cClientResponseThread_EndEvent);
				if (pcClientResponseThread == NULL)
				{
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
					printf("CConnectionMonitoringThread - crete CClientResponseThread error.\n");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
					continue;
				}
				CClientResponseThread::RESULT_ENUM eRet = pcClientResponseThread->Start();
				if (eRet != CClientResponseThread::RESULT_SUCCESS)
				{
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
					printf("CConnectionMonitoringThread - start CClientResponseThread error.\n");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
					continue;
				}

				// リストに登録
				m_ClientResponseThreadList.push_back(pcClientResponseThread);
				printf("accepted connection from %s, port=%d\n", inet_ntoa(tClentInfo.tAddr.sin_addr), ntohs(tClentInfo.tAddr.sin_port));
				continue;
			}

			// クライアント応答スレッド終了イベント
			if (tEvents[i].data.fd == this->GetEdfThreadEndReqEvent())
			{
				// リストに登録されている、クライアント応答スレッドからスレッド終了フラグが立っているスレッドを全て終了させる
				ClientResponseThreadList_CheckEndThread();
			}
		}
	}

	// スレッド終了イベントを送信
	this->m_cThreadEndEvent.SetEvent();

	pthread_cleanup_pop(1);
}


//-----------------------------------------------------------------------------
// クライアント接続監視スレッド終了時に呼ばれる処理
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ThreadProcCleanup(void* pArg)
{
	CConnectionMonitoringThread* pcConnectionMonitorThread = (CConnectionMonitoringThread*)pArg;


	// epollファイルディスクリプタ解放
	if (pcConnectionMonitorThread->m_epfd != -1)
	{
		close(pcConnectionMonitorThread->m_epfd);
		pcConnectionMonitorThread->m_epfd = -1;
	}
}


//-----------------------------------------------------------------------------
// リストに登録されている、クライアント応答スレッドを全て解放する
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ClientResponseThreadList_Clear()
{
	std::list< CClientResponseThread*>::iterator it = m_ClientResponseThreadList.begin();
	while (it != m_ClientResponseThreadList.end())
	{
		CClientResponseThread* p = *it;
		delete p;
		it++;
	}
	m_ClientResponseThreadList.clear();
}


//-----------------------------------------------------------------------------
// リストに登録されている、クライアント応答スレッドからスレッド終了フラグが立
// っているスレッドを全て終了させる
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ClientResponseThreadList_CheckEndThread()
{
	std::list< CClientResponseThread*>::iterator it = m_ClientResponseThreadList.begin();
	while (it != m_ClientResponseThreadList.end())
	{
		CClientResponseThread* p = *it;

		// スレッド終了要求フラグが立っている？
		if (p->IsThreadEndRequest() == true)
		{
			delete p;
			it = m_ClientResponseThreadList.erase(it);
			continue;
		}

		it++;
	}
}
