#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NAME_LEN 128
struct Client {
  char name[NAME_LEN]; // 客户端login的时候使用的别名,
                       // 可以再次向服务器发送sign指令更改
  struct sockaddr_storage addr; // 客户端的地址
  char message[BUFSIZ]; // 积压的消息队列, 客户端发送pull指令的时候返回并清空
  // char password[128];
};

// 创建一个用来记录客户端信息的数组, 到时候直接根据name查ip
#define LIST_SIZE 100
static struct Client client_list[LIST_SIZE];
static int instant_client_len = 0; // 当前客户端数组有效长度

// 拿出一个数组存放位于各个线程的客户端的信息, 避免误操作
// sockaddr_storage是一个比sockaddr_in更普遍的存储ip地址的结构 同时支持ipv6
#define SOCK_POOL_LEN 10
static struct sockaddr_storage clnt_addr[SOCK_POOL_LEN];
static int vacant_sock_storage[SOCK_POOL_LEN] = {0}; // 0:空闲 1:占用中
static int sock_addr[SOCK_POOL_LEN] = {0};           // 套接字id

void *thread_callback(void *args) {
  // 我也不知道为什么它莫名加了 1 ...
  int sock_index = (int *)args;
  int clifd = sock_addr[sock_index];
  struct sockaddr_storage client_addr = clnt_addr[sock_index];

  char buffer[BUFSIZ] = {0};
  int strLen = read(clifd, buffer, BUFSIZ); //接收客户端发来的数据

  char command[5]; // pull / send / sign
  // strncpy(command, buffer, 4);
  char param[NAME_LEN]; // 其余部分
  char msg[BUFSIZ];
  sscanf(buffer, "%s %s %s", command, param, msg);
  command[4] = '\0';

  printf("sock index t : %d\n", sock_index);
  printf("clint sock t: %d\n", clifd);
  printf("command : %s\n", command);
  printf("msg: %s\n", msg);
  printf("param: %s\n", param);
  fflush(stdout);

  int i;
  if (strcmp(command, "pull") == 0) { // 查看发给自己的数据
    if (instant_client_len == 0) {
      strcpy(buffer, "you have not signed up yet ~\n");
    } else {
      for (i = 0; i < instant_client_len; i++) {
        if (client_addr.__ss_align == client_list[i].addr.__ss_align) {
          strcpy(buffer, client_list[i].message);
          strcpy(client_list[i].message, ""); // 拉取后清零
          break;
        }
        if (i == instant_client_len - 1) {
          strcpy(buffer, "you have not signed up yet ~\n");
        }
      }
    }
  } else if (strcmp(command, "sign") == 0) { // 注册账户
    if (instant_client_len == 0) {
      client_list[instant_client_len].addr = client_addr;
      strcpy(client_list[instant_client_len].name, param);
      strcpy(client_list[instant_client_len].message, "");
      instant_client_len++;
    } else {
      for (i = 0; i < instant_client_len; i++) {

        if (client_addr.__ss_align == client_list[i].addr.__ss_align) {
          strcpy(client_list[i].name, param);
          break;
        }
        if (i == instant_client_len - 1) {
          client_list[instant_client_len].addr = client_addr;
          strcpy(client_list[instant_client_len].name, param);
          strcpy(client_list[instant_client_len].message, "");
          instant_client_len++;
        }
      }
    }
    strcpy(buffer, "welcome~ ");
  } else if (strcmp(command, "send") == 0) {

    if (instant_client_len == 0) {
      strcpy(buffer, "you have not signed up yet ~\n");
    } else {
      for (i = 0; i < instant_client_len; i++) {
        if (client_addr.__ss_align == client_list[i].addr.__ss_align) {
          char myname[NAME_LEN];
          strcpy(myname, client_list[i].name);
          strcat(myname, ":");
          strcat(myname, msg);
          strcpy(msg, myname);
          strcat(msg, "\n");
          int j;
          for (j = 0; j < instant_client_len; j++) {
            if (strcmp(param, client_list[i].name) == 0) {
              strcat(client_list[i].message, msg);
              strcpy(buffer, "message sent\n");
              break;
            }
            if (i == instant_client_len - 1) {
              strcpy(buffer, "user do not exist");
            }
          }
          break;
        }
        if (i == instant_client_len - 1) {
          strcpy(buffer, "you have not signed up yet ~\n");
        }
      }
    }

  } else {
    strcpy(buffer, "invaild command");
  }

  // 说明:向客户端发送数据
  // write()/send() 并不立即向网络中传输数据，
  // 而是先将数据写入缓冲区中，再由 TCP 协议将数据从缓冲区发送到目标机器。
  // 一旦将数据写入到缓冲区，函数就可以成功返回
  /**  以下代码可以查看缓存区大小
  int optVal;
  socklen_t optLen = sizeof(int);
  getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &optVal, &optLen);
  printf("Buffer length: %d\n", optVal);
  */
  printf("response buffer: %s\n", buffer);
  fflush(stdout);

  write(clifd, buffer, strLen);
  close(clifd);              //通信结束，关闭通信套接字
  memset(buffer, 0, BUFSIZ); //重置缓冲区

  vacant_sock_storage[sock_index] = 0; // 将这个客户端存储位重设为空闲

  return NULL;
}

int main() {

  memset(client_list, 0, LIST_SIZE); //重置client_list

  //创建套接字
  int serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr)); //每个字节都用0填充
  serv_addr.sin_family = AF_INET;           //使用IPv4地址
  // inet_addr 函数的作用即为将此字符串转换为 int
  // 类型，同时将主机字节序转换为网络字节序(intel的小端转网络大端)
  serv_addr.sin_addr.s_addr = inet_addr("0.0.0.0"); //具体的IP地址
  serv_addr.sin_port = htons(1234);                 //端口

  printf("server starting...\n");
  fflush(stdout);

  // 操作系统要求一个绑定延时，在上次响应后的 30秒,
  // 任何程序都不能重新绑定这个端口 但有时候我们希望能够快速重新启动程序
  // 于是这里为服务端套接字通过 setsockopt 设置选项。SOL_SOCKET 代表 socket
  // 层自身，SO_REUSEADDR 表示可重用端口
  int reuse = 1;
  if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) ==
      -1) {
    printf("error!%s", strerror(errno));
    return -1;
  }

  //将套接字和IP、端口绑定
  if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
    printf("error!%s", strerror(errno));
    return -1;
  }

  //进入监听状态，等待用户发起请求
  listen(serv_sock, 20);

  // struct sockaddr_storage clnt_addr[100];

 
  while (1) {

    // 寻找空闲的sockaddr_storage容器
    int sock_index;
    struct sockaddr_storage *current_clnt_addr = &clnt_addr[0];
    for (sock_index = 0; sock_index < SOCK_POOL_LEN; sock_index++) {
      if (vacant_sock_storage[sock_index] == 0) {
        current_clnt_addr = &clnt_addr[sock_index];
        vacant_sock_storage[sock_index] = 1;
        break;
      }
    }

    socklen_t clnt_addr_size = sizeof(*current_clnt_addr);
    int clnt_sock = accept(serv_sock, (struct sockaddr *)current_clnt_addr,
                           &clnt_addr_size); //接收客户端请求

    printf("sock index %d\n", sock_index);
    printf("clint sock m: %d\n", clnt_sock);
    fflush(stdout);

    pthread_t t0;

    sock_addr[sock_index] = clnt_sock;

    pthread_create(&t0, NULL, thread_callback, sock_index);
  }

  close(serv_sock);

  return 0;
}