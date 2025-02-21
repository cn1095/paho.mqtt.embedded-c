#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "MQTTClient.h"

#define MAX_TOPICS_PER_PROCESS 50  // 每个进程最多订阅 50 个主题
#define MAX_TOPIC_COUNT 500        // 允许最大主题数量

volatile int toStop = 0;

void cfinish(int sig) {
    signal(SIGINT, NULL);
    toStop = 1;
}

// MQTT 连接参数
struct opts_struct {
    char *clientid;
    int nodelimiter;
    char *delimiter;
    enum QoS qos;
    char *username;
    char *password;
    char *host;
    int port;
    int showtopics;
    char *script;
} opts = {
    (char *)"stdout-subscriber", 0, (char *)"\n", QOS1, NULL, NULL, (char *)"bemfa.com", 9501, 0
};

// MQTT 消息到达回调函数
void messageArrived(MessageData *md) {
    MQTTMessage *message = md->message;
    if (opts.showtopics)
        printf("%.*s\t", md->topicName->lenstring.len, md->topicName->lenstring.data);
    if (opts.nodelimiter)
        printf("%.*s", (int)message->payloadlen, (char *)message->payload);
    else
        printf("%.*s%s", (int)message->payloadlen, (char *)message->payload, opts.delimiter);

    if (opts.script) {
        char command[2048];
        snprintf(command, sizeof(command), "sh %s \"%.*s\" &", opts.script, (int)message->payloadlen, (char *)message->payload);
        system(command);
    }
}

// 处理 MQTT 订阅，每个进程独立运行
void handle_mqtt_process(char *topic_list[], int topic_count) {
    int rc;
    unsigned char buf[100];
    unsigned char readbuf[100];

    Network n;
    MQTTClient c;

    NetworkInit(&n);
    NetworkConnect(&n, opts.host, opts.port);
    MQTTClientInit(&c, &n, 1000, buf, 100, readbuf, 100);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = opts.clientid;
    data.username.cstring = opts.username;
    data.password.cstring = opts.password;
    data.keepAliveInterval = 10;
    data.cleansession = 1;

    printf("[进程 %d] 连接到服务器 【%s %d】...\n", getpid(), opts.host, opts.port);
    rc = MQTTConnect(&c, &data);
    if (rc != 0) {
        fprintf(stderr, "[进程 %d] 连接失败，状态码：%d\n", getpid(), rc);
        return;
    }
    printf("[进程 %d] 连接成功!\n", getpid());

    for (int i = 0; i < topic_count; i++) {
        rc = MQTTSubscribe(&c, topic_list[i], opts.qos, messageArrived);
        if (rc != 0) {
            fprintf(stderr, "[进程 %d] 订阅失败：%s，状态码：%d\n", getpid(), topic_list[i], rc);
        } else {
            printf("[进程 %d] 订阅成功：%s\n", getpid(), topic_list[i]);
        }
    }

    while (!toStop) {
        MQTTYield(&c, 1000);
    }

    printf("[进程 %d] 断开连接...\n", getpid());
    MQTTDisconnect(&c);
    NetworkDisconnect(&n);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("用法: %s 主题名称1,主题名称2,... [参数]\n", argv[0]);
        exit(1);
    }

    char *topic_list[MAX_TOPIC_COUNT];
    int topic_count = 0;
    char *token = strtok(argv[1], ",");
    while (token && topic_count < MAX_TOPIC_COUNT) {
        topic_list[topic_count++] = strdup(token);
        token = strtok(NULL, ",");
    }

    if (topic_count > MAX_TOPICS_PER_PROCESS) {
        int num_processes = (topic_count + MAX_TOPICS_PER_PROCESS - 1) / MAX_TOPICS_PER_PROCESS;
        int start_index = 0;

        printf("检测到 %d 个主题，启动 %d 个进程进行处理...\n", topic_count, num_processes);

        for (int i = 0; i < num_processes; i++) {
            int end_index = start_index + MAX_TOPICS_PER_PROCESS;
            if (end_index > topic_count)
                end_index = topic_count;

            int pid = fork();
            if (pid == 0) {  // 子进程
                handle_mqtt_process(&topic_list[start_index], end_index - start_index);
                exit(0);
            } else if (pid < 0) {
                fprintf(stderr, "进程创建失败\n");
                exit(1);
            }

            start_index = end_index;
        }

        // 父进程等待所有子进程完成
        for (int i = 0; i < num_processes; i++) {
            wait(NULL);
        }
    } else {
        handle_mqtt_process(topic_list, topic_count);
    }

    return 0;
}
