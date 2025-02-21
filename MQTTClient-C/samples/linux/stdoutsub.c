#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "MQTTClient.h"

#define MAX_THREADS 50  // 线程最大并发数

volatile int toStop = 0;

struct opts_struct {
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
} opts = {
    (char*)"stdout-subscriber", 0, (char*)"\n", QOS1, NULL, NULL, (char*)"bemfa.com", 9501, 0, NULL
};

// 订阅主题的参数结构体
typedef struct {
    char topic[128];
    int thread_index;
} ThreadArg;

// 线程数组
pthread_t threads[MAX_THREADS];
int active_threads = 0;
pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_cond = PTHREAD_COND_INITIALIZER;

// 线程完成后的清理函数
void thread_cleanup() {
    pthread_mutex_lock(&thread_mutex);
    active_threads--;
    pthread_cond_signal(&thread_cond);  // 释放等待的线程
    pthread_mutex_unlock(&thread_mutex);
}

// 处理收到的 MQTT 消息
void messageArrived(MessageData* md) {
    MQTTMessage* message = md->message;
    
    if (opts.showtopics)
        printf("%.*s\t", md->topicName->lenstring.len, md->topicName->lenstring.data);
    if (opts.nodelimiter)
        printf("%.*s", (int)message->payloadlen, (char*)message->payload);
    else
        printf("%.*s%s", (int)message->payloadlen, (char*)message->payload, opts.delimiter);
    
    fflush(stdout);

    if (opts.script) {
        char payload_buf[1024];
        int len = (message->payloadlen < 1023) ? message->payloadlen : 1023;
        memcpy(payload_buf, message->payload, len);
        payload_buf[len] = '\0'; 

        char command[2048];
        snprintf(command, sizeof(command), "sh %s \"%s\" &", opts.script, payload_buf);
        system(command);
    }
}

// MQTT 订阅线程函数
void* subscribeThread(void* arg) {
    ThreadArg* thread_arg = (ThreadArg*)arg;
    char* topic = thread_arg->topic;
    int thread_index = thread_arg->thread_index;

    printf("[线程 %d] 正在连接并订阅主题: %s\n", thread_index, topic);

    Network n;
    MQTTClient c;
    unsigned char buf[100];
    unsigned char readbuf[100];

    NetworkInit(&n);
    if (NetworkConnect(&n, opts.host, opts.port) != 0) {
        fprintf(stderr, "[线程 %d] 连接失败: %s\n", thread_index, topic);
        thread_cleanup();
        return NULL;
    }

    MQTTClientInit(&c, &n, 1000, buf, 100, readbuf, 100);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID.cstring = opts.clientid;
    data.username.cstring = opts.username;
    data.password.cstring = opts.password;
    data.keepAliveInterval = 10;
    data.cleansession = 1;

    if (MQTTConnect(&c, &data) != 0) {
        fprintf(stderr, "[线程 %d] MQTT 连接失败: %s\n", thread_index, topic);
        NetworkDisconnect(&n);
        thread_cleanup();
        return NULL;
    }

    printf("[线程 %d] 连接成功，正在订阅: %s\n", thread_index, topic);
    if (MQTTSubscribe(&c, topic, opts.qos, messageArrived) != 0) {
        fprintf(stderr, "[线程 %d] 订阅失败: %s\n", thread_index, topic);
        MQTTDisconnect(&c);
        NetworkDisconnect(&n);
        thread_cleanup();
        return NULL;
    }

    printf("[线程 %d] 订阅成功: %s\n", thread_index, topic);

    while (!toStop) {
        MQTTYield(&c, 1000);
    }

    printf("[线程 %d] 断开订阅: %s\n", thread_index, topic);
    MQTTDisconnect(&c);
    NetworkDisconnect(&n);

    thread_cleanup();
    return NULL;
}

// 创建线程订阅 MQTT
void createThreadForTopic(char* topic, int thread_index) {
    pthread_mutex_lock(&thread_mutex);
    
    // 等待有空闲的线程槽
    while (active_threads >= MAX_THREADS) {
        pthread_cond_wait(&thread_cond, &thread_mutex);
    }
    
    // 线程参数
    ThreadArg* thread_arg = (ThreadArg*)malloc(sizeof(ThreadArg));
    strncpy(thread_arg->topic, topic, sizeof(thread_arg->topic) - 1);
    thread_arg->thread_index = thread_index;

    // 启动线程
    if (pthread_create(&threads[thread_index], NULL, subscribeThread, (void*)thread_arg) == 0) {
        active_threads++;
    } else {
        fprintf(stderr, "线程创建失败: %s\n", topic);
        free(thread_arg);
    }

    pthread_mutex_unlock(&thread_mutex);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s 主题名称1,主题名称2,...\n", argv[0]);
        return -1;
    }

    char* topic_list = argv[1];
    char* topics = strdup(topic_list);
    char* topic = strtok(topics, ",");
    int thread_index = 0;

    while (topic != NULL) {
        createThreadForTopic(topic, thread_index);
        thread_index++;
        topic = strtok(NULL, ",");
    }

    free(topics);

    // 等待所有线程结束
    for (int i = 0; i < thread_index; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("所有订阅线程已结束，程序退出。\n");
    return 0;
}
