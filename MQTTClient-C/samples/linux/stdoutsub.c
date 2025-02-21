#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <time.h>
#include "MQTTClient.h"

#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

volatile int toStop = 0;

// 最大并发订阅数
#define MAX_CONCURRENT_SUBSCRIPTIONS 50

// 日志输出函数
void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stdout, "[INFO] [%s] ", time_buf);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stderr, "[ERROR] [%s] ", time_buf);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    
    va_end(args);
}

void usage()
{
    log_info("====================================");
    log_info("       MQTT 标准输出订阅器         ");
    log_info("====================================");
    log_info("用法: stdoutsub 主题名称 <命令>");
    log_info("\n可选命令:");
    log_info("  --host <主机名>         （默认: bemfa.com，MQTT 服务器地址）");
    log_info("  --port <端口>           （默认: 9501，MQTT 服务器端口）");
    log_info("  --qos <服务质量>        （默认: 1，MQTT QoS 等级，可选 0 或 1）");
    log_info("  --delimiter <分隔符>    （默认: \\n，消息之间的分隔符）");
    log_info("  --clientid <账户私钥>   （默认: 主机名 + 时间戳）");
    log_info("  --username <用户名>     （默认: none，无用户名）");
    log_info("  --password <密码>       （默认: none，无密码）");
    log_info("  --showtopics <on|off>   （默认: off，是否显示主题名，若主题含通配符则默认开启）");
    log_info("  --script <脚本路径>     （收到 MQTT 消息时，执行指定脚本）");
    log_info("\n示例:");
    log_info("  stdoutsub 主题名 --host bemfa.com --port 9501 --qos 1 --clientid asa48fd88e53d356ab21841a951284d");
    log_info("\n");
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
                    log_info("开启消息主题名显示");
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

void* subscribeTopic(void* topic_param)
{
    char* topic = (char*)topic_param;
    int rc = 0;
    MQTTClient c;
    unsigned char buf[100];
    unsigned char readbuf[100];
    Network n;
    NetworkInit(&n);

    // 连接信息日志
    log_info("正在连接到 MQTT 服务器 [%s:%d]...", opts.host, opts.port);
    rc = NetworkConnect(&n, opts.host, opts.port);
    if (rc != 0)
    {
        log_error("无法连接到服务器，状态码：%d", rc);
        return NULL;
    }
    log_info("成功连接到服务器 [%s:%d]", opts.host, opts.port);

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
        log_error("连接失败，状态码：%d", rc);
        NetworkDisconnect(&n);
        return NULL;
    }

    log_info("成功连接到服务器");

    rc = MQTTSubscribe(&c, topic, opts.qos, messageArrived);
    if (rc != 0)
    {
        log_error("订阅失败，主题：%s，状态码：%d", topic, rc);
    }
    else
    {
        log_info("成功订阅主题：%s", topic);
    }

    while (!toStop)
    {
        MQTTYield(&c, 1000);
    }

    MQTTDisconnect(&c);
    NetworkDisconnect(&n);

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
            log_info("检测到通配符 # 或 + ，默认开启主题名显示");
        }
        topic = strtok(NULL, ",");
    }

    if (topic_count > 1)
    {
        opts.showtopics = 1;
        log_info("检测到订阅多个主题，默认开启主题名显示");
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
