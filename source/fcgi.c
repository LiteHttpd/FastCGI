/*
 * @filename:    fcgi.c
 * @author:      Tanswer, WuChang
 * @date:        2024年6月20日 21:07:00
 * @description:
 */

#include "fastcgi.h"
#include "fcgi.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#if WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include <errno.h>

static int FCGI_Write(int sockfd, const char* data, int size) {
#ifdef WIN32
    return send(sockfd, data, size, 0);
#else
    return write(sockfd, data, size);
#endif
}

static int FCGI_Read(int sockfd, char* data, int size) {
#ifdef WIN32
    return recv(sockfd, data, size, 0);
#else
    return read(sockfd, data, size);
#endif
}

static char *findStartHtml(char *content);
static void getHtmlFromContent(FastCgi_t *c, char *content);

void FastCgi_init(FastCgi_t *c)
{
    c -> sockfd_ = 0;
    c -> flag_ = 0;
    c -> requestId_ = 0;
}

void FastCgi_finit(FastCgi_t *c)
{
#if WIN32
    closesocket(c->sockfd_);
#else
    close(c->sockfd_);
#endif // WIN32
}

void setRequestId(FastCgi_t *c, int requestId)
{
    c -> requestId_ = requestId;
}

FCGI_Header makeHeader(int type, int requestId,
                        int contentLength, int paddingLength)
{
    FCGI_Header header;
    
    header.version = FCGI_VERSION_1;
    
    header.type = (unsigned char)type;

    /* 两个字段保存请求ID */
    header.requestIdB1 = (unsigned char)((requestId >> 8) & 0xff);
    header.requestIdB0 = (unsigned char)(requestId & 0xff);

    /* 两个字段保存内容长度 */
    header.contentLengthB1 = (unsigned char)((contentLength >> 8) & 0xff);
    header.contentLengthB0 = (unsigned char)(contentLength & 0xff);

    /* 填充字节的长度 */
    header.paddingLength = (unsigned char)paddingLength;

    /* 保存字节赋为 0 */
    header.reserved = 0;

    return header;
}

FCGI_BeginRequestBody makeBeginRequestBody(int role, int keepConnection)
{
    FCGI_BeginRequestBody body;

    /* 两个字节保存期望 php-fpm 扮演的角色 */
    body.roleB1 = (unsigned char)((role >> 8) & 0xff);
    body.roleB0 = (unsigned char)(role & 0xff);

    /* 大于0长连接，否则短连接 */
    body.flags = (unsigned char)((keepConnection) ? FCGI_KEEP_CONN : 0);

    memset(&body.reserved, 0, sizeof(body.reserved));

    return body;
}   


int makeNameValueBody(const char *name, int nameLen,
                        const char *value, int valueLen,
                        char *bodyBuffPtr)
{
    char* ptrStart = bodyBuffPtr;

    /* 如果 nameLen 小于128字节 */
    if(nameLen < 128){
        *bodyBuffPtr++ = (unsigned char)nameLen;    //nameLen用1个字节保存
    } else {
        /* nameLen 用 4 个字节保存 */
        *bodyBuffPtr++ = (unsigned char)((nameLen >> 24) | 0x80);
        *bodyBuffPtr++ = (unsigned char)(nameLen >> 16);
        *bodyBuffPtr++ = (unsigned char)(nameLen >> 8);
        *bodyBuffPtr++ = (unsigned char)nameLen;
    }

    /* valueLen 小于 128 就用一个字节保存 */
    if(valueLen < 128){
        *bodyBuffPtr++ = (unsigned char)valueLen;
    } else {
        /* valueLen 用 4 个字节保存 */
        *bodyBuffPtr++ = (unsigned char)((valueLen >> 24) | 0x80);
        *bodyBuffPtr++ = (unsigned char)(valueLen >> 16);
        *bodyBuffPtr++ = (unsigned char)(valueLen >> 8);
        *bodyBuffPtr++ = (unsigned char)valueLen;
    }

    /* 将 name 中的字节逐一加入body中的buffer中 */
    for(int i = 0; i < nameLen; i++){
        *bodyBuffPtr++ = name[i];
    }

    /* 将 value 中的值逐一加入body中的buffer中 */
    for(int i = 0; i < valueLen; i++){
        *bodyBuffPtr++ = value[i];
    }
    
    return (int)((size_t)bodyBuffPtr - (size_t)ptrStart);
}

int getNameValueBodySize(int nameLen, int valueLen)
{
    return
        (nameLen < 128 ? 1 : 4) + nameLen
        + (valueLen < 128 ? 1 : 4) + valueLen;
}

int startConnect(FastCgi_t *c, const char* addr, uint16_t port)
{
    struct sockaddr_in server_address;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd <= 0) {
        return 0;
    }

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(addr);
    server_address.sin_port = htons(port);

    int rc = connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address));
    if (rc < 0) { return 0; }

    c -> sockfd_ = sockfd;
    return sockfd;
}
int sendStartRequestRecord(FastCgi_t *c)
{
    FCGI_BeginRequestRecord beginRecord;

    beginRecord.header = makeHeader(FCGI_BEGIN_REQUEST, c->requestId_, sizeof(beginRecord.body),0);
    beginRecord.body = makeBeginRequestBody(FCGI_RESPONDER, 0);

    int rc = FCGI_Write(c->sockfd_, (char *)&beginRecord, sizeof(beginRecord));

    return rc == sizeof(beginRecord);
}

int sendParams(FastCgi_t *c, const char *name, const char *value)
{
    int nameLen = strlen(name), valueLen = strlen(value);
    int bodyLen = getNameValueBodySize(nameLen, valueLen);

    FCGI_Header nameValueHeader;
    nameValueHeader = makeHeader(FCGI_PARAMS, c->requestId_, bodyLen, 0);

    int nameValueRecordLen = bodyLen + FCGI_HEADER_LEN;
    char* nameValueRecord = malloc(nameValueRecordLen);
    if (!nameValueRecord) { return 0; }

    /* 将头和body拷贝到一块buffer 中只需调用一次write */
    memcpy(nameValueRecord, &nameValueHeader, FCGI_HEADER_LEN);
    
    /* 生成 PARAMS 参数内容的 body */
    makeNameValueBody(name, nameLen, value, valueLen, nameValueRecord + FCGI_HEADER_LEN);

    int rc = FCGI_Write(c->sockfd_, nameValueRecord, nameValueRecordLen);

    free(nameValueRecord);
    return rc == nameValueRecordLen;
}

int sendPostData(FastCgi_t* c, const char* data, int size) {
    FCGI_Header dataHeader;
    dataHeader = makeHeader(FCGI_STDIN, c->requestId_, size, 0);

    int dataRecordLen = size + FCGI_HEADER_LEN;
    char* dataRecord = malloc(dataRecordLen);
    if (!dataRecord) { return 0; }

    /* 将头和body拷贝到一块buffer 中只需调用一次write */
    memcpy(dataRecord, &dataHeader, FCGI_HEADER_LEN);

    /* 拷贝数据 */
    memcpy(dataRecord + FCGI_HEADER_LEN, data, size);

    int rc = FCGI_Write(c->sockfd_, dataRecord, dataRecordLen);

    free(dataRecord);
    return rc == dataRecordLen;
}

int sendEndRequestRecord(FastCgi_t *c)
{
    FCGI_Header endHeader;
    endHeader = makeHeader(FCGI_END_REQUEST, c->requestId_, 0, 0);

    int rc = FCGI_Write(c->sockfd_, (char *)&endHeader, FCGI_HEADER_LEN);

    return rc == FCGI_HEADER_LEN;
}

int readResponseData(FastCgi_t* c, ResponseDataCallback callback, void* arg)
{
    /* 先将头部 8 个字节读出来 */
    FCGI_Header responderHeader;
    while(FCGI_Read(c->sockfd_, (char*)&responderHeader, FCGI_HEADER_LEN) > 0) {
        if(responderHeader.type == FCGI_STDOUT){
            /* 获取内容长度 */
            int contentLen = (responderHeader.contentLengthB1 << 8) + (responderHeader.contentLengthB0);
            char* content = malloc(contentLen);
            memset(content, 0, contentLen);

            /* 读取获取内容 */
            int ret = FCGI_Read(c->sockfd_, content, contentLen);
            //assert(ret == contentLen);

            if (callback) {
                callback(content, contentLen, arg);
            }

            free(content);

            /* 跳过填充部分 */
            for (int i = 0; i < responderHeader.paddingLength; i++) {
                char tmp = 0;
                int ret = FCGI_Read(c->sockfd_, &tmp, 1);
                //assert(ret == 1);
            }
        } //end of type FCGI_STDOUT
        else if(responderHeader.type == FCGI_STDERR){
            int contentLen = (responderHeader.contentLengthB1 << 8) + (responderHeader.contentLengthB0);
            char* content = malloc(contentLen);
            memset(content, 0, contentLen);

            int ret = FCGI_Read(c->sockfd_, content, contentLen);
            //assert(ret == contentLen);

            // Error Output

            free(content);

            /* 跳过填充部分 */
            for (int i = 0; i < responderHeader.paddingLength; i++) {
                char tmp = 0;
                int ret = FCGI_Read(c->sockfd_, &tmp, 1);
                //assert(ret == 1);
            }
        }// end of type FCGI_STDERR 
        else if(responderHeader.type == FCGI_END_REQUEST){
            FCGI_EndRequestBody endRequest;

            int ret = FCGI_Read(c->sockfd_, (char*)&endRequest, sizeof(endRequest));
            //assert(ret == sizeof(endRequest));
        }
    }

    return 1;
}
