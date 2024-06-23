#ifndef FCGI_H
#define FCGI_H

#include "fastcgi.h"
#include <stdint.h>

typedef struct 
{
    int sockfd_;        //与php-fpm 建立的 sockfd
    int requestId_;     //record 里的请求ID
    int flag_;          //用来标志当前读取内容是否为html内容


}FastCgi_t;

void FastCgi_init(FastCgi_t *c);

void FastCgi_finit(FastCgi_t *c);

//设置请求Id
void setRequestId(FastCgi_t *c, int requestId);

//生成头部
FCGI_Header makeHeader(int type, int request, 
                    int contentLength, int paddingLength);

//生成发起请求的请求体
FCGI_BeginRequestBody makeBeginRequestBody(int role, int keepConnection);

//生成 PARAMS 的 name-value body
int makeNameValueBody(const char *name, int nameLen,
                        const char *value, int valueLen,
                        char *bodyBuffPtr);
int getNameValueBodySize(int nameLen, int valueLen);

//连接php-fpm，如果成功则返回对应的套接字描述符
int startConnect(FastCgi_t *c, const char* addr, uint16_t port);

//发送开始请求记录
int sendStartRequestRecord(FastCgi_t *c);

//向php-fpm发送name-value参数对
int sendParams(FastCgi_t *c, const char *name, const char *value);
int sendEmptyParams(FastCgi_t* c);

//发送post数据
int sendPostData(FastCgi_t* c, const char* data, int size);

//发送结束请求消息
int sendEndRequestRecord(FastCgi_t *c);

//读返回内容
typedef void(*ResponseDataCallback)(const char*, int, void*);
int readResponseData(FastCgi_t *c, ResponseDataCallback callback, void* arg);

#endif
