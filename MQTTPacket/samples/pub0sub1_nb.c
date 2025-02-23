#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MQTTPacket.h"
#include "transport.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <signal.h>
int toStop = 0;

void print_usage(char *program_name);

void cfinish(int sig)
{
    signal(SIGINT, NULL);
    toStop = 1;
}

void stop_init(void)
{
    signal(SIGINT, cfinish);
    signal(SIGTERM, cfinish);
}
/* 参数结构体 */
typedef struct {
    char* host;
    int port;
    char* clientID;
    char* username;
    char* password;
    char* topic;
    char* msg;
} MQTTConfig;

/* 声明 print_usage 函数 */
void print_usage(char *program_name);

/* 解析命令行参数 */
int parse_args(int argc, char *argv[], MQTTConfig *config)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            config->host = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config->port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--clientid") == 0 && i + 1 < argc) {
            config->clientID = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--username") == 0 && i + 1 < argc) {
            config->username = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            config->password = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--topic") == 0 && i + 1 < argc) {
            config->topic = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--msg") == 0 && i + 1 < argc) {
            config->msg = argv[i + 1];
            i++;
        } else {
            print_usage(argv[0]);  // 传递程序名称
            return -1;
        }
    }
    return 0;
}

/* 打印程序使用说明 */
void print_usage(char *program_name)
{
    printf("用法: %s --host <host> --port <port> --clientid <client_id> --topic <topic> --msg <message> [--username <username>] [--password <password>]\n", program_name);
    printf("    --host      MQTT服务器地址（默认：bemfa.com）\n");
    printf("    --port      MQTT服务器端口（默认: 9501）\n");
    printf("    --clientid  账户私钥（不能为空）\n");
    printf("    --topic     主题名称（不能为空）\n");
    printf("    --msg       要发布的指令（不能为空）\n");
    printf("    --username  可选，巴法MQTT用户名\n");
    printf("    --password  可选，巴法MQTT密码\n");
}

int main(int argc, char *argv[])
{
    MQTTConfig config = {
        .host = "bemfa.com",
        .port = 9501,
        .clientID = "",
        .username = "",
        .password = "",
        .topic = "",
        .msg = "消息"
    };

    // 解析命令行参数
    if (parse_args(argc, argv, &config) != 0) {
        print_usage(argv[0]);  // 传递程序名称
        return -1;
    }

    // 检查必需的参数
    if (config.clientID == NULL || strlen(config.clientID) == 0 ||
        config.topic == NULL || strlen(config.topic) == 0 ||
        config.msg == NULL || strlen(config.msg) == 0) {
        printf("错误: 账户私钥、主题名称和指令不能为空。\n");
        print_usage(argv[0]);  // 传递程序名称
        return -1;
    }

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    int rc = 0;
    int mysock = 0;
    unsigned char buf[200];
    int buflen = sizeof(buf);
    int msgid = 1;
    MQTTString topicString = MQTTString_initializer;
    int req_qos = 0;
    char* payload = config.msg;
    int payloadlen = strlen(payload);
    int len = 0;
    MQTTTransport mytransport;
    int retry_count = 0;
    int max_retries = 3;

    stop_init();

    /* 打开连接 */
    mysock = transport_open(config.host, config.port);
    if(mysock < 0) {
        printf("连接失败，退出程序！\n");
        return mysock;
    }

    printf("连接到主机：%s，端口：%d\n", config.host, config.port);

    mytransport.sck = &mysock;
    mytransport.getfn = transport_getdatanb;
    mytransport.state = 0;

    data.clientID.cstring = config.clientID;
    data.keepAliveInterval = 20;
    data.cleansession = 1;
    data.username.cstring = config.username;
    data.password.cstring = config.password;

    len = MQTTSerialize_connect(buf, buflen, &data);
    rc = transport_sendPacketBuffer(mysock, buf, len);

    /* 等待 connack 响应 */
    while (retry_count < max_retries) {
        if (MQTTPacket_read(buf, buflen, transport_getdata) == CONNACK) {
            unsigned char sessionPresent, connack_rc;
            if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, buf, buflen) == 1 && connack_rc == 0) {
                printf("成功连接到 MQTT 服务器！\n");
                break;
            } else {
                printf("连接失败，返回码 %d，重试...\n", connack_rc);
                retry_count++;
            }
        } else {
            printf("未收到有效的连接响应，重试...\n");
            retry_count++;
        }
        if (retry_count < max_retries) {
            #ifdef _WIN32
                Sleep(1000); 
            #else
                sleep(1);  
            #endif
        }
    }

    if (retry_count == max_retries) {
        printf("连接失败，已达最大重试次数，退出程序。\n");
        goto exit;
    }

    /* 订阅主题 */
    topicString.cstring = config.topic;
    len = MQTTSerialize_subscribe(buf, buflen, 0, msgid, 1, &topicString, &req_qos);
    rc = transport_sendPacketBuffer(mysock, buf, len);

    retry_count = 0;
    while (retry_count < max_retries) {
        int frc = MQTTPacket_readnb(buf, buflen, &mytransport);
        if (frc == SUBACK) {
            unsigned short submsgid;
            int subcount;
            int granted_qos;
            rc = MQTTDeserialize_suback(&submsgid, 1, &subcount, &granted_qos, buf, buflen);
            if (granted_qos == 0) {
                printf("成功订阅主题【%s】\n", config.topic);
                break;
            } else {
                printf("订阅失败，已授予的QoS为 %d，重试...\n", granted_qos);
                retry_count++;
            }
        } else if (frc == -1) {
            printf("订阅失败，超时，重试...\n");
            retry_count++;
        }
        if (retry_count < max_retries) {
            #ifdef _WIN32
                Sleep(1000); 
            #else
                sleep(1);  
            #endif
        }
    }

    if (retry_count == max_retries) {
        printf("订阅失败，已达最大重试次数，退出程序。\n");
        goto exit;
    }

    /* 发布消息 */
    printf("开始发布指令：%s\n", payload);
    len = MQTTSerialize_publish(buf, buflen, 0, 0, 0, 0, topicString, (unsigned char*)payload, payloadlen);
    rc = transport_sendPacketBuffer(mysock, buf, len);

    printf("发布指令完成，等待响应状态...\n");

    /* 接收消息 */
    while (!toStop) {
        if (MQTTPacket_readnb(buf, buflen, &mytransport) == PUBLISH) {
            unsigned char dup;
            int qos;
            unsigned char retained;
            unsigned short msgid;
            int payloadlen_in;
            unsigned char* payload_in;
            int rc;
            MQTTString receivedTopic;

            rc = MQTTDeserialize_publish(&dup, &qos, &retained, &msgid, &receivedTopic,
                    &payload_in, &payloadlen_in, buf, buflen);
            printf("接收到指令状态：%.*s\n", payloadlen_in, payload_in);
            if (memcmp(payload_in, payload, payloadlen_in) == 0) {
                printf("更新成功！\n");
                break; 
            } else {
                printf("更新异常，请检查！\n");
            }
        }
    }

    /* 断开连接 */
    len = MQTTSerialize_disconnect(buf, buflen);
    rc = transport_sendPacketBuffer(mysock, buf, len);

exit:
    transport_close(mysock);

    return 0;
}
