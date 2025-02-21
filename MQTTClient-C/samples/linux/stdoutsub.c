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
#include <stdio.h>
#include <memory.h>
#include "MQTTClient.h"

#include <stdio.h>
#include <signal.h>

#include <sys/time.h>


volatile int toStop = 0;


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
					printf("警告：MQTT 支持Qos0 Qos1等级，支持retian保留消息，不支持qos2，使用qos2会被强制下线，次数过多可造成账号异常无法使用。\n");
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
					opts.showtopics = 1;
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


void messageArrived(MessageData* md)
{
	MQTTMessage* message = md->message;

	if (opts.showtopics)
		printf("%.*s\t", md->topicName->lenstring.len, md->topicName->lenstring.data);
	if (opts.nodelimiter)
		printf("%.*s", (int)message->payloadlen, (char*)message->payload);
	else
		printf("%.*s%s", (int)message->payloadlen, (char*)message->payload, opts.delimiter);
	//fflush(stdout);

	if (opts.script)
	{
    		char payload_buf[1024];
    		int len = (message->payloadlen < 1023) ? message->payloadlen : 1023;
   		memcpy(payload_buf, message->payload, len);
    		payload_buf[len] = '\0'; 

    		char command[2048];
    		if (opts.showtopics)
    		{
        	// 格式: <主题名> "<消息>"
        	snprintf(command, sizeof(command), "sh %s \"%.*s\" \"%s\" &",
                 	opts.script,
                 	md->topicName->lenstring.len, md->topicName->lenstring.data,
                 	payload_buf);
    		}
    		else 
    		{
        	// 仅发送消息，不带主题 "<消息>"
        	snprintf(command, sizeof(command), "sh %s \"%s\" &", opts.script, payload_buf);
    		}

    		system(command);
	}
}


int main(int argc, char** argv)
{
	int rc = 0;
	unsigned char buf[100];
	unsigned char readbuf[100];
	
	if (argc < 2)
		usage();
	
	char* topic = argv[1];

	if (strchr(topic, '#') || strchr(topic, '+'))
	{
		opts.showtopics = 1;
		printf("检测到主题名称中包含通配符 # 或 + ，默认开启消息输出主题名。\n");
	}
	if (opts.showtopics)
		printf("主题名是： %s\n", topic);

	getopts(argc, argv);	
	printf("\n状态码：0 表示成功，-1 表示失败。\n\n");
	Network n;
	MQTTClient c;

	signal(SIGINT, cfinish);
	signal(SIGTERM, cfinish);

	NetworkInit(&n);
	NetworkConnect(&n, opts.host, opts.port);
	MQTTClientInit(&c, &n, 1000, buf, 100, readbuf, 100);
 
	MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
	data.willFlag = 0;
	data.MQTTVersion = 3;
	data.clientID.cstring = opts.clientid;
	data.username.cstring = opts.username;
	data.password.cstring = opts.password;

	data.keepAliveInterval = 10;
	data.cleansession = 1;
	printf("正在连接到 【%s %d】\n", opts.host, opts.port);
	
	rc = MQTTConnect(&c, &data);
	printf("连接状态： %d\n", rc);
    
    	printf("正在订阅主题： 【%s】\n", topic);
	rc = MQTTSubscribe(&c, topic, opts.qos, messageArrived);
	printf("订阅状态： %d\n", rc);

	while (!toStop)
	{
		MQTTYield(&c, 1000);	
	}
	
	printf("正在关闭...\n");

	MQTTDisconnect(&c);
	NetworkDisconnect(&n);

	return 0;
}
