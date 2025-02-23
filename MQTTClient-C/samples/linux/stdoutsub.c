/*******************************************************************************
 * Copyright (c) 2012, 2016 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *   http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *    Ian Craggs - change delimiter option from char to string
 *    Al Stockdill-Mander - Version using the embedded C client
 *    Ian Craggs - update MQTTClient function names
 *******************************************************************************/

/*
 
 stdout subscriber
 
 compulsory parameters:
 
  topic to subscribe to
 
 defaulted parameters:
 
	--host localhost
	--port 1883
	--qos 2
	--delimiter \n
	--clientid stdout_subscriber
	
	--userid none
	--password none

 for example:

    stdoutsub topic/of/interest --host iot.eclipse.org

*/
#include <stdarg.h> 
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <time.h>
#include "MQTTClient.h"

#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

volatile int toStop = 0;

#define MAX_CONCURRENT_SUBSCRIPTIONS 50

void log_message(const char *level, FILE *stream, const char *format, va_list args)
{
    time_t now;
    time(&now);

    now += 8 * 3600;
    struct tm *tm_info = gmtime(&now);

    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stream, "[%s] [%s]：", level, time_buf);
    vfprintf(stream, format, args);
    fprintf(stream, "\n");
}

void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message("INFO", stdout, format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message("ERROR", stderr, format, args);
    va_end(args);
}

void usage()
{
    printf("\n");
    printf("====================================\n");
    printf("       MQTT 标准输出订阅器         \n");
    printf("====================================\n");
    printf("用法: stdoutsub 主题名称 <命令>\n");
    printf("\n可选命令:\n");
    printf("  --host <主机名>         （默认: bemfa.com，MQTT 服务器地址）\n");
    printf("  --port <端口>           （默认: 9501，MQTT 服务器端口）\n");
    printf("  --qos <服务质量>        （默认: 1，MQTT QoS 等级，可选 0 或 1）\n");
    printf("  --delimiter <分隔符>    （默认: \\n，消息之间的分隔符）\n");
    printf("  --clientid <账户私钥>   （默认: 主机名 + 时间戳）\n");
    printf("  --username <用户名>     （默认: none，无用户名）\n");
    printf("  --password <密码>       （默认: none，无密码）\n");
    printf("  --showtopics <on|off>   （默认: off，是否显示主题名，若主题含通配符则默认开启）\n");
    printf("  --script <脚本路径>     （收到 MQTT 消息时，执行指定脚本）\n");
    printf("\n");
    printf("示例:\n");
    printf("  stdoutsub 主题名 --host bemfa.com --port 9501 --qos 1 --clientid asa48fd88e53d356ab21841a951284d\n");
    printf("\n");
    exit(-1);
}

void cfinish(int sig)
{
    signal(SIGINT, NULL);
    toStop = 1;
}

struct opts_struct
{
    char* clientid;
    int nodelimiter;
    char* delimiter;
    enum QoS qos;
    char* username;
    char* password;
    char* host;
    int port;
    int showtopics;
    char* script;
} opts =
{
    (char*)"stdout-subscriber", 0, (char*)"\n", QOS1, NULL, NULL, (char*)"bemfa.com", 9501, 0
};

void getopts(int argc, char** argv)
{
    int count = 2;

    while (count < argc)
    {
        if (strcmp(argv[count], "--qos") == 0)
        {
            if (++count < argc)
            {
                if (strcmp(argv[count], "0") == 0)
                {
                    opts.qos = QOS0;
                }
                else if (strcmp(argv[count], "1") == 0)
                {
                    opts.qos = QOS1;
                }
                else if (strcmp(argv[count], "2") == 0)
                {
                    opts.qos = QOS2;
                    log_info("警告：MQTT 支持Qos0 Qos1等级，支持retian保留消息，不支持qos2，使用qos2会被强制下线，次数过多可造成账号异常无法使用。");
                }
                else
                {
                    usage();
                }
            }
            else
                usage();
        }
        else if (strcmp(argv[count], "--host") == 0)
        {
            if (++count < argc)
                opts.host = argv[count];
            else
                usage();
        }
        else if (strcmp(argv[count], "--port") == 0)
        {
            if (++count < argc)
                opts.port = atoi(argv[count]);
            else
                usage();
        }
        else if (strcmp(argv[count], "--clientid") == 0)
        {
            if (++count < argc)
                opts.clientid = argv[count];
            else
                usage();
        }
        else if (strcmp(argv[count], "--username") == 0)
        {
            if (++count < argc)
                opts.username = argv[count];
            else
                usage();
        }
        else if (strcmp(argv[count], "--password") == 0)
        {
            if (++count < argc)
                opts.password = argv[count];
            else
                usage();
        }
        else if (strcmp(argv[count], "--delimiter") == 0)
        {
            if (++count < argc)
                opts.delimiter = argv[count];
            else
                opts.nodelimiter = 1;
        }
        else if (strcmp(argv[count], "--showtopics") == 0)
        {
            if (++count < argc)
            {
                if (strcmp(argv[count], "on") == 0)
                {
                    opts.showtopics = 1;
                    log_info("开启消息主题名显示。");
                }
                else if (strcmp(argv[count], "off") == 0)
                {
                    opts.showtopics = 0;
                }
                else
                {
                    usage();
                }
            }
            else
                usage();
        }
        else if (strcmp(argv[count], "--script") == 0)
        {
            if (++count < argc)
                opts.script = argv[count];
            else
                usage();
        }
        count++;
    }
}

int is_command_available(const char *command)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "which %s > /dev/null 2>&1", command);
    return system(buffer) == 0;  
}

void messageArrived(MessageData* md)
{
    MQTTMessage* message = md->message;

    if (opts.showtopics)
        printf("%.*s\t", md->topicName->lenstring.len, md->topicName->lenstring.data);
    if (opts.nodelimiter)
        printf("%.*s", (int)message->payloadlen, (char*)message->payload);
    else
        printf("%.*s%s", (int)message->payloadlen, (char*)message->payload, opts.delimiter);

    if (opts.script)
    {
        char payload_buf[1024];
        int len = (message->payloadlen < 1023) ? message->payloadlen : 1023;
        memcpy(payload_buf, message->payload, len);
        payload_buf[len] = '\0';

        char command[2048];
        const char *shell = NULL;

        if (is_command_available("sh"))
        {
            shell = "sh";
        }
        else if (is_command_available("bash"))
        {
            shell = "bash";
        }
        else
        {
            log_error("错误：找不到可用的 shell（sh 或 bash）！");
            return; 
        }
        if (opts.showtopics)
        {
            // 格式: <主题名> "<消息>"
            snprintf(command, sizeof(command), "%s %s \"%.*s\" \"%s\" &",
                     shell,
                     opts.script,
                     md->topicName->lenstring.len, md->topicName->lenstring.data,
                     payload_buf);
        }
        else 
        {
            // 仅发送消息，不带主题 "<消息>"
            snprintf(command, sizeof(command), "%s %s \"%s\" &", shell, opts.script, payload_buf);
        }

        system(command);
    }
}

void* subscribeTopic(void* topic_param)
{
    char* topic = strdup((char*)topic_param);  // 复制主题，防止传递的指针失效
    int rc = 0;
    MQTTClient c;
    unsigned char buf[100];
    unsigned char readbuf[100];
    Network n;

    while (!toStop)  // 无限循环，直到 `toStop` 设为 `true`
    {
        NetworkInit(&n);

        log_info("【%s】<=== 正在连接到 MQTT 服务器 ===>【%s:%d】", topic, opts.host, opts.port);
        rc = NetworkConnect(&n, opts.host, opts.port);
        if (rc != 0)
        {
            log_error("【%s】无法连接到服务器，状态码：%d，5 秒后重试...", topic, rc);
            NetworkDisconnect(&n);
            sleep(5); // 5 秒后重试
            continue;
        }

        MQTTClientInit(&c, &n, 1000, buf, 100, readbuf, 100);

        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.willFlag = 0;
        data.MQTTVersion = 3;
        data.clientID.cstring = opts.clientid;
        data.username.cstring = opts.username;
        data.password.cstring = opts.password;
        data.keepAliveInterval = 10;
        data.cleansession = 1;

        rc = MQTTConnect(&c, &data);
        if (rc != 0)
        {
            log_error("【%s】MQTT 连接失败，状态码：%d，5 秒后重试...", topic, rc);
            NetworkDisconnect(&n);
            sleep(5);
            continue;
        }

        log_info("【%s】成功连接到服务器！", topic);

        rc = MQTTSubscribe(&c, topic, opts.qos, messageArrived);
        if (rc != 0)
        {
            log_error("【%s】订阅失败，状态码：%d，5 秒后重试...", topic, rc);
            MQTTDisconnect(&c);
            NetworkDisconnect(&n);
            sleep(5);
            continue;
        }

        log_info("【%s】成功订阅主题！", topic);

        // 监听消息
        while (!toStop)
        {
            rc = MQTTYield(&c, 1000);
            if (rc != 0)
            {
                log_error("【%s】MQTT 连接中断，状态码：%d，尝试重连...", topic, rc);
                break; // 退出监听，重新连接
            }
        }

        log_info("【%s】连接已断开，准备重新连接...", topic);
        MQTTDisconnect(&c);
        NetworkDisconnect(&n);
        sleep(5);  // 等待 5 秒再尝试重连
    }

    log_info("【%s】线程终止，取消订阅并释放资源。", topic);
    free(topic);
    return NULL;
}

int main(int argc, char** argv)
{
    int rc = 0;
    unsigned char buf[100];
    unsigned char readbuf[100];

    if (argc < 2)
        usage();

    char* topic_list = argv[1];
    char* topics = strdup(topic_list);

    getopts(argc, argv);

    int topic_count = 0;
    char* topic = strtok(topics, ",");
    while (topic != NULL)
    {
        topic_count++;
        if (strchr(topic, '#') || strchr(topic, '+'))
        {
            opts.showtopics = 1;
            log_info("检测到主题名包含通配符 # 或 + ，默认开启主题名显示。");
        }
        topic = strtok(NULL, ",");
    }

    if (topic_count > 1)
    {
        opts.showtopics = 1;
        log_info("检测到订阅多个主题，默认开启主题名显示。");
    }

    free(topics);

    pthread_t threads[MAX_CONCURRENT_SUBSCRIPTIONS];
    topic = strtok(topic_list, ",");
    int thread_count = 0;
    while (topic != NULL && thread_count < MAX_CONCURRENT_SUBSCRIPTIONS)
    {
        pthread_create(&threads[thread_count], NULL, subscribeTopic, (void*)topic);
        thread_count++;
        topic = strtok(NULL, ",");
    }

    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
