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

typedef struct
{
	int sock_conn;
	char ip[16];
	unsigned short port;
	time_t online_time;
	char** send_file_list;
	int send_file_cnt;
} client_info_t;

void* comm_thr(void* arg);
int send_file_with_offset(int sock, const char* file_path, uint64_t offset);

int main(int argc, char** argv)
{
	signal(SIGPIPE, SIG_IGN);

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

	int sock_listen = socket(AF_INET, SOCK_STREAM, 0);

	if(-1 == sock_listen)
	{
		perror("socket fail");
		exit(1);
	}

	int val = 1;
	setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = INADDR_ANY;
	myaddr.sin_port = htons(9413);

	if(-1 == bind(sock_listen, (struct sockaddr*)&myaddr, sizeof(myaddr)))
	{
		perror("bind fail");
		exit(1);
	}

	if(-1 == listen(sock_listen, 5))
	{
		perror("listen fail");
		exit(1);
	}

	int sock_conn;
	pthread_t tid;
	client_info_t* pci = NULL;
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);

	struct timeval tv;
	tv.tv_sec = 30;
	tv.tv_usec = 0;

	printf("服务端已启动，监听端口 9413...\n");
	printf("待发送文件: ");
	for(int i = 1; i < argc; i++)
		printf("%s ", argv[i]);
	printf("\n");

	while(1)
	{	
		sock_conn = accept(sock_listen, (struct sockaddr*)&client_addr, &addr_len);

		if(-1 == sock_conn)
		{
			perror("accept fail");
			continue;
		}

		pci = malloc(sizeof(client_info_t));

		if(NULL == pci)
		{
			perror("malloc fail");
			close(sock_conn);
			continue;
		}

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

		setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	close(sock_listen);
	return 0;
}

void* comm_thr(void* arg)
{
	client_info_t* pci = (client_info_t*)arg;
	int i, err_code = 0;

	pthread_detach(pthread_self());

	printf("\n客户端(%s:%hu)上线...\n", pci->ip, pci->port);

	for(i = 0; i < pci->send_file_cnt; i++)
	{
		file_info_t fi;
		ssize_t ret;

		memset(&fi, 0, sizeof(fi));

		struct stat st;
		if(lstat(pci->send_file_list[i], &st) == -1)
		{
			perror("lstat fail");
			err_code = 1;
			break;
		}

		fi.mode = st.st_mode;
		fi.size = st.st_size;
		fi.enable_resume = 1;

		const char* file_name = strrchr(pci->send_file_list[i], '/');
		if(file_name == NULL)
			file_name = pci->send_file_list[i];
		else
			file_name++;
		strncpy(fi.name, file_name, sizeof(fi.name) - 1);

		printf("\n准备发送文件: %s (%lu 字节)\n", fi.name, fi.size);

		if(write(pci->sock_conn, &fi, sizeof(fi)) != sizeof(fi))
		{
			fprintf(stderr, "send file info fail\n");
			err_code = 2;
			break;
		}

		file_info_t client_req;
		ret = read(pci->sock_conn, &client_req, sizeof(client_req));
		if(ret != sizeof(client_req))
		{
			fprintf(stderr, "recv client request fail\n");
			err_code = 3;
			break;
		}

		printf("客户端请求偏移: %lu 字节\n", client_req.offset);

		if(client_req.offset >= fi.size)
		{
			printf("文件已完整，跳过发送\n");
			continue;
		}

		err_code = send_file_with_offset(pci->sock_conn, pci->send_file_list[i], client_req.offset);

		if(err_code != 0)
		{
			printf("向客户端(%s:%hu)发送 %s 文件失败(Error code: %d)！\n", 
			       pci->ip, pci->port, pci->send_file_list[i], err_code);
			break;
		}
		else
		{
			printf("向客户端(%s:%hu)发送 %s 文件成功！\n", 
			       pci->ip, pci->port, pci->send_file_list[i]);
		}
	}

	close(pci->sock_conn);
	printf("客户端(%s:%hu)下线！\n", pci->ip, pci->port);
	free(pci);

	return NULL;
}

int send_file_with_offset(int sock, const char* file_path, uint64_t offset)
{
	struct stat st;
	if(lstat(file_path, &st) == -1)
	{
		perror("lstat fail");
		return 1;
	}

	int fd = open(file_path, O_RDONLY);
	if(fd == -1)
	{
		perror("open fail");
		return 2;
	}

	if(lseek(fd, offset, SEEK_SET) == -1)
	{
		perror("lseek fail");
		close(fd);
		return 3;
	}

	char buff[8192];
	uint64_t send_cnt = 0;
	uint64_t total_to_send = st.st_size - offset;
	ssize_t ret;

	while(send_cnt < total_to_send)
	{
		size_t to_read = (total_to_send - send_cnt) > sizeof(buff) ? sizeof(buff) : (total_to_send - send_cnt);
		ret = read(fd, buff, to_read);
		if(ret <= 0)
			break;

		if(write(sock, buff, ret) != ret)
			break;

		send_cnt += ret;
		printf("\r发送进度: %.2f %%", (double)(offset + send_cnt) / st.st_size * 100);
		fflush(stdout);
	}

	close(fd);
	printf("\n");

	if(send_cnt != total_to_send)
	{
		fprintf(stderr, "send file data fail\n");
		return 4;		
	}

	return 0;
}
