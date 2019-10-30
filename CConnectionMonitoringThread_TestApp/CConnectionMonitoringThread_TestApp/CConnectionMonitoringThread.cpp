//*****************************************************************************
// �N���C�A���g�ڑ��Ď��X���b�h
//*****************************************************************************
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "CConnectionMonitoringThread.h"



#define _CONNECTION_MONITORING_THREAD_
#define CLIENT_CONNECT_NUM							( 5 )						// �N���C�A���g�ڑ��\��


//-----------------------------------------------------------------------------
// �R���X�g���N�^
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::CConnectionMonitoringThread()
{
	m_bInitFlag = false;
	m_ErrorNo = 0;
	memset(&m_tServerInfo, 0x00, sizeof(m_tServerInfo));
	m_tServerInfo.Socket = -1;



	// ����������
	m_bInitFlag = true;
}


//-----------------------------------------------------------------------------
// �f�X�g���N�^
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::~CConnectionMonitoringThread()
{
	// �N���C�A���g�ڑ��Ď��X���b�h��~�R����l��
	this->Stop(m_tServerInfo);
}


//-----------------------------------------------------------------------------
// �N���C�A���g�ڑ��Ď��X���b�h�J�n
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Start(SERVER_INFO_TABLE& tServerInfo)
{
	bool						bRet = false;
	RESULT_ENUM					eRet = RESULT_SUCCESS;
	CThread::RESULT_ENUM		eThreadRet = CThread::RESULT_SUCCESS;


	// �������������������Ă��Ȃ��ꍇ
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// ���ɃX���b�h�����삵�Ă���ꍇ
	bRet = this->IsActive();
	if (bRet == true)
	{
		return RESULT_ERROR_ALREADY_STARTED;
	}

	// �T�[�o�[�ڑ�����������
	eRet = ServerConnectInit(m_tServerInfo);
	if (eRet != RESULT_SUCCESS)
	{
		return eRet;
	}

	// �N���C�A���g�ڑ��Ď��X���b�h�J�n
	eThreadRet = CThread::Start();
	if (eThreadRet != CThread::RESULT_SUCCESS)
	{
		m_ErrorNo = CThread::GetErrorNo();
		return (CConnectionMonitoringThread::RESULT_ENUM)eThreadRet;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// �N���C�A���g�ڑ��Ď��X���b�h��~
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Stop(SERVER_INFO_TABLE& tServerInfo)
{
	bool						bRet = false;


	// �������������������Ă��Ȃ��ꍇ
	if (m_bInitFlag == false)
	{
		return RESULT_ERROR_INIT;
	}

	// ���ɃX���b�h����~���Ă���ꍇ
	bRet = this->IsActive();
	if (bRet == false)
	{
		return RESULT_SUCCESS;
	}

	// �N���C�A���g�ڑ��Ď��X���b�h��~
	CThread::Stop();

	// �T�[�o�[���̃\�P�b�g�����
	if (tServerInfo.Socket != -1)
	{
		close(tServerInfo.Socket);
		memset(&m_tServerInfo, 0x00, sizeof(m_tServerInfo));
		m_tServerInfo.Socket = -1;
	}

	return RESULT_SUCCESS;
}


//-----------------------------------------------------------------------------
// �T�[�o�[�ڑ�������
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::ServerConnectInit(SERVER_INFO_TABLE& tServerInfo)
{
	int					iRet = 0;


	// �T�[�o�[���̃\�P�b�g�𐶐�
	tServerInfo.Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tServerInfo.Socket != -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - socket");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return RESULT_ERROR_CREATE_SOCKET;
	}

	// �T�[�o�[����IP�A�h���X�E�|�[�g��ݒ�
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

	// �N���C�A���g������̐ڑ���҂�
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
// �N���C�A���g�ڑ��Ď��X���b�h��~
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ThreadProc()
{

	int							epfd = -1;
	struct epoll_event			tEpollEvent;


	// �����ɃX���b�h�I�����ɌĂ΂��֐�������


	// epoll�t�@�C���f�B�X�N���v�^����
	epfd = epoll_create(10);
	if (epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_
		perror("CConnectionMonitoringThread - epoll_create");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_
		return;
	}

	// �X���b�h�I���v���C�x���g��o�^
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

	// �ڑ��v��
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

	// �X���b�h�J�n�C�x���g�𑗐M
	this->m_cThreadStartEvent.SetEvent();







}