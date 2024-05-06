#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fstream>  
#include "httpHeader.h"
#include "threadPool.h"

#define PORT 8080
#define IP "127.0.0.1"
#define BUFSIZE 8192

// 保存路径
const std::string serverpath("/home/lyj/hls/server/");
// 缓冲区
char buf[BUFSIZE];

void handle_cgi(int client_sock, httpHeader& http)
{
    int cgi_output[2];
    int cgi_input[2];

    // cgi的pipe输出
    pipe(cgi_output);
    // cgi的pipe输入
    pipe(cgi_input);

    // 启动一个进程用来启动cgi程序
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork error\n");
        exit(0);
    }
    if (pid == 0)
    {
        // 子进程
        close(cgi_output[0]); // 关闭了cgi_output中的读通道
        close(cgi_input[1]);  // 关闭了cgi_input中的写通道
        // 重定向输入输出
        dup2(cgi_output[1], STDOUT_FILENO);
        dup2(cgi_input[0], STDIN_FILENO);
        // 打印执行cgi脚本的路径
        execl("/usr/bin/python3", "/usr/bin/python3", "post.cgi", NULL);

        close(cgi_output[1]); // 关闭了cgi_output中的写通道
        close(cgi_input[0]);  // 关闭了cgi_input中的读通道

        exit(0);
    }
    else if (pid > 0)
    {
        // 父进程
        close(cgi_output[1]);
        close(cgi_input[0]);

        httpHeader::makeheader(httpHeader::params_200, buf, BUFSIZ);
        printf("%s", buf);
        send(client_sock, buf, strlen(buf), 0);

        // 读取cgi脚本返回数据
        int read_bytes = 0;
        while ((read_bytes = read(cgi_output[0], buf, BUFSIZ)) > 0)
        {
            send(client_sock, buf, read_bytes, 0);
        }

        // 关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);
        // 回收子进程
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            printf("waitpid error %d", status);
        }
    }
}

/* 保存推流端上传的文件 */
int handle_save(int client_sock, httpHeader& http) {
    // 保存文件的地址
    std::string filepath = serverpath;
    std::string m3u8path = serverpath;
    filepath += std::string("httpfile/video/") + http.get("username") + "/" + http.get("filename");
    m3u8path += std::string("httpfile/video/") + http.get("username") + "/main.m3u8";

    // 打开文件，如果文件不存在则创建它  
    // 使用 std::ios::binary 以二进制模式打开文件  
    // 使用 std::ios::out 以写入模式打开文件  
    std::fstream file(filepath, std::ios::binary | std::ios::out);
    // 检查文件是否成功打开  
    if (!file) {  
        std::cerr << "无法打开文件" << filepath << '!' << std::endl;  
        close(client_sock);
        return -1;  
    }   
    // 写入数据
    std::string data = http.get("OutBandData");
    file.write(data.c_str(), data.size());
    // 关闭文件
    file.close();


    // 打开m3u8文件，并追加内容
    std::fstream file2(m3u8path, std::ios::app);
    // 检查文件是否成功打开  
    if (!file2) {  
        std::cerr << "无法打开文件" << m3u8path << '!' << std::endl;  
        close(client_sock);
        return -1;  
    }   
    // 写入数据
    std::string data2 = "#EXTINF:10\n";
    data2 += "http://" + http.get("Host") + "/video/" + http.get("username") + "/" + http.get("filename") + "\n";
    file2.write(data2.c_str(), data2.size());
    // 关闭文件
    file2.close();

    return 0;
}

/* 将拉流端的文件传出 */
int handle_file(int client_sock, httpHeader& http) {
    std::string path = http.get("path");
    path = serverpath + "/httpfile" + path;
    // 如果是目录就添加html的头
    if (path.back() == '/') path += "index.html";

    // 查看文件状态
    struct stat st;
    int ret = stat(path.c_str(),&st);

    // 文件不存在
    if (ret < 0) {
        // 发送 404 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(httpHeader::params_404, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
        return -1;
    }

    // 获取文件大小,字符串
    std::string file_size = std::to_string(st.st_size);

    // 打开文件
    FILE *file = fopen(path.c_str(), "rb");
    if (file == NULL) {
        // 发送 400 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(httpHeader::params_400, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
        return -1;
    }

    // 如果请求的是图片
    else if (path.substr(path.size()-3).compare("png") == 0) {
        std::unordered_map<std::string,std::string> params = {
            {"http_version",HTTP_VERSION},
            {"status","200"},
            {"Server",SERVER_NAME},
            {"Content-Type","image/png"},
            {"Content-Length", file_size}
        };
        // 发送 200 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(params, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
    }

    // 如果请求的是视频
    else if (path.substr(path.size()-3).compare("mp4") == 0) {
        std::unordered_map<std::string,std::string> params = {
            {"http_version",HTTP_VERSION},
            {"status","200"},
            {"Server",SERVER_NAME},
            {"Content-Type","video/mp4"},
            {"Content-Length", file_size}
        };
        // 发送 200 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(params, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
    }

    // 如果请求的是视频
    else if (path.substr(path.size()-2).compare("ts") == 0) {
        std::unordered_map<std::string,std::string> params = {
            {"http_version",HTTP_VERSION},
            {"status","200"},
            {"Server",SERVER_NAME},
            {"Content-Type","video/ts"},
            {"Content-Length", file_size}
        };
        // 发送 200 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(params, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
    }

    // 如果请求的是m3u8请求
    else if (path.substr(path.size()-2).compare("ts") == 0) {
        std::unordered_map<std::string,std::string> params = {
            {"http_version",HTTP_VERSION},
            {"status","200"},
            {"Server",SERVER_NAME},
            {"Content-Type","text/m3u8"},
            {"Content-Length", file_size}
        };
        // 发送 200 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(params, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
    }

    else {
        // 发送 200 的头
        memset(buf,0,BUFSIZE);
        httpHeader::makeheader(httpHeader::params_200, buf, BUFSIZE);
        send(client_sock, buf, strlen(buf), 0);
    }

    // 读取文件并发送
    size_t bytesRead;
    while ((bytesRead = fread(buf, 1, BUFSIZE, file)) > 0) {
        send(client_sock, buf, bytesRead, 0);
    }
    fclose(file);

    return 0;
}

void handle(void* arg)
{
    int client_sock = *(int*)arg;
    // 解析http头信息
    httpHeader http(client_sock);

    std::string url = http.get("path");
    // 如果是POST方法，且url是/upload
    std::cout << "pthread:" << pthread_self();
    if (url.compare("/upload") == 0 && http.get_method() == METHOD_POST) {
        printf("handle_save\n");
        handle_save(client_sock, http);
    }

    // 如果是GET方法
    if (http.get_method() == METHOD_GET) {
        std::cout << "handle_file:" <<  url << std::endl;;
        handle_file(client_sock, http);
    }
    close(client_sock);
}


int main()
{
    // 创建线程池
    ThreadPool* pool = new ThreadPool(8, 10);

    int server = socket(PF_INET, SOCK_STREAM, 0);
    if (server == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 设置此选项，强制重新使用处于TIME_WAIT状态的socket地址
    int option;
    socklen_t optlen;
    optlen = sizeof(option);
    option = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

    struct sockaddr_in myaddr;
    bzero(&myaddr, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(PORT);
    myaddr.sin_addr.s_addr = INADDR_ANY;
    socklen_t myaddrlen = sizeof(myaddr);
    if (bind(server, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1) {
        perror("Binding failed");
        close(server);
        exit(EXIT_FAILURE);
    }

    if (listen(server, 5) == -1)
    {
        perror("Listening failed");
        close(server);
        exit(EXIT_FAILURE);
    }

    while (true)
    {
        struct sockaddr clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);
        int client_sock = accept(server, &clientaddr, &clientaddrlen);
        if (client_sock < 0)
        {
            perror("accpet error\n");
            continue;
        }
        else if (client_sock == 0)
        {
            close(client_sock);
            continue;
        }
        else if (client_sock > 0)
        {
            int* num = (int*)malloc(sizeof(int));
            *num = client_sock;
            pool->addTask(handle, num);
        }
    }
}