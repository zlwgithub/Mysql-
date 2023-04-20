#include "pch.h"
#include"CommonConnectionPool.h"
#include "public.h"
using namespace std;
#include<iostream>

ConnectionPool* ConnectionPool::getConnectionPool() {
	static ConnectionPool pool; //lock��unlock
	return &pool;
}

//�������ļ��м���������
bool ConnectionPool::loadConfigFile() {
	FILE* pf = fopen("mysql.ini", "r");
	if (pf == nullptr) {
		LOG("mysqk.ini file is not exist!");
		return false;
	}

	//û��ĩβ
	while (!feof(pf)) {
		char line[1024] = { 0 };
		fgets(line, 1024, pf);
		string str = line;
		int idx = str.find("=", 0);
		if (idx == -1) //��Ч��������
		{
			continue;
		}
		//password=123456\n
		int endidx = str.find('\n', idx);
		string key = str.substr(0, idx);
		string value = str.substr(idx + 1, endidx - idx - 1);

		if (key == "ip") {
			_ip = value;
		}
		else if (key == "port") {
			_port = atoi(value.c_str());
		}
		else if (key == "username") {
			_username = value;
		}
		else if (key == "password") {
			_password = value;
		}
		else if (key == "dbname") {
			_dbname = value;
		}
		else if (key == "initSize") {
			_initSize = atoi(value.c_str());
		}
		else if (key == "maxSize") {
			_maxSize = atoi(value.c_str());
		}
		else if (key == "maxIdleTime") {
			_maxIdleTime = atoi(value.c_str());
		}
		else if (key == "connectionTimeOut") {
			_connectionTimeout = atoi(value.c_str());
		}
	}
	return true;
}

//���ӳصĹ���
ConnectionPool::ConnectionPool() {
	//����������
	if (!loadConfigFile())
	{
		return;
	}

	//������ʼ����������
	for (int i = 0; i < _initSize; ++i)
	{
		Connection* p = new Connection();
		p->connect(_ip, _port, _username, _password, _dbname);
		_connectionQue.push(p);
		p->refreshAliveTime(); //ˢ��һ�¿�ʼ���е���ʼʱ��
		_connectionCnt++;
	}

	//����һ���µ��̣߳���Ϊ����������
	thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
	produce.detach();

	//����һ���µĶ�ʱ�̣߳�ɨ�賬��maxIdleTimeʱ��Ŀ������ӣ����ж�������ӻ���
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}


//�����ڶ������߳��У�ר�Ÿ�������������
void ConnectionPool::produceConnectionTask()
{
	for (;;)
	{
		unique_lock<mutex> lock(_queueMutex);
		while (!_connectionQue.empty())
		{
			cv.wait(lock); //���в��գ���ʱ�����߳̽���ȴ�״̬
		}

		//��������û�дﵽ���ޣ����������µ�����
		if (_connectionCnt < _maxSize)
		{
			Connection* p = new Connection();
			p->connect(_ip, _port, _username, _password, _dbname);
			p->refreshAliveTime();
			_connectionQue.push(p);
			_connectionCnt++;
		}

		//֪ͨ�������̣߳���������������
		cv.notify_all();
	}
}

//ɨ�賬��maxIdleTimeʱ��Ŀ������ӣ����ж�������ӻ���
void ConnectionPool::scannerConnectionTask() {
	for (;;)
	{
		//ͨ��sleepģ�ⶨʱЧ��
		this_thread::sleep_for(chrono::seconds(_maxIdleTime));

		//ɨ���������У��ͷŶ��������
		unique_lock<mutex> lock(_queueMutex);
		while (_connectionCnt > _initSize)
		{
			Connection* p = _connectionQue.front();
			if (p->getAliveTime() >= _maxIdleTime * 1000) {
				_connectionQue.pop();
				_connectionCnt--;
				delete p; //����~Connection()�ͷ�����
			}
			else {
				//��ͷ������û�г���_maxIdleTime,�������ӿ϶�û��
				break;
			}
		}
	}
}

//���ⲿ�ṩ�ӿڣ������ӳ��л�ȡһ�����õĿ������� delete
shared_ptr<Connection> ConnectionPool::getConnection()
{
	unique_lock<mutex> lock(_queueMutex);
	while (_connectionQue.empty()) {
		if (cv_status::timeout == cv.wait_for(lock, chrono::milliseconds(_connectionTimeout)))
		{
			if (_connectionQue.empty()) {
				LOG("��ȡ�������ӳ�ʱ��...��ȡ����ʧ��!");
				return nullptr;
			}
		}
	}

	/*
	shared_ptr����ָ������ʱ�����connection��Դֱ��delete�����൱�ڵ���connection������������connection�ͱ�close����
	������Ҫ�Զ���shared_ptr���ͷ���Դ�ķ�ʽ����connectionֱ�ӹ黹��queue����
	*/
	shared_ptr<Connection> sp(_connectionQue.front(), [&](Connection* pcon) {
		//�������ڷ�����Ӧ���߳��е��õģ�����Ҫ���Ƕ��е��̰߳�ȫ����
		unique_lock<mutex> lock(_queueMutex);
		pcon->refreshAliveTime();
		_connectionQue.push(pcon);
		});

	_connectionQue.pop();
	cv.notify_all(); //�����������Ժ�֪ͨ�������̼߳��һ�£��������Ϊ���ˣ��Ͻ���������

	return sp;
}