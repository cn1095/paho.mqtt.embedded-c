#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include "MQTTClient.h"

#define MAX_TOPICS_PER_THREAD 50  // 每个线程最多处理 50 个主题
#define MAX_THREADS 10            // 允许的最大线程数
#define MAX_TOPIC_LENGTH 100      // 单个主题的最大长度
#define MAX_PAYLOAD_LENGTH 1024   // MQTT 消息的最大长度

volatile int toStop = 0;

struct MQTTThreadArgs
{
    char **topics;     // 主题列表
    int topic_count;   // 主题数量
    char clientid[64]; // 客户端 ID
};

void cfinish(int sig)
{
    signal(SIGINT, NULL);
    toStop = 1;
}

void messageArrived(MessageData *md)
{
    MQTTMessage *message = md->message;
    printf("[收到消息] 主题: %.*s, 内容: %.*s\n",
           md->topicName->lenstring.len, md->topicName->lenstring.data,
           (int)message->payloadlen, (char *)message->payload);
}

void *mqtt_thread(void *args)
{
    struct MQTTThreadArgs *mqttArgs = (struct MQTTThreadArgs *)args;

    Network n;
    MQTTClient c;
    unsigned char sendbuf[100];
    unsigned char readbuf[100];

    NetworkInit(&n);
    if (NetworkConnect(&n, "bemfa.com", 9501) != 0)
    {
        fprintf(stderr, "线程 [%s] 连接失败\n", mqttArgs->clientid);
        pthread_exit(NULL);
    }

    MQTTClientInit(&c, &n, 1000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = mqttArgs->clientid;
    data.username.cstring = NULL;
    data.password.cstring = NULL;
    data.keepAliveInterval = 10;
    data.cleansession = 1;

    printf("线程 [%s] 连接到 MQTT 服务器...\n", mqttArgs->clientid);
    if (MQTTConnect(&c, &data) != 0)
    {
        fprintf(stderr, "线程 [%s] 连接 MQTT 失败\n", mqttArgs->clientid);
        pthread_exit(NULL);
    }

    printf("线程 [%s] 订阅 %d 个主题:\n", mqttArgs->clientid, mqttArgs->topic_count);
    for (int i = 0; i < mqttArgs->topic_count; i++)
    {
        printf("  - %s\n", mqttArgs->topics[i]);
        if (MQTTSubscribe(&c, mqttArgs->topics[i], QOS1, messageArrived) != 0)
        {
            fprintf(stderr, "线程 [%s] 订阅失败: %s\n", mqttArgs->clientid, mqttArgs->topics[i]);
        }
    }

    while (!toStop)
    {
        MQTTYield(&c, 1000);
    }

    printf("线程 [%s] 断开连接...\n", mqttArgs->clientid);
    MQTTDisconnect(&c);
    NetworkDisconnect(&n);
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "用法: %s <主题1,主题2,...>\n", argv[0]);
        return -1;
    }

    char *topic_list = strdup(argv[1]); // 复制主题参数
    char *topics[MAX_THREADS * MAX_TOPICS_PER_THREAD]; // 主题存储
    int topic_count = 0;

    // 拆分主题
    char *token = strtok(topic_list, ",");
    while (token != NULL && topic_count < MAX_THREADS * MAX_TOPICS_PER_THREAD)
    {
        topics[topic_count++] = strdup(token);
        token = strtok(NULL, ",");
    }
    free(topic_list);

    // 计算需要的线程数量
    int thread_count = (topic_count + MAX_TOPICS_PER_THREAD - 1) / MAX_TOPICS_PER_THREAD;
    if (thread_count > MAX_THREADS)
    {
        fprintf(stderr, "错误: 主题数量超出 %d 限制\n", MAX_THREADS * MAX_TOPICS_PER_THREAD);
        return -1;
    }

    // 创建线程
    pthread_t threads[MAX_THREADS];
    struct MQTTThreadArgs args[MAX_THREADS];

    for (int i = 0; i < thread_count; i++)
    {
        args[i].topic_count = (i == thread_count - 1) ? (topic_count % MAX_TOPICS_PER_THREAD) : MAX_TOPICS_PER_THREAD;
        if (args[i].topic_count == 0)
            args[i].topic_count = MAX_TOPICS_PER_THREAD;

        args[i].topics = &topics[i * MAX_TOPICS_PER_THREAD];
        snprintf(args[i].clientid, sizeof(args[i].clientid), "client_%d", i);

        if (pthread_create(&threads[i], NULL, mqtt_thread, &args[i]) != 0)
        {
            fprintf(stderr, "线程创建失败: client_%d\n", i);
            return -1;
        }
    }

    // 捕捉退出信号
    signal(SIGINT, cfinish);
    signal(SIGTERM, cfinish);

    // 等待线程结束
    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // 释放内存
    for (int i = 0; i < topic_count; i++)
    {
        free(topics[i]);
    }

    printf("所有 MQTT 线程已结束\n");
    return 0;
}
