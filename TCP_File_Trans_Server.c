#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/time.h>

#pragma pack(push, 1)


typedef struct 
{
	char name[101];  // 文件名最大长度不超过 100 字节
	uint32_t mode;   // 文件模式
	uint64_t size;   // 文件大小
	// ......        // 更多文件属性可以扩展	

} file_info_t;


#pragma pack(pop)



typedef struct
{
	int sock_conn;
	char ip[16];
	unsigned short port;
	time_t online_time;     // 上线时间
	char** send_file_list;  // 待发送的文件路径列表
	int send_file_cnt;      // 待发送的文件数量
	// char user_name[50];  // 用户名
	//......

} client_info_t;



void* comm_thr(void* arg);
int send_file(int sock, const char* file_path);



int main(int argc, char** argv)
{
	signal(SIGPIPE, SIG_IGN);

	// 校验参数
	if(argc == 1)
	{
		fprintf(stderr, "Usage: %s file1 [...]\n", argv[0]);
		return 1;
	}

	for(int i = 1; i < argc; i++)
	{
		if(-1 == access(argv[i], R_OK))
		{
			fprintf(stderr, "文件 %s 不存在或不可读！\n", argv[i]);
			return 1;
		}
	}

	// 第 1 步：创建监听套接字
	int sock_listen = socket(AF_INET, SOCK_STREAM, 0);

	if(-1 == sock_listen)
	{
		perror("socket fail");
		exit(1);
	}


	// 开启地址复用，以允许服务器快速重启
	int val = 1;
	setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));


	// 第 2 步：绑定地址

	// 指定地址
	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;          // 指定地址家族(AF)为 Internet 地址家族
	myaddr.sin_addr.s_addr = INADDR_ANY;  // 指定 IP 地址为本机任意地址
	//myaddr.sin_addr.s_addr = inet_addr("172.16.251.96");  // 指定 IP 地址为本机的某个具体 IP 地址
	myaddr.sin_port = htons(9413);        // 指定端口号为 9413
	
	//printf("%hu\n", htons(6666));  // 2586

	// 绑定
	if(-1 == bind(sock_listen, (struct sockaddr*)&myaddr, sizeof(myaddr)))
	{
		perror("bind fail");
		exit(1);
	}


	// 第 3 步：监听
	if(-1 == listen(sock_listen, 5))
	{
		perror("listen fail");
		exit(1);
	}

	// 第 4 步：接收客户端连接请求
	
	int sock_conn;
	pthread_t tid;
	client_info_t* pci = NULL;
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);

	struct timeval tv;
	tv.tv_sec = 10;
	tv.tv_usec = 0;


	while(1)
	{	
		sock_conn = accept(sock_listen, (struct sockaddr*)&client_addr, &addr_len);

		if(-1 == sock_conn)
		{
			perror("accept fail");
			exit(1);
		}

		pci = malloc(sizeof(client_info_t));

		if(NULL == pci)
		{
			perror("malloc fail");
			close(sock_conn);
			continue;
		}

		// 获取当前上线的客户端信息
		pci->sock_conn = sock_conn;
		strcpy(pci->ip, inet_ntoa(client_addr.sin_addr));
		pci->port = ntohs(client_addr.sin_port);
		pci->online_time = time(NULL);
		pci->send_file_list = argv + 1;
		pci->send_file_cnt = argc - 1;
		
		if(pthread_create(&tid, NULL, comm_thr, pci))
		{
			perror("pthread_create fail");

			free(pci);
			close(sock_conn);
			continue;
		}

		// 设置接收超时
		setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	// 第 7 步：关闭监听套接字
	close(sock_listen);

	return 0;
}



// 定义通信线程函数
void* comm_thr(void* arg)
{
	client_info_t* pci = (client_info_t*)arg;
	int i, err_code = 0;

	pthread_detach(pthread_self());

	printf("\n客户端(%s:%hu)上线...\n", pci->ip, pci->port);

	// 第 5 步：收发数据
	for(i = 0; i < pci->send_file_cnt; i++)
	{
		if((err_code = send_file(pci->sock_conn, pci->send_file_list[i])) != 0)
		{
			printf("\n向客户端(%s:%hu)发送 %s 文件失败(Error code: %d)！\n", pci->ip, pci->port, pci->send_file_list[i], err_code);
			break;
		}
		else
		{
			printf("\n向客户端(%s:%hu)发送 %s 文件成功！\n", pci->ip, pci->port, pci->send_file_list[i]);
		}
	}

	// 第 6 步：断开连接（关闭连接套接字）
	close(pci->sock_conn);

	printf("\n客户端(%s:%hu)下线！\n", pci->ip, pci->port);

	free(pci);

	return NULL;
}



// 将指定文件发送给客户端
int send_file(int sock, const char* file_path)
{
	file_info_t fi = {""};
	const char* file_name = NULL;
	struct stat st;
	int fd, ret;
	char buff[1024];
	uint64_t send_cnt = 0;


	// 获取文件模式和大小
	if(lstat(file_path, &st) == -1)
	{
		perror("lstat fail");
		return 1;
	}

	fi.mode  = st.st_mode;
	fi.size  = st.st_size;

	// 获取文件名（不含路径）
	file_name = strrchr(file_path, '/');

	if(file_name == NULL)
		file_name = file_path;
	else
		file_name++;

	strncpy(fi.name, file_name, sizeof(fi.name) - 1);

	// 发送文件属性信息
	if(write(sock, &fi, sizeof(fi)) != sizeof(fi))
	{
		fprintf(stderr, "send file attribute fail\n");
		return 2;
	}

	// 发送文件数据内容
	fd = open(file_path, O_RDONLY);

	if(fd == -1)
	{
		perror("open fail");
		return 3;
	}

	while((ret = read(fd, buff, sizeof(buff))) > 0)
	{
			if(write(sock, buff, ret) != ret)
				break;

			send_cnt += ret;
	}

	close(fd);

	if(send_cnt != fi.size)
	{
		fprintf(stderr, "send file data fail\n");
		return 4;		
	}

	return 0;  // 发送文件成功
}


