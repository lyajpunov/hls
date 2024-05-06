#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

#define PORT 8080
#define IP "127.0.0.1"
#define BUFSIZE 1024
const char *username = "lyj";

int main()
{
    struct stat file_stat;
    char buf[BUFSIZE];
    int i = 0;

    while (true)
    {

        int client = socket(PF_INET, SOCK_STREAM, 0);
        if (client == -1)
        {
            perror("创建socket失败!\n");
            exit(EXIT_FAILURE);
        }

        // 设置此选项，强制重新使用处于TIME_WAIT状态的socket地址
        int option;
        socklen_t optlen;
        optlen = sizeof(option);
        option = 1;
        setsockopt(client, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

        // 设置连接地址
        struct sockaddr_in myaddr;
        bzero(&myaddr, sizeof(myaddr));
        myaddr.sin_family = AF_INET;
        myaddr.sin_port = htons(PORT);
        myaddr.sin_addr.s_addr = inet_addr(IP);
        socklen_t myaddrlen = sizeof(myaddr);

        // 连接服务器
        if (connect(client, (struct sockaddr *)&myaddr, myaddrlen) < 0)
        {
            perror("连接失败！\n");
            return -1;
        }

        // 获取文件名
        sprintf(buf, "/home/lyj/hls/client/video-data/WLWZ%d.ts", i);

        // 获取文件大小
        if (stat(buf, &file_stat) == -1)
        {
            perror("Error getting file status");
            return -1; // 出现错误时返回-1
        }

        // 打开文件
        FILE *file = fopen(buf, "rb");

        // 发送消息头
        sprintf(buf, "POST /upload?username=%s&filename=WLWZ%d.ts HTTP/1.1\r\nContent-Type: video/ts\r\nHost: %s:%d\r\nContent-Length: %ld\r\n\r\n", username, i, IP, PORT, file_stat.st_size);
        if (send(client, buf, strlen(buf), 0) != strlen(buf))
        {
            perror("发送失败！\n");
            return -1;
        }

        // 发送消息体
        size_t bytesSentTotal = 0;
        size_t bytesRead;
        while ((bytesRead = fread(buf, 1, BUFSIZE, file)) > 0)
        {
            size_t bytesSent = send(client, buf, bytesRead, 0);
            if (bytesSent < 0)
            {
                perror("Error sending file");
                fclose(file);
                return -1;
            }
            bytesSentTotal += bytesSent;
        }

        // 关闭文件
        fclose(file);

        // 检查是否发送完整个文件
        if (bytesSentTotal != file_stat.st_size)
        {
            perror("没有完全发送");
            return -1;
        }
        printf("发送成功: WLWZ%d\n",i);

        i++;
        // 关闭套接字
        close(client);
        // 休息十秒
        sleep(10);
    }

    return 0;
}
