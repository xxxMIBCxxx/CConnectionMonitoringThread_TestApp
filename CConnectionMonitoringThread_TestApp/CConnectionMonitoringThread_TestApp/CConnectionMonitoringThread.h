#pragma once
//*****************************************************************************
// クライアント接続監視スレッド
//*****************************************************************************
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "CThread.h"
#include "CClientResponseThread.h"
#include "list"




class CConnectionMonitoringThread : public CThread
{
public:
	// クライアント接続監視クラスの結果種別
	typedef enum
	{
		RESULT_SUCCESS = 0x00000000,										// 正常終了
		RESULT_ERROR_INIT = 0xE00000001,									// 初期処理に失敗している
		RESULT_ERROR_ALREADY_STARTED = 0xE00000002,							// 既にスレッドを開始している
		RESULT_ERROR_START = 0xE00000003,									// スレッド開始に失敗しました

		RESULT_ERROR_PARAM = 0xE1000001,									// パラメータエラー
		RESULT_ERROR_CREATE_SOCKET = 0xE1000002,							// ソケット生成に失敗
		RESULT_ERROR_BIND = 0xE1000003,										// ソケットの名前付けに失敗
		RESULT_ERROR_LISTEN = 0xE1000004,									// 接続待ちに失敗
		RESULT_ERROR_SYSTEM = 0xE9999999,									// システムエラー
	} RESULT_ENUM;


	// サーバー情報構造体
	typedef struct
	{
		int									Socket;							// ソケット
		struct sockaddr_in					tAddr;							// インターネットソケットアドレス構造体
	} SERVER_INFO_TABLE;


	//// クライアント情報構造体
	//typedef struct
	//{
	//	int									Socket;							// ソケット
	//	struct sockaddr_in					tAddr;							// インターネットソケットアドレス構造体
	//} CLIENT_INFO_TABLE;


private:
	bool									m_bInitFlag;					// 初期化完了フラグ
	int										m_ErrorNo;						// エラー番号

	SERVER_INFO_TABLE						m_tServerInfo;					// サーバー情報


	int										m_epfd;							// epollファイルディスクリプタ（クライアント接続監視スレッドで使用）

	CEvent									m_cClientResponseThread_EndEvent;	// クライアント応答スレッド終了処理イベント
	std::list< CClientResponseThread *>		m_ClientResponseThreadList;



public:
	CConnectionMonitoringThread();
	~CConnectionMonitoringThread();
	RESULT_ENUM Start();
	RESULT_ENUM Stop();

private:
	RESULT_ENUM ServerConnectInit(SERVER_INFO_TABLE& tServerInfo);
	void ThreadProc();
	static void ThreadProcCleanup(void* pArg);
	void ClientResponseThreadList_Clear();
	void ClientResponseThreadList_CheckEndThread();
};








