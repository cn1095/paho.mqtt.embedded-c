/*******************************************************************************
 * Copyright (c) 2012, 2013 IBM Corp.
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
 
*/
#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <time.h>

#include "MQTTClient.h"

#define DEFAULT_STACK_SIZE -1

#include "linux.cpp"

#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>


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
	MQTT::QoS qos;
	char* username;
	char* password;
	char* host;
	int port;
	int showtopics;
	char* script;
} opts =
{
	(char*)"stdout-subscriber", 0, (char*)"\n", MQTT::QOS1, NULL, NULL, (char*)"bemfa.com", 9501, 0
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
					opts.qos = MQTT::QOS0;
				else if (strcmp(argv[count], "1") == 0)
					opts.qos = MQTT::QOS1;
				else if (strcmp(argv[count], "2") == 0)
				{
					opts.qos = MQTT::QOS2;
					log_info("警告：MQTT 支持Qos0 Qos1等级，支持retian保留消息，不支持qos2，使用qos2会被强制下线，次数过多可造成账号异常无法使用。");
				}
				else
					usage();
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
					opts.showtopics = 0;
				else
					usage();
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

void messageArrived(MQTT::MessageData& md)
{
	MQTT::Message &message = md.message;

	if (opts.showtopics)
		printf("%.*s\t", md.topicName.lenstring.len, md.topicName.lenstring.data);
	if (opts.nodelimiter)
		printf("%.*s", (int)message.payloadlen, (char*)message.payload);
	else
		printf("%.*s%s", (int)message.payloadlen, (char*)message.payload, opts.delimiter);
	fflush(stdout);
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

void myconnect(IPStack &ipstack, MQTT::Client<IPStack, Countdown, 1000> &client, MQTTPacket_connectData &data, const char *topic)
{
    log_info("【%s】<===正在连接到 MQTT 服务器===>【%s:%d】", topic, opts.host, opts.port);
    int rc = ipstack.connect(opts.host, opts.port);
    if (rc != 0)
    {
        log_error("【%s】TCP 连接失败，返回码：%d", topic, rc);
        return;
    }

    rc = client.connect(data);
    if (rc != 0)
    {
        log_error("【%s】MQTT 连接失败，返回码：%d", topic, rc);
        return;
    }

    log_info("【%s】成功连接到 MQTT 服务器！", topic);
}

void *subscribeTopic(void *topic_param)
{
    char *topic = (char *)topic_param;
    IPStack ipstack;
    MQTT::Client<IPStack, Countdown, 1000> client(ipstack);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.willFlag = 0;
    data.MQTTVersion = 3;
    data.clientID.cstring = opts.clientid;
    data.username.cstring = opts.username;
    data.password.cstring = opts.password;
    data.keepAliveInterval = 10;
    data.cleansession = 1;

    myconnect(ipstack, client, data, topic);

    int rc = client.subscribe(topic, opts.qos, messageArrived);
    if (rc != 0)
    {
        log_error("【%s】订阅失败，返回码：%d", topic, rc);
    }
    else
    {
        log_info("【%s】成功订阅主题！", topic);
    }

    while (!toStop)
    {
        client.yield(1000);
    }

    log_info("【%s】取消订阅并断开连接。", topic);
    client.disconnect();
    ipstack.disconnect();

    free(topic); // 释放 strdup 分配的内存
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        usage();

    getopts(argc, argv);
    signal(SIGINT, cfinish);
    signal(SIGTERM, cfinish);

    char *topic_list = strdup(argv[1]); // 复制输入的主题列表，避免 `strtok` 修改原数据
    pthread_t threads[MAX_CONCURRENT_SUBSCRIPTIONS];

    int thread_count = 0;
    char *topic = strtok(topic_list, ",");
    while (topic != NULL && thread_count < MAX_CONCURRENT_SUBSCRIPTIONS)
    {
        char *topic_copy = strdup(topic); // 复制主题字符串，避免 `strtok` 影响
        pthread_create(&threads[thread_count], NULL, subscribeTopic, (void *)topic_copy);
        thread_count++;
        topic = strtok(NULL, ",");
    }

    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    free(topic_list);
    return 0;
}

