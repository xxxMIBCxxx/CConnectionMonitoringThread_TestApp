#pragma once
//*****************************************************************************
// クライアント応答スレッドクラス
//*****************************************************************************
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "CThread.h"



#define	IP_ADDR_BUFF_SIZE					( 32 )
#define RECV_BUFF_SIZE						( 1024 )


class CClientResponseThread : public CThread
{
public:

	// クライアント応答スレッドクラスの結果種別
	typedef enum
	{
		RESULT_SUCCESS = 0x00000000,											// 正常終了
		RESULT_ERROR_INIT = 0xE00000001,										// 初期処理に失敗している
		RESULT_ERROR_ALREADY_STARTED = 0xE00000002,								// 既にスレッドを開始している
		RESULT_ERROR_START = 0xE00000003,										// スレッド開始に失敗しました

		RESULT_ERROR_SYSTEM = 0xE9999999,										// システムエラー
	} RESULT_ENUM;

	// クライアント情報構造体
	typedef struct
	{
		int									Socket;								// ソケット
		struct sockaddr_in					tAddr;								// インターネットソケットアドレス構造体

	} CLIENT_INFO_TABLE;



	bool									m_bInitFlag;						// 初期化完了フラグ
	int										m_ErrorNo;							// エラー番号

	CLIENT_INFO_TABLE						m_tClientInfo;						// クライアント情報
	char									m_szIpAddr[IP_ADDR_BUFF_SIZE + 1];	// IPアドレス
	uint16_t								m_Port;								// ポート番号
	
	int										m_epfd;								// epollファイルディスクリプタ（クライアント応答スレッドで使用）
	char									m_szRecvBuf[RECV_BUFF_SIZE + 1];	// 受信バッファ


	bool									m_bThreadEndFlag;					// スレッド終了フラグ
	CEvent									*m_pcClientResponseThread_EndEvent;	// クライアント応答スレッド終了処理イベント


public:
	CClientResponseThread(CLIENT_INFO_TABLE& tClientInfo, CEvent* pcClientResponseThread_EndEvent);
	~CClientResponseThread();
	RESULT_ENUM Start();
	RESULT_ENUM Stop();
	bool IsThreadEndRequest();

public:
	void ThreadProc();
	static void ThreadProcCleanup(void* pArg);

};








