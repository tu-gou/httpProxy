#define _CRT_SECURE_NO_WARNINGS
//#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <fstream>
#include<string>
#include<iostream>
using namespace std;

#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //�������ݱ��ĵ���󳤶�
#define HTTP_PORT 80 //http �������˿�

//Http ��Ҫͷ������
struct HttpHeader {
	char method[4]; // POST ���� GET��ע����ЩΪ CONNECT����ʵ���ݲ�����
	char url[1024];  // ����� url
	char host[1024]; // Ŀ������
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};


//�û�����
char ForbiddenIP[1024][17];
int IPnum = 0;

//������վ
char fishUrl[1024][1024];
int fishUrlnum = 0;

BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
int ParseCacheHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);


BOOL ParseDate(char* buffer, char* field, char* tempDate);
void makeNewHTTP(char* buffer, char* value);
void makeFilename(char* url, char* filename);
void makeCache(char* buffer, char* url);
void getCache(char* buffer, char* filename);
//������ز���
BOOL haveCache = FALSE;
BOOL needCache = TRUE;
char* strArr[100];

bool ForbiddenToConnect(char* httpheader);
bool GotoFalseWebsite(char* url);
void ParseCache(char* buffer, char* status, char* last_modified);
bool UserIsForbidden(char* userID);  //�û�����
//������ز���
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;
//�����µ����Ӷ�ʹ�����߳̽��д������̵߳�Ƶ���Ĵ����������ر��˷���Դ
//����ʹ���̳߳ؼ�����߷�����Ч��
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};
int _tmain(int argc, _TCHAR* argv[])
{
	printf("�����������������\n");
	printf("��ʼ��...\n");
	if (!InitSocket()) {
		printf("socket ��ʼ��ʧ��\n");
		return -1;
	}
	printf("����������������У������˿� %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	SOCKADDR_IN acceptAddr;
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	//������������ϼ���
	char client_IP[16];
	//���ý���IP
	memcpy(ForbiddenIP[IPnum++], "127.0.0.1", 10);

	//���÷�����Щ��վ�ᱻ�ض��򵽵�����վ
	memcpy(fishUrl[fishUrlnum++], "http://pku.edu.cn/", 18);

	while (true) {

		acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAddr, NULL);//��ʱIP��ַΪ204.204.204.204

		
		//�����û�IP����
		/*int ff = sizeof(acceptAddr);
		acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAddr, &(ff));
		printf("��ȡ�û�IP��ַ��%s\n",inet_ntoa(acceptAddr.sin_addr));
		memcpy(client_IP, inet_ntoa(acceptAddr.sin_addr), 16);
		if (UserIsForbidden(client_IP))
		{
			printf("***********��IP�ѱ�����***************\n");
			closesocket(acceptSocket);
			exit(0);
		}*/


		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0,
			&ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(500);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}
//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: ��ʼ���׽���
//************************************
BOOL InitSocket() {
	//�����׽��ֿ⣨���룩
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("���� winsock ʧ�ܣ� �������Ϊ: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("�����ҵ���ȷ�� winsock �汾\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("�����׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("���׽���ʧ��\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("�����˿�%d ʧ��", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: �߳�ִ�к���
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE], fileBuffer[MAXSIZE];//�ļ�д��
	ZeroMemory(Buffer, MAXSIZE);
	char sendBuffer[MAXSIZE];
	ZeroMemory(sendBuffer, MAXSIZE);
	char FishBuffer[MAXSIZE];//������վ�Ļ���
	ZeroMemory(FishBuffer, MAXSIZE);
	char* CacheBuffer;
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;

	//���տͻ��˵�����
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	
	//printf("�ͻ��������� buffer \n %s\n", Buffer);
	memcpy(sendBuffer, Buffer, recvSize);
	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	ParseHttpHead(CacheBuffer, httpHeader);


	//����
	char* DateBuffer;
	DateBuffer = (char*)malloc(MAXSIZE);
	ZeroMemory(DateBuffer, strlen(Buffer) + 1);
	memcpy(DateBuffer, Buffer, strlen(Buffer) + 1);
	//printf("DateBuffer: \n%s\n", DateBuffer);
	char filename[100];
	ZeroMemory(filename, 100);
	makeFilename(httpHeader->url, filename);
	//printf("filename : %s\n", filename);
	char* field = (char*)"Date";
	char date_str[30];  //�����ֶ�Date��ֵ
	ZeroMemory(date_str, 30);
	ZeroMemory(fileBuffer, MAXSIZE);
	FILE* in;//��ȡ���ػ���
	if ((in = fopen(filename, "rb")) != NULL) {
		printf("\n**********��ȡ���ػ���****************\n");
		fread(fileBuffer, sizeof(char), MAXSIZE, in);
		fclose(in);
		ParseDate(fileBuffer, field, date_str);
		printf("date_str:%s\n", date_str);
		makeNewHTTP(Buffer, date_str);
		haveCache = TRUE;
		goto success;
	}
	delete CacheBuffer;

	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		goto error;
	}
	printf("������������ %s �ɹ�\n", httpHeader->host);


	//������վ��Ϣ
	if (ForbiddenToConnect(httpHeader->url))
	{
		printf("*************��������� %s *******************\n",httpHeader->url);
		goto error;

	}


	//��վ����  ����http://pku.edu.cn/  �ض��� http://today.hit.edu.cn/
	if (GotoFalseWebsite(httpHeader->url))
	{

		char* pr;
		int fishing_len = 0;//ʹ��fishing_len����¼�Ѷ�ȡ���ĵĳ��ȣ��Է���������޸ĺ��汨��
		fishing_len = strlen("HTTP/1.1 302 Moved Temporarily\r\n");
		memcpy(FishBuffer, "HTTP/1.1 302 Moved Temporarily\r\n", fishing_len);
		pr = FishBuffer + fishing_len;
		fishing_len = strlen("Connection:keep-alive\r\n");
		memcpy(pr, "Connection:keep-alive\r\n", fishing_len);
		pr = pr + fishing_len;
		fishing_len = strlen("Cache-Control:max-age=0\r\n");
		memcpy(pr, "Cache-Control:max-age=0\r\n", fishing_len);
		pr = pr + fishing_len;
		//�ض��򵽽��չ�����
		fishing_len = strlen("Location: http://today.hit.edu.cn/\r\n\r\n");
		memcpy(pr, "Location: http://today.hit.edu.cn/\r\n\r\n", fishing_len);
		//��302���ķ��ظ��ͻ���
		ret = send(((ProxyParam*)lpParameter)->clientSocket, FishBuffer, sizeof(FishBuffer), 0);
		goto error;
	}
	if (recvSize <= 0) {
		goto error;
	}

success://�л���ֱ�Ӷ�ȡ���͸��ͻ���
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("����Ŀ�������ʧ�ܣ�����\n");
		goto error;
	}
	printf("������������ %s �ɹ�\n", httpHeader->host);
	//���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//�ȴ�Ŀ���������������
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		printf("����Ŀ�������������ʧ�ܣ�����\n");
		goto error;
	}
	//�л���ʱ���жϷ��ص�״̬���Ƿ���304�������򽫻�������ݷ��͸��ͻ���
	if (haveCache == TRUE) {
		getCache(Buffer, filename);
	}
	if (needCache == TRUE) {
		makeCache(Buffer, httpHeader->url);  //���汨��
	}
	//��Ŀ����������ص�����ֱ��ת�����ͻ���
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);

	
	//������
error:
	printf("�ر��׽���\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
	return 0;

}
//************************************
//Method : ForbiddenToConnect
//FullName: ForbiddenToConnect
//Access: public
//Return : bool
//Qualifier:ʵ����վ���ˣ����������ĳЩ��վ
//Parameter: char *httpheader
//************************************
bool ForbiddenToConnect(char* httpheader)
{
	char* forbiddernUrl =(char*) "http://pku.edu.cn/";
	if (!strcmp(httpheader, forbiddernUrl))
	{
		return true;
	}
	else
		return false;
}

//************************************
//Method : UserIsForbidden
//FullName: UserIsForbidden
//Access: public
//Return : bool
//Qualifier:ʵ���û����ˣ�����IP
//Parameter: char *userID
//************************************
bool UserIsForbidden(char* userID)
{
	for (int i = 0; i < IPnum; i++)
	{
		if (strcmp(userID, ForbiddenIP[i]) == 0)
		{
			//�û�IP�ڽ���IP����
			return true;
		}
	}
	return false;
}
//************************************
//Method : GotoFalseWebsite
//FullName: GotoFalseWebsite
//Access: public
//Return : bool
//Qualifier:ʵ�ַ���������ģ����վ
//Parameter: char *url
//************************************
bool GotoFalseWebsite(char* url)
{
	cout << url << endl;
	for (int i = 0; i < fishUrlnum; i++)
	{
		if (strcmp(url, fishUrl[i]) == 0)
		{
			return true;
		}
	}
	return false;
}


//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: ���� TCP �����е� HTTP ͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//��ȡ��һ��   ����ptr��û���˵�һ��
									  //printf("��ȡ����p = %s\n", p);
	if (p[0] == 'G') {//GET ��ʽ
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST ��ʽ
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: ������������Ŀ��������׽��֣�������
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) {
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr))
		== SOCKET_ERROR) {
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}


//����HTTPͷ����field�ֶΣ����������field�򷵻�true������ȡ����
BOOL ParseDate(char* buffer, char* field, char* tempDate) {
	char* p, * ptr, temp[5];
	//*field = "If-Modified-Since";
	const char* delim = "\r\n";
	ZeroMemory(temp, 5);
	p = strtok(buffer, delim);
	//printf("%s\n", p);
	int len = strlen(field) + 2;
	while (p) {
		if (strstr(p, field) != NULL) {
			memcpy(tempDate, &p[len], strlen(p) - len);
			//printf("tempDate: %s\n", tempDate);
			return TRUE;
		}
		p = strtok(NULL, delim);
	}
	return TRUE;
}

//����HTTP������
void makeNewHTTP(char* buffer, char* value) {
	const char* field = "Host";
	const char* newfield = "If-Modified-Since: ";
	//const char *delim = "\r\n";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	char* pos = strstr(buffer, field);
	int i = 0;
	for (i = 0; i < strlen(pos); i++) {
		temp[i] = pos[i];
	}
	*pos = '\0';
	while (*newfield != '\0') {  //����If-Modified-Since�ֶ�
		*pos++ = *newfield++;
	}
	while (*value != '\0') {
		*pos++ = *value++;
	}
	*pos++ = '\r';
	*pos++ = '\n';
	for (i = 0; i < strlen(temp); i++) {
		*pos++ = temp[i];
	}
}

//����url�����ļ���
void makeFilename(char* url, char* filename) {
	while (*url != '\0') {
		if (*url != '/' && *url != ':' && *url != '.') {
			*filename++ = *url;
		}
		url++;
	}
	strcat(filename, ".txt");
}

//���л���
void makeCache(char* buffer, char* url) {
	char* p, * ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char* delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//��ȡ��һ��
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //״̬����200ʱ����
		//printf("url : %s\n", url);
		char filename[1024] = { 0 };  // �����ļ���
		makeFilename(url, filename);
		printf("filename : %s\n", filename);
		ofstream of;
		of.open(filename, ios::out);
		of << buffer << endl;
		of.close();
		printf("\n=====================================\n\n");
		printf("\n***********��ҳ�Ѿ�������**********\n");
	}
}

//��ȡ����
void getCache(char* buffer, char* filename) {
	char* p, * ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char* delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//��ȡ��һ��
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //�������صı����е�״̬��Ϊ304ʱ�����ѻ��������
		printf("\n=====================================\n\n");
		printf("***********�ӱ�����û���**************\n");
		ZeroMemory(buffer, strlen(buffer));
		FILE* in = NULL;
		if ((in = fopen(filename, "r")) != NULL) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		needCache = FALSE;
	}
}
