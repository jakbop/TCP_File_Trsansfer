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
#include <sys/stat.h>

#pragma pack(push, 1)
typedef struct 
{
	char name[101];
	uint32_t mode;
	uint64_t size;
	uint64_t offset;
	uint8_t enable_resume;
} file_info_t;
#pragma pack(pop)

int recv_file_with_offset(int sock, const char* file_name, uint64_t offset, uint64_t file_size);

int main(int argc, char* argv[])
{
	signal(SIGPIPE, SIG_IGN);

	if(argc != 2)
	{
		fprintf(stderr, "用法: %s <服务端IP>\n", argv[0]);
		return 1;
	}

	int sock_cli = socket(AF_INET, SOCK_STREAM, 0);
	if(-1 == sock_cli)
	{
		perror("socket 创建失败");
		exit(1);
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9413);
	if(inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
	{
		perror("IP地址格式错误");
		close(sock_cli);
		exit(1);
	}

	if(connect(sock_cli, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
	{
		perror("连接服务端失败");
		close(sock_cli);
		exit(1);
	}
	printf("成功连接到服务端 %s:9413，开始接收文件...\n", argv[1]);

	while(1)
	{
		file_info_t fi;
		ssize_t ret = read(sock_cli, &fi, sizeof(fi));
		if(ret != sizeof(fi))
		{
			if(ret == 0)
				printf("\n服务端已断开连接\n");
			else
				perror("\n接收文件信息失败");
			break;
		}

		printf("\n----------------------------------------\n");
		printf("开始接收文件：%s\n", fi.name);
		printf("文件大小：%lu 字节\n", fi.size);
		printf("断点续传：%s\n", fi.enable_resume ? "支持" : "不支持");

		uint64_t existing_size = 0;
		struct stat st;
		if(stat(fi.name, &st) == 0)
		{
			existing_size = st.st_size;
			if(existing_size < fi.size)
			{
				printf("检测到未完成的下载，已下载 %lu 字节 (%.2f%%)，从断点继续...\n", 
				       existing_size, (double)existing_size / fi.size * 100);
			}
			else if(existing_size == fi.size)
			{
				printf("文件已完整存在，跳过下载\n");
				file_info_t req;
				memset(&req, 0, sizeof(req));
				req.offset = fi.size;
				write(sock_cli, &req, sizeof(req));
				continue;
			}
			else
			{
				printf("本地文件大于服务端文件，重新下载\n");
				existing_size = 0;
			}
		}
		else
		{
			printf("新文件下载，从头开始\n");
		}

		file_info_t req;
		memset(&req, 0, sizeof(req));
		req.offset = existing_size;
		if(write(sock_cli, &req, sizeof(req)) != sizeof(req))
		{
			perror("发送请求失败");
			break;
		}

		int err_code = recv_file_with_offset(sock_cli, fi.name, existing_size, fi.size);

		if(err_code != 0)
		{
			printf("文件接收异常，错误码：%d\n", err_code);
			break;
		}
	}

	close(sock_cli);
	printf("客户端程序退出\n");
	return 0;
}

int recv_file_with_offset(int sock, const char* file_name, uint64_t offset, uint64_t file_size)
{
	char buff[8192];
	int fd;
	ssize_t ret;
	uint64_t recv_cnt = 0;
	uint64_t total_to_recv = file_size - offset;

	if(offset == 0)
	{
		fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0664);
	}
	else
	{
		fd = open(file_name, O_WRONLY | O_APPEND);
	}

	if(fd == -1)
	{
		perror("创建/打开文件失败");
		return 1;
	}

	while(recv_cnt < total_to_recv)
	{
		size_t need_recv = (total_to_recv - recv_cnt) > sizeof(buff) ? sizeof(buff) : (total_to_recv - recv_cnt);
		ret = read(sock, buff, need_recv);

		if(ret <= 0)
		{
			perror("接收文件数据失败");
			close(fd);
			return 2;
		}

		if(write(fd, buff, ret) != ret)
		{
			perror("写入文件失败");
			close(fd);
			return 3;
		}

		recv_cnt += ret;
		printf("\r接收进度: %.2f %%", (double)(offset + recv_cnt) / file_size * 100);
		fflush(stdout);
	}

	close(fd);
	printf("\n文件 %s 接收完成！\n", file_name);

	return 0;
}
