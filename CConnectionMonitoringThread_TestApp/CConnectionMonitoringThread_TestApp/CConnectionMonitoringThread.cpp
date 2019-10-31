//*****************************************************************************
// �N���C�A���g�ڑ��Ď��X���b�h
//*****************************************************************************
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "CConnectionMonitoringThread.h"


#define _CONNECTION_MONITORING_THREAD_DEBUG_
#define CLIENT_CONNECT_NUM							( 5 )						// �N���C�A���g�ڑ��\��
#define EPOLL_MAX_EVENTS							( 10 )						// epoll�ő�C�x���g

//-----------------------------------------------------------------------------
// �R���X�g���N�^
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

	// �N���C�A���g�����X���b�h�I���C�x���g
	eRet = m_cClientResponseThread_EndEvent.Init();
	if (eRet != CEvent::RESULT_SUCCESS)
	{
		return;
	}

	// ����������
	m_bInitFlag = true;
}


//-----------------------------------------------------------------------------
// �f�X�g���N�^
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::~CConnectionMonitoringThread()
{
	// �N���C�A���g�ڑ��Ď��X���b�h��~�R����l��
	this->Stop();
}


//-----------------------------------------------------------------------------
// �N���C�A���g�ڑ��Ď��X���b�h�J�n
//-----------------------------------------------------------------------------
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Start()
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
CConnectionMonitoringThread::RESULT_ENUM CConnectionMonitoringThread::Stop()
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

	// ���X�g�ɓo�^����Ă���A�N���C�A���g�����X���b�h��S�ĉ������
	ClientResponseThreadList_Clear();

	// �N���C�A���g�ڑ��Ď��X���b�h��~
	CThread::Stop();

	// �T�[�o�[���̃\�P�b�g�����
	if (m_tServerInfo.Socket != -1)
	{
		close(m_tServerInfo.Socket);
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
	if (tServerInfo.Socket == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - socket");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return RESULT_ERROR_CREATE_SOCKET;
	}

	// close�����璼���Ƀ\�P�b�g���������悤�ɂ���ibind�ŁuAddress already in use�v�ƂȂ�̂��������j
	const int one = 1;
	setsockopt(tServerInfo.Socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));


	// �T�[�o�[����IP�A�h���X�E�|�[�g��ݒ�
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

	// �N���C�A���g������̐ڑ���҂�
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
// �N���C�A���g�ڑ��Ď��X���b�h��~
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ThreadProc()
{
	int							iRet = 0;
	struct epoll_event			tEvent;
	struct epoll_event			tEvents[ EPOLL_MAX_EVENTS ];
	bool						bLoop = true;


	// �X���b�h���I������ۂɌĂ΂��֐���o�^
	pthread_cleanup_push(ThreadProcCleanup, this);

	// epoll�t�@�C���f�B�X�N���v�^����
	m_epfd = epoll_create(EPOLL_MAX_EVENTS);
	if (m_epfd == -1)
	{
		m_ErrorNo = errno;
#ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		perror("CConnectionMonitoringThread - epoll_create");
#endif	// #ifdef _CONNECTION_MONITORING_THREAD_DEBUG_
		return;
	}

	// �X���b�h�I���v���C�x���g��o�^
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

	// �N���C�A���g�����X���b�h�I���C�x���g��o�^
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

	// �ڑ��v��
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

	// �X���b�h�J�n�C�x���g�𑗐M
	this->m_cThreadStartEvent.SetEvent();

	// �X���b�h�I���v��������܂Ń��[�v
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
			// �X���b�h�I���v���C�x���g��M
			if (tEvents[i].data.fd == this->GetEdfThreadEndReqEvent())
			{
				bLoop = false;
				continue;
			}

			// �ڑ��v��
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

				// ���X�g�ɓo�^
				m_ClientResponseThreadList.push_back(pcClientResponseThread);
				printf("accepted connection from %s, port=%d\n", inet_ntoa(tClentInfo.tAddr.sin_addr), ntohs(tClentInfo.tAddr.sin_port));
				continue;
			}

			// �N���C�A���g�����X���b�h�I���C�x���g
			if (tEvents[i].data.fd == this->GetEdfThreadEndReqEvent())
			{
				// ���X�g�ɓo�^����Ă���A�N���C�A���g�����X���b�h����X���b�h�I���t���O�������Ă���X���b�h��S�ďI��������
				ClientResponseThreadList_CheckEndThread();
			}
		}
	}

	// �X���b�h�I���C�x���g�𑗐M
	this->m_cThreadEndEvent.SetEvent();

	pthread_cleanup_pop(1);
}


//-----------------------------------------------------------------------------
// �N���C�A���g�ڑ��Ď��X���b�h�I�����ɌĂ΂�鏈��
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ThreadProcCleanup(void* pArg)
{
	CConnectionMonitoringThread* pcConnectionMonitorThread = (CConnectionMonitoringThread*)pArg;


	// epoll�t�@�C���f�B�X�N���v�^���
	if (pcConnectionMonitorThread->m_epfd != -1)
	{
		close(pcConnectionMonitorThread->m_epfd);
		pcConnectionMonitorThread->m_epfd = -1;
	}
}


//-----------------------------------------------------------------------------
// ���X�g�ɓo�^����Ă���A�N���C�A���g�����X���b�h��S�ĉ������
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
// ���X�g�ɓo�^����Ă���A�N���C�A���g�����X���b�h����X���b�h�I���t���O����
// ���Ă���X���b�h��S�ďI��������
//-----------------------------------------------------------------------------
void CConnectionMonitoringThread::ClientResponseThreadList_CheckEndThread()
{
	std::list< CClientResponseThread*>::iterator it = m_ClientResponseThreadList.begin();
	while (it != m_ClientResponseThreadList.end())
	{
		CClientResponseThread* p = *it;

		// �X���b�h�I���v���t���O�������Ă���H
		if (p->IsThreadEndRequest() == true)
		{
			delete p;
			it = m_ClientResponseThreadList.erase(it);
			continue;
		}

		it++;
	}
}
