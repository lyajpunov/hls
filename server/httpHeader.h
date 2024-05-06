#ifndef __httpHeader_H
#define __httpHeader_H

#include <unordered_map>
#include <string>
#include <cstring>
#include <iostream>
#include <sys/socket.h>

#define SERVER_NAME "LYJ's server" // 服务端名称
#define HTTP_VERSION "HTTP/1.1"    // 服务端http协议版本
#define BUFSIZE 8192

enum METHOD
{
    METHOD_GET = 1,
    METHOD_POST
};

class httpHeader
{
private:
    // 请求头中的参数
    std::unordered_map<std::string, std::string> cache;
    // 请求携带的参数
    std::unordered_map<std::string, std::string> param;
    // 获取一行，最后一个字符是换行符，没有\r，只有\n
    int get_line(int sock, std::string& buf);
    // 文件描述符
    int sock;
    // 是否有额外参数
    bool hasOtherParam;

public:
    httpHeader(const int sock);
    ~httpHeader();

    std::string get(const char *key);
    std::string get(std::string &key);
    int get_method();
    // 打印键值对
    void print();
    // 处理x_www_form_urlencoded方法的post参数
    void handle_pos_x_www_form_urlencoded();
    // 根据键值对信息生成相应的头
    static int makeheader(std::unordered_map<std::string, std::string> &params, std::string &buf);
    static int makeheader(std::unordered_map<std::string, std::string> &params, char *buf, int bufsize);
    // 状态码与描述之间的映射
    static std::unordered_map<std::string, std::string> status_2_description;
    static std::unordered_map<std::string, std::string> params_200;
    static std::unordered_map<std::string, std::string> params_400;
    static std::unordered_map<std::string, std::string> params_404;
    static std::unordered_map<std::string, std::string> params_500;
};

httpHeader::httpHeader(const int sock) : sock(sock), hasOtherParam(false)
{
    std::string buf;
    char buff[BUFSIZE];

    // 单独处理第一行
    if (get_line(sock, buf) > 1){
        // 找到第一个部分
        int pos1 = buf.find(' ');
        std::string method = buf.substr(0, pos1);
        // 找到第二个部分
        int pos2 = buf.find(" HTTP/");
        std::string path = buf.substr(pos1+1, pos2-pos1-1);
        // 找到第三个部分
        int pos3 = buf.find('\n');
        std::string version = buf.substr(pos2+1, pos3-pos2-1);
        // 添加到map中
        cache["method"] = method;
        cache["path"] = path;
        cache["version"] = version;

        buf.clear();
    }

    // 循环处理头的其他行
    while (get_line(sock, buf) > 1) {
        // 找到第一个部分
        int pos1 = buf.find(' ');
        std::string key = buf.substr(0, pos1-1);
        // 找到第二个部分
        int pos2 = buf.find('\n');
        std::string val = buf.substr(pos1+1, pos2-pos1-1);
        cache[key] = val;
        buf.clear();
    }

    // 保存外带数据
    if (cache.find("Content-Length") != cache.end()) {
        int bytes_read = 0;
        cache["OutBandData"] = "";
        while ((bytes_read = recv(sock, buff, BUFSIZE, 0)) > 0) {
            cache["OutBandData"].append(buff, bytes_read);
        }
        // 校验外带数据
        if (cache["OutBandData"].size() != std::stoi(cache["Content-Length"])) {
            std::cerr << "外带数据不完整\n";
            return;
        }
    }


    // 处理 URL 中携带的参数
    std::string line = cache["path"];
    int pos = line.find('?');
    if (pos != std::string::npos)
    {
        // url中存在额外参数
        hasOtherParam = true;
        cache["path"] = line.substr(0, pos);
        line.erase(0, pos + 1);
        // 循环处理参数部分
        while (!line.empty())
        {
            pos = line.find('=');
            if (pos != std::string::npos)
            {
                std::string key = line.substr(0, pos); // 提取参数名
                line.erase(0, pos + 1);                // 删除参数名
                pos = line.find('&');                      // 查找下一个参数的分隔符
                std::string value;
                if (pos != std::string::npos)
                {
                    value = line.substr(0, pos); // 提取参数值
                    line.erase(0, pos + 1);      // 删除参数值及分隔符
                }
                else
                {
                    value = line; // 如果没有下一个分隔符，说明这是最后一个参数
                    line.clear(); // 清空行
                }
                param[key] = value; // 将参数名和参数值存入 map
            }
        }
    }

}

httpHeader::~httpHeader() {}

int httpHeader::get_line(int sock, std::string& buf)
{
    int i = 0;
    char c = '\0';
    int n;

    while (c != '\n')
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf.push_back(c);
            i++;
        }
        else
            c = '\n';
    }

    return i;
}

std::string httpHeader::get(std::string& k) 
{
    if (cache.find(k) != cache.end())
    {
        return cache[k];
    }
    if (param.find(k) != param.end())
    {
        return param[k];
    }
    return nullptr;
}

std::string httpHeader::get(const char *key)
{  
    std::string k(key);  
  
    // 首先检查 cache  
    if (cache.find(k) != cache.end())  
    {  
        return cache[k];  
    }  
  
    // 然后检查 param  
    if (param.find(k) != param.end())  
    {  
        return param[k];  
    }  
  
    // 如果没有找到，可以返回一个空字符串或抛出异常  
    return std::string(); // 或者可以抛出 std::out_of_range 异常  
}

int httpHeader::get_method()
{
    if (cache.find("method") == cache.end())
        return -1;
    if (cache["method"].compare("GET") == 0 || cache["method"].compare("get") == 0)
        return METHOD_GET;
    if (cache["method"].compare("POST") == 0 || cache["method"].compare("post") == 0)
        return METHOD_POST;
    return -1;
}


void httpHeader::print()
{
    for (const auto &pair : cache)
    {
        std::cout << pair.first << ": " << pair.second << "\n";
    }
    for (const auto &pair : param)
    {
        std::cout << pair.first << ": " << pair.second << "\n";
    }
}

void httpHeader::handle_pos_x_www_form_urlencoded()
{
    std::string header = cache["OutBandData"];
    size_t pos = 0;
    // 处理post中携带的数据
    if (cache["method"].compare("POST") == 0 || cache["method"].compare("post") == 0)
    {
        while (!header.empty())
        {
            pos = header.find('=');
            if (pos != std::string::npos)
            {
                std::string key = header.substr(0, pos); // 提取参数名
                header.erase(0, pos + 1);                // 删除参数名
                pos = header.find('&');                  // 查找下一个参数的分隔符
                std::string value;
                if (pos != std::string::npos)
                {
                    value = header.substr(0, pos); // 提取参数值
                    header.erase(0, pos + 1);      // 删除参数值及分隔符
                }
                else
                {
                    value = header; // 如果没有下一个分隔符，说明这是最后一个参数
                    header.clear(); // 清空行
                }
                param[key] = value; // 将参数名和参数值存入 map
            }
        }
    }
}

int httpHeader::makeheader(std::unordered_map<std::string, std::string> &params, char *buf, int bufsize)
{
    std::string tmp;
    int headersize = httpHeader::makeheader(params, tmp);
    if (headersize > bufsize)
        return -1;
    strcpy(buf, tmp.c_str());
    return headersize;
}

int httpHeader::makeheader(std::unordered_map<std::string, std::string> &params, std::string &buf)
{
    buf.clear();
    if (params.find("http_version") != params.end())
    {
        buf += params["http_version"];
        buf += ' ';
    }
    else
    {
        goto failure;
    }

    if (params.find("status") != params.end())
    {
        buf += params["status"];
        buf += ' ';
        buf += status_2_description[params["status"]];
        buf += "\r\n";
    }
    else
    {
        goto failure;
    }

    for (auto para : params)
    {
        if (para.first.compare("http_version") == 0)
            continue;
        if (para.first.compare("status") == 0)
            continue;
        if (para.first.compare("description") == 0)
            continue;
        buf += para.first;
        buf += ": ";
        buf += para.second;
        buf += "\r\n";
    }
    buf += "\r\n";

    return buf.size();

failure:
    buf.clear();
    return -1;
}

std::unordered_map<std::string, std::string> httpHeader::status_2_description = {
    {"100", "Continue"},              // 继续
    {"101", "Switching Protocols"},   // 切换协议
    {"200", "OK"},                    // 成功
    {"201", "Created"},               // 已创建
    {"202", "Accepted"},              // 已接受
    {"204", "No Content"},            // 无内容
    {"301", "Moved Permanently"},     // 永久移动
    {"302", "Found"},                 // 临时移动
    {"303", "See Other"},             // 查看其他
    {"304", "Not Modified"},          // 未修改
    {"307", "Temporary Redirect"},    // 临时重定向
    {"308", "Permanent Redirect"},    // 永久重定向
    {"400", "Bad Request"},           // 错误请求
    {"401", "Unauthorized"},          // 未授权
    {"403", "Forbidden"},             // 禁止
    {"404", "Not Found"},             // 找不到
    {"405", "Method Not Allowed"},    // 方法不允许
    {"408", "Request Timeout"},       // 请求超时
    {"429", "Too Many Requests"},     // 请求过多
    {"500", "Internal Server Error"}, // 内部服务器错误
    {"501", "Not Implemented"},       // 未实现
    {"502", "Bad Gateway"},           // 错误网关
    {"503", "Service Unavailable"},   // 服务不可用
    {"504", "Gateway Timeout"}        // 网关超时
};

std::unordered_map<std::string, std::string> httpHeader::params_200 = {
    {"http_version", HTTP_VERSION},
    {"status", "200"},
    {"Server", SERVER_NAME},
};

std::unordered_map<std::string, std::string> httpHeader::params_400 = {
    {"http_version", HTTP_VERSION},
    {"status", "400"},
    {"Server", SERVER_NAME},
    {"Content-Type", "text/html"}};

std::unordered_map<std::string, std::string> httpHeader::params_404 = {
    {"http_version", HTTP_VERSION},
    {"status", "404"},
    {"Server", SERVER_NAME},
    {"Content-Type", "text/html"}};

std::unordered_map<std::string, std::string> httpHeader::params_500 = {
    {"http_version", HTTP_VERSION},
    {"status", "500"},
    {"Server", SERVER_NAME},
    {"Content-Type", "text/html"}};

#endif