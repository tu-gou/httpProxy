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
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

//Http 重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024];  // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};


//用户过滤
char ForbiddenIP[1024][17];
int IPnum = 0;

//钓鱼网站
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
//缓存相关参数
BOOL haveCache = FALSE;
BOOL needCache = TRUE;
char* strArr[100];

bool ForbiddenToConnect(char* httpheader);
bool GotoFalseWebsite(char* url);
void ParseCache(char* buffer, char* status, char* last_modified);
bool UserIsForbidden(char* userID);  //用户过滤
//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;
//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};
int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	SOCKADDR_IN acceptAddr;
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	//代理服务器不断监听
	char client_IP[16];
	//设置禁用IP
	memcpy(ForbiddenIP[IPnum++], "127.0.0.1", 10);

	//设置访问哪些网站会被重定向到钓鱼网站
	memcpy(fishUrl[fishUrlnum++], "http://pku.edu.cn/", 18);

	while (true) {

		acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAddr, NULL);//此时IP地址为204.204.204.204

		
		//禁用用户IP访问
		/*int ff = sizeof(acceptAddr);
		acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAddr, &(ff));
		printf("获取用户IP地址：%s\n",inet_ntoa(acceptAddr.sin_addr));
		memcpy(client_IP, inet_ntoa(acceptAddr.sin_addr), 16);
		if (UserIsForbidden(client_IP))
		{
			printf("***********此IP已被禁用***************\n");
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
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败， 错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE], fileBuffer[MAXSIZE];//文件写入
	ZeroMemory(Buffer, MAXSIZE);
	char sendBuffer[MAXSIZE];
	ZeroMemory(sendBuffer, MAXSIZE);
	char FishBuffer[MAXSIZE];//钓鱼网站的缓存
	ZeroMemory(FishBuffer, MAXSIZE);
	char* CacheBuffer;
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;

	//接收客户端的请求
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	
	//printf("客户端请求报文 buffer \n %s\n", Buffer);
	memcpy(sendBuffer, Buffer, recvSize);
	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	ParseHttpHead(CacheBuffer, httpHeader);


	//缓存
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
	char date_str[30];  //保存字段Date的值
	ZeroMemory(date_str, 30);
	ZeroMemory(fileBuffer, MAXSIZE);
	FILE* in;//读取本地缓存
	if ((in = fopen(filename, "rb")) != NULL) {
		printf("\n**********读取本地缓存****************\n");
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
	printf("代理连接主机 %s 成功\n", httpHeader->host);


	//屏蔽网站信息
	if (ForbiddenToConnect(httpHeader->url))
	{
		printf("*************不允许访问 %s *******************\n",httpHeader->url);
		goto error;

	}


	//网站引导  访问http://pku.edu.cn/  重定向到 http://today.hit.edu.cn/
	if (GotoFalseWebsite(httpHeader->url))
	{

		char* pr;
		int fishing_len = 0;//使用fishing_len来记录已读取报文的长度，以方便接下来修改后面报文
		fishing_len = strlen("HTTP/1.1 302 Moved Temporarily\r\n");
		memcpy(FishBuffer, "HTTP/1.1 302 Moved Temporarily\r\n", fishing_len);
		pr = FishBuffer + fishing_len;
		fishing_len = strlen("Connection:keep-alive\r\n");
		memcpy(pr, "Connection:keep-alive\r\n", fishing_len);
		pr = pr + fishing_len;
		fishing_len = strlen("Cache-Control:max-age=0\r\n");
		memcpy(pr, "Cache-Control:max-age=0\r\n", fishing_len);
		pr = pr + fishing_len;
		//重定向到今日哈工大
		fishing_len = strlen("Location: http://today.hit.edu.cn/\r\n\r\n");
		memcpy(pr, "Location: http://today.hit.edu.cn/\r\n\r\n", fishing_len);
		//将302报文返回给客户端
		ret = send(((ProxyParam*)lpParameter)->clientSocket, FishBuffer, sizeof(FishBuffer), 0);
		goto error;
	}
	if (recvSize <= 0) {
		goto error;
	}

success://有缓存直接读取后发送给客户端
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("连接目标服务器失败！！！\n");
		goto error;
	}
	printf("代理连接主机 %s 成功\n", httpHeader->host);
	//将客户端发送的 HTTP 数据报文直接转发给目标服务器
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		printf("返回目标服务器的数据失败！！！\n");
		goto error;
	}
	//有缓存时，判断返回的状态码是否是304，若是则将缓存的内容发送给客户端
	if (haveCache == TRUE) {
		getCache(Buffer, filename);
	}
	if (needCache == TRUE) {
		makeCache(Buffer, httpHeader->url);  //缓存报文
	}
	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);

	
	//错误处理
error:
	printf("关闭套接字\n");
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
//Qualifier:实现网站过滤，不允许访问某些网站
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
//Qualifier:实现用户过滤，禁用IP
//Parameter: char *userID
//************************************
bool UserIsForbidden(char* userID)
{
	for (int i = 0; i < IPnum; i++)
	{
		if (strcmp(userID, ForbiddenIP[i]) == 0)
		{
			//用户IP在禁用IP表中
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
//Qualifier:实现访问引导到模拟网站
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
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行   而且ptr内没有了第一行
									  //printf("提取到的p = %s\n", p);
	if (p[0] == 'G') {//GET 方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
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
// Qualifier: 根据主机创建目标服务器套接字，并连接
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


//分析HTTP头部的field字段，如果包含该field则返回true，并获取日期
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

//改造HTTP请求报文
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
	while (*newfield != '\0') {  //插入If-Modified-Since字段
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

//根据url构造文件名
void makeFilename(char* url, char* filename) {
	while (*url != '\0') {
		if (*url != '/' && *url != ':' && *url != '.') {
			*filename++ = *url;
		}
		url++;
	}
	strcat(filename, ".txt");
}

//进行缓存
void makeCache(char* buffer, char* url) {
	char* p, * ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char* delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //状态码是200时缓存
		//printf("url : %s\n", url);
		char filename[1024] = { 0 };  // 构造文件名
		makeFilename(url, filename);
		printf("filename : %s\n", filename);
		ofstream of;
		of.open(filename, ios::out);
		of << buffer << endl;
		of.close();
		printf("\n=====================================\n\n");
		printf("\n***********网页已经被缓存**********\n");
	}
}

//获取缓存
void getCache(char* buffer, char* filename) {
	char* p, * ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char* delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //主机返回的报文中的状态码为304时返回已缓存的内容
		printf("\n=====================================\n\n");
		printf("***********从本机获得缓存**************\n");
		ZeroMemory(buffer, strlen(buffer));
		FILE* in = NULL;
		if ((in = fopen(filename, "r")) != NULL) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		needCache = FALSE;
	}
}
