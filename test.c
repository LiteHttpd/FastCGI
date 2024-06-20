/*
 * @filename:    main.c
 * @author:      Tanswer, WuChang
 * @date:        2024年6月20日 21:07:00
 * @description:
 */

#include <stdio.h>
#include <stdlib.h>
#include "fcgi.h"

int main()
{
    FastCgi_t *c;
    c = (FastCgi_t *)malloc(sizeof(FastCgi_t));
    FastCgi_init(c);
    setRequestId(c,1);
    startConnect(c, "127.0.0.1", 9000);
    sendStartRequestRecord(c);
    sendParams(c, "SCRIPT_FILENAME","/home/Tanswer/index.php");
    sendParams(c, "REQUEST_METHOD","GET");
    sendEndRequestRecord(c);
    readResponseData(c, NULL, NULL);
    FastCgi_finit(c);
    return 0;
}
