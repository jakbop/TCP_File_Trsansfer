#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// 与服务端完全一致的结构体定义（必须保证内存布局相同）
#pragma pack(push, 1)
typedef struct 
{
    char name[101];  // 文件名
    uint32_t mode;   // 文件模式
    uint64_t size;   // 文件大小
} file_info_t;
#pragma pack(pop)

// 函数声明
int recv_file(int sock);

int main(int argc, char *argv[])
{
    // 忽略SIGPIPE信号，防止连接断开时程序崩溃
    signal(SIGPIPE, SIG_IGN);

    // 校验参数：需要传入服务端IP
    if (argc != 2)
    {
        fprintf(stderr, "用法: %s <服务端IP>\n", argv[0]);
        return 1;
    }

    // 1. 创建客户端套接字
    int sock_cli = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sock_cli)
    {
        perror("socket 创建失败");
        exit(1);
    }

    // 2. 配置服务端地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9413);  // 与服务端端口一致
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        perror("IP地址格式错误");
        close(sock_cli);
        exit(1);
    }

    // 3. 连接服务端
    if (connect(sock_cli, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("连接服务端失败");
        close(sock_cli);
        exit(1);
    }
    printf("成功连接到服务端 %s:9413，开始接收文件...\n", argv[1]);

    // 循环接收文件（服务端会依次发送所有文件）
    while (1)
    {
        int ret = recv_file(sock_cli);
        if (ret != 0)
        {
            // 连接断开或接收失败，退出循环
            if (ret == -1)
                printf("服务端已断开连接\n");
            else
                printf("文件接收异常，错误码：%d\n", ret);
            break;
        }
        printf("----------------------------------------\n");
    }

    // 4. 关闭套接字
    close(sock_cli);
    printf("客户端程序退出\n");
    return 0;
}

// 接收单个文件的核心函数
int recv_file(int sock)
{
    file_info_t fi;
    char buff[1024];
    int fd;
    ssize_t ret;
    uint64_t recv_cnt = 0;

    // 1. 先接收文件信息结构体
    ret = read(sock, &fi, sizeof(fi));
    if (ret != sizeof(fi))
    {
        // 连接断开
        if (ret == 0)
            return -1;
        perror("接收文件信息失败");
        return 1;
    }

    printf("开始接收文件：%s\n", fi.name);
    printf("文件大小：%lu 字节\n", fi.size);

    // 2. 创建文件，准备写入
    fd = open(fi.name, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd == -1)
    {
        perror("创建文件失败");
        return 2;
    }

    // 3. 循环接收文件数据并写入
    while (recv_cnt < fi.size)
    {
        // 计算本次需要接收的字节数
        size_t need_recv = (fi.size - recv_cnt) > sizeof(buff) ? sizeof(buff) : (fi.size - recv_cnt);
        ret = read(sock, buff, need_recv);

        if (ret <= 0)
        {
            perror("接收文件数据失败");
            close(fd);
            return 3;
        }

        // 写入文件
        if (write(fd, buff, ret) != ret)
        {
            perror("写入文件失败");
            close(fd);
            return 4;
        }

        recv_cnt += ret;
        // 打印接收进度
        printf("\r已接收：%.2f %%", (double)recv_cnt / fi.size * 100);
        fflush(stdout);
    }

    // 收尾
    close(fd);
    printf("\n文件 %s 接收完成！总大小：%lu 字节\n", fi.name, recv_cnt);

    return 0;
}
