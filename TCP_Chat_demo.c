#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>


int main()
{
	signal(SIGPIPE, SIG_IGN);

	// 第 1 步：创建监听套接字
	int sock_listen = socket(AF_INET, SOCK_STREAM, 0);

	if(-1 == sock_listen)
	{
		perror("socket fail");
		exit(1);
	}


	// 第 2 步：绑定地址

	// 指定地址
	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;          // 指定地址家族(AF)为 Internet 地址家族
	myaddr.sin_addr.s_addr = INADDR_ANY;  // 指定 IP 地址为本机任意地址
	//myaddr.sin_addr.s_addr = inet_addr("172.16.251.96");  // 指定 IP 地址为本机的某个具体 IP 地址
	myaddr.sin_port = htons(7788);        // 指定端口号为 6666
	
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
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);

	struct timeval tv;
	tv.tv_sec = 60;
	tv.tv_usec = 0;


	while(1)
	{	
		sock_conn = accept(sock_listen, (struct sockaddr*)&client_addr, &addr_len);

		if(-1 == sock_conn)
		{
			perror("accept fail");
			exit(1);
		}

		printf("\n客户端(%s:%hu)上线啦~\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		// 第 5 步：收发数据
		while(1)
		{
			int ret;

			// 发送数据
			char msg[1025] = "";

			printf("我说：");
			scanf("%1024[^\n]", msg);

			while(getchar() != '\n');

			//write(sock_conn, msg, strlen(msg));   // 发送数据
			send(sock_conn, msg, strlen(msg), 0); // 发送数据，最后一个参数传 0 表示以默认方式发送数据，和上面的代码等效

			// 斜杠命令 /exit
			if(strcmp(msg, "/exit") == 0)
			{
				printf("\n服务器主动结束聊天！\n");
				break;
			}

			// 接收数据
			//ret = read(sock_conn, msg, sizeof(msg) - 1);   // 接收数据
			ret = recv(sock_conn, msg, sizeof(msg) - 1, 0);  // 接收数据，最后一个参数传 0 表示以默认方式接收，和上面代码等效

			if(ret > 0)
			{
				msg[ret] = '\0';
	
				// 斜杠命令 /exit
				if(strcmp(msg, "/exit") == 0)
				{
					printf("\n客户端主动结束聊天！\n");
					break;
				}

				printf("\n他说：%s\n", msg);
			}
			else
			{
				printf("\n连接已断开！\n");
				break;
			}
		}


		// 第 6 步：断开连接（关闭连接套接字）
		close(sock_conn);
	}

	// 第 7 步：关闭监听套接字
	close(sock_listen);

	return 0;
}

