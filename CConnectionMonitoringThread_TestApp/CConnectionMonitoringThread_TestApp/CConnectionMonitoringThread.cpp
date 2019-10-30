//*****************************************************************************
// クライアント接続監視スレッド
//*****************************************************************************
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "CConnectionMonitoringThread.h"



#define _CONNECTION_MONITORING_THREAD_
#define CLIENT_CONNECT_NUM							( 5 )						// クライアント接続可能数


//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::CConnectionMonitoringThread()
{
	m_bInitFlag = false;
	m_ErrorNo = 0;
	memset(&m_tServerInfo, 0x00, sizeof(m_tServerInfo));
	m_tServerInfo.Socket = -1;



	// 初期化完了
	m_bInitFlag = true;
}


//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::~CConnectionMonitoringThread()
{
	// クライアント接続監視スレッド停止漏れを考慮
	this->Stop(m_tServerInfo);
}


//-----------------------------------------------------------------------------
// クライアント接続監視スレッド開始
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Start(SERVER_INFO_TABLE& tServerInfo)
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
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Stop(SERVER_INFO_TABLE& tServerInfo)
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

	// サーバー側のソケットを解放
	if (tServerInfo.Socket != -1)
	{
		close(tServerInfo.Socket);
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
	if (tServerInfo.Socket != -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - socket");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return RESULT_ERROR_CREATE_SOCKET;
	}

	// サーバー側のIPアドレス・ポートを設定
	iRet = bind(tServerInfo.Socket, (struct sockaddr*) & tServerInfo.tAddr, sizeof(tServerInfo.tAddr));
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - bind");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		close(tServerInfo.Socket);
		tServerInfo.Socket = -1;
		return RESULT_ERROR_BIND;
	}

	// クライアント側からの接続を待つ
	iRet = listen(tServerInfo.Socket, CLIENT_CONNECT_NUM);
	if (iRet == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - listen");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
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

	int							epfd = -1;
	struct epoll_event			tEpollEvent;


	// ここにスレッド終了時に呼ばれる関数を書く


	// epollファイルディスクリプタ生成
	epfd = epoll_create(10);
	if (epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - epoll_create");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return;
	}

	// スレッド終了要求イベントを登録
	memset(&tEpollEvent, 0x00, sizeof(tEpollEvent));
	tEpollEvent.events = EPOLLIN;
	tEpollEvent.data.fd = this->GetEdfThreadEndReqEvent();
	epoll_ctl(epfd, EPOLL_CTL_ADD, this->GetEdfThreadEndReqEvent(), &tEpollEvent);
	if (epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - epoll_ctl[ThreadEndReqEvent]");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return;
	}

	// 接続要求
	memset(&tEpollEvent, 0x00, sizeof(tEpollEvent));
	tEpollEvent.events = EPOLLIN;
	tEpollEvent.data.fd = this->m_tServerInfo.Socket;
	epoll_ctl(epfd, EPOLL_CTL_ADD, this->m_tServerInfo.Socket, &tEpollEvent);
	if (epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - epoll_ctl[Server Socket]");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return;
	}

	// スレッド開始イベントを送信
	this->m_cThreadStartEvent.SetEvent();







}