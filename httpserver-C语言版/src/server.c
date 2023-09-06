#include "server.h"
#include <stdio.h>			// sscanf(),perror(),sprintf()
#include <arpa/inet.h>		// socket系列
#include <sys/epoll.h>		// epoll系列
#include <fcntl.h>			// 边沿非阻塞 fcntl()
#include <errno.h>			// errno,EAGAIN
#include <strings.h>		// strcasecmp():不区分大小写
#include <string.h>			// memcpy(),memset(),strcmp(),strrchr(),strstr()
#include <sys/stat.h>       // 文件属性系列
#include <assert.h>         // 断言
#include <sys/sendfile.h>   // sendfile()
#include <sys/types.h>      // lseek()
#include <unistd.h>         // lseek(),chdir()
#include <dirent.h>
#include <stdlib.h>         // atoi(), malloc() free()
#include <ctype.h>
#include <pthread.h>


typedef struct SocketInfo {
    int fd;   //监听的或者通信的文件描述符
    int epfd; //epoll树的根节点
    pthread_t tid;  //对应线程
} SocketInfo;

// 初始化用于监听的套接字
int initListenFd(unsigned short port) {
    //1.创建监听的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0); //1.1 ipv4，流式协议，默认tcp
    if (fd == -1) { //1.2 失败都是返回-1
        perror("socket"); //1.3 perror: print a system error message
        return -1;
    }

    //add1.设置端口复用
    int opt = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret == -1) {
        perror("setsockopt");
        return -1;
    }

    //2.绑定本地的IP port
    struct sockaddr_in saddr; //2.2 IP端口信息，使用sockaddr_in结构，再强制类型转换——这个信息需要转换大小端（字节序）
    saddr.sin_family = AF_INET; //2.2.1 地址族协议，选择ipv4
    saddr.sin_port = htons(port);  //2.2.2 端口，选一个未被使用的，5000以上基本上就可以满足，htons：host to net short
    saddr.sin_addr.s_addr = INADDR_ANY; //2.2.3 ip地址，0 = 0.0.0.0 对于0来说，大端和小端没有区别，所以不需要转换。INADDR_ANY可以绑定本地任意ip地址，就会绑定本地网卡实际ip地址
    
    ret = bind(fd, (struct sockaddr*)&saddr, sizeof(saddr)); //2.1 socket、本地ip
    if (ret == -1) {
        perror("bind");
        return -1;
    }

    //3. 设置监听
    ret = listen(fd, 128); //3.1 socekt，最大连接请求
    if (ret == -1) {
        perror("listen");
        return -1;
    }

    return fd; // 返回监听的fd
}

// 启动epoll--传入监听fd
int epollRun(int fd) {
    //1 创建epoll实例
    int epfd = epoll_create(100); //参数>0即可
    if (epfd == -1) {
        //失败了
        perror("epoll_create");
        exit(0);
    }

    //2.lfd上树
    struct epoll_event ev; //8.3.1委托事件
    ev.events = EPOLLIN | EPOLLET; //9.2 设定边沿模式——监听的fd可以不改
    ev.data.fd = fd;  //8.3.2 一般没有特殊情况就是fd，说明是该fd调用的
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev); //8.3 将监听套接字添加到epoll中，epoll、op、fd、委托epoll的事件
    if (ret == -1) //8.3.3 epoll_ctl也有返回值，这里判断
	{
		perror("epoll_ctl");
		return -1;
	}
    
    //3.检测
    struct epoll_event evs[1024];
    int size = sizeof(evs)/sizeof(evs[0]);
    while (1) {
        int num = epoll_wait(epfd, evs, size, -1); //8.4 阻塞（-1）等待
        // printf("num = %d\n", num);
        for (int i = 0; i < num; i++) {
            int eventfd = evs[i].data.fd; //8.4.1 该fd就是触发的fd
            SocketInfo *info = (SocketInfo *)malloc(sizeof(SocketInfo)); //这里用堆，保证子线程运行过程中info还在其生命周期内
            info->fd = eventfd;
            info->epfd = epfd;
            if (eventfd == fd) { //8.4.2 说明是监听的fd
                //10.2 线程1
                // 建立新连接 accept
                pthread_create(&info->tid, NULL, acceptConn, info);
                // pthread_detach(tid);
            } else {
                //10.3
                // 主要是接受对端的数据--recvHttpRequset
                pthread_create(&info->tid, NULL, recvHttpRequset, info);
                // pthread_detach(tid);
            }
        }
    }
}

// 和客户端建立连接
void* acceptConn(void* arg) {
    SocketInfo *info = (SocketInfo*)arg;
    
    //1.建立连接
    struct sockaddr_in caddr;
    int addrlen = sizeof(caddr);
    int cfd = accept(info->fd, (struct sockaddr*)&caddr, &addrlen); //4.1 监听socket、客户端地址信息（传出），长度（传入传出）

    //4.2 这里返回的cfd：用于通信的文件描述符！！
    if (cfd == -1) {
        perror("accept");
        free(info);
        return NULL;
    }
    
    //2.设置fd的非阻塞属性
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;  //字母O大写，非阻塞
    fcntl(cfd, F_SETFL, flag);

    //3.添加通信cfd
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; //9.2.1 设置通信fd为边沿触发模式
    ev.data.fd = cfd;
    int ret = epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev); // ev传进去是值传递，可以复用之前的
    if (ret == -1)
	{
		perror("epoll_ctl");
		return NULL;
	}

    printf("acceptConn...self:%ld, tid: %ld\n", pthread_self(), info->tid);
    free(info);
    return NULL;
}

/*-------------------------------------- 接受http请求 ------------------------------------------------*/
void* recvHttpRequset(void* arg) {
    SocketInfo *info = (SocketInfo*)arg;
    printf("start recvHttpRequset...self:%ld, tid: %ld\n", pthread_self(), info->tid);
    
    //接受数据
    int len = 0, total = 0;
    char tmp[1024];
    char buff[4096];


    while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0) { // 5.1.0 这里recv会一直阻塞，直到接受到数据（接收到0就是客户端断开）
        if (total + len <= sizeof(buff)) {
            memcpy(buff+total, tmp, len); //memcpy(void *dest, const void *src, size_t n);
            memset(tmp, 0, sizeof(tmp));
        }
        total += len;
    }
    
    if (len == -1 && errno == EAGAIN) { //9.6 读完了，提示Resource temporarily unavaliable的错误
        //解析请求行
        char* pt = strstr(buff, "\r\n"); //strstr 在一个字符串中查找另一个子字符串的第一次出现。它返回指向第一次出现的子字符串的指针，如果未找到则返回 NULL。
        int reqLen = pt - buff; // 指针相减，得到长度
        buff[reqLen] = '\0';
        parseRequestLine(buff, info->fd); //传递以\0结尾的请求行
    }
    else if (len == 0) { //5.1.2 长度==0表示客户端断开连接
        printf("client unconnect...\n");
        //6.4.1 断开连接
        epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
        close(info->fd);  //8.6先删除，再关闭（不然会返回-1）
    }
    else { //5.1.3 len<0表示接受失败
        perror("recerve fail...\n");
    }

    printf("end recvHttpRequset...self:%ld, tid: %ld\n", pthread_self(), info->tid);
    free(info);
    return NULL;
}


/*-------------------------------------- 处理http请求 ------------------------------------------------*/
//解析请求行
int parseRequestLine(const char* line, int cfd) {
    printf("start parseRequestLine...self:%ld, line:%s\n", pthread_self(), line);
    //1.解析请求行
	char method[12] = { 0 };
	char path[1024] = { 0 };
	char version[16] = { 0 };
    sscanf(line, "%[^ ] %[^ ] %[^\r\n]", method, path, version);
    printf("sscanf-line11111, method: %s, path: %s, version:%s\n", method, path, line);
    decodeMsg(path, path);
    printf("sscanf-line, method: %s, path: %s, version:%s\n", method, path, line);

    //2.解析客户端请求的静态资源（目录或文件）
    if (strcasecmp(method, "get") != 0)  // 比较两个字符串（不区分大小写）返回字典序排序比较
	{
		return -1;
	}
    char* file = NULL;
	if (strcmp(path, "/") == 0)  // 比较两个字符串（区分大小写）。返回字典序排序比较
	{
		file = "./";     //请求的是资源目录
	}
	else
	{
		file = path + 1; //请求的是资源文件
	}

    /*-------------------------------------- 响应http请求 ------------------------------------------------*/
    // 获取文件属性
	struct stat st; // 于表示文件或目录的状态信息的结构体。
    int ret = stat(file, &st);
    if (ret == -1)
	{
		// 文件不存在 --回复404
		sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return 0;
	}
    // 判断文件类型
    if (S_ISDIR(st.st_mode)) // 文件的访问权限和类型
	{
		// 把这个目录中的内容发送给客户端
		printf("The request is for a directory...\n");
		sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}

}

//发送文件
int sendFile(const char* fileName, int cfd) {
	// 1.打开文件
	printf("fileName: %s\n", fileName);
	int fd = open(fileName, O_RDONLY); //只读方式打开

	if (fd <= 0)
	{
		printf("errno=%d, file fd: %d\n", errno, fd);
		perror("open");
	}
	assert(fd > 0); //断言
	printf("Start sending a File...\n");
    
	// 每次从文件中读1k,发送1k数据。
	while (1)
	{
		char buf[1024] = {0};
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, MSG_NOSIGNAL);
			usleep(10); //发送数据慢一些，留给浏览器处理数据的时间
		}
		else if (len == 0)
		{
			break;      //文件发送完了
		}
		else
		{
			perror("read"); //异常
		}
	}
	printf("END sending a File...\n");
	return 0;
}

//获取响应文件类型
const char* getFileType(const char* name) {
    
	// a.jpg, a.mp4, a.html........
	// 自右向左查找'.'字符， 如果不存在返回null
    const char* dot = strrchr(name, '.');
    if (dot == NULL) return "text/plain; charset=utf-8"; //纯文本

    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
	  return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/X-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/X-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}

// 除数据块外，发送http响应（状态行+响应头+空行）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length) {
    printf("START Response status line, header and blank line... \n");
	// 状态行
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
	// 响应头和空行
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);
	//sprintf(buf + strlen(buf), "\r\n");

	send(cfd, buf, strlen(buf), 0);
	printf("END Response status line, header and blank line... \n");
	return 0;
}

// 发送目录
int sendDir(const char* dirName, int cfd) {
    printf("Start sending a directory...\n");
	char buf[4096] = { 0 };
    sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);

    struct dirent** namelist; // 表示目录条目（目录项）的结构体。
    int num = scandir(dirName, &namelist, NULL, alphasort);  // 遍历目录，并获取目录中的所有目录项。
    for (int i = 0; i < num; ++i)
	{
		// 从namelist中取出文件名    namelist指向的是一个指针数组 比如:struct dirent* tmp[]
        char* name = namelist[i]->d_name;
		struct stat st;                 
		char subPath[1024] = { 0 };
		sprintf(subPath, "%s/%s", dirName, name);
		stat(subPath, &st);
		if (S_ISDIR(st.st_mode))
		{
			sprintf(buf + strlen(buf), 
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		else
		{
			sprintf(buf + strlen(buf), 
				"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		free(namelist[i]);
	}
    sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	printf("Send Directory End...\n");
	free(namelist);
	return 0;
}

// 将16进制的字符转换为10进制的整形
int hexToDec(char c) {
    if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

// 解码
// to 存储解码之后的数据，传出参数， from被解码的数据，传入参数
void decodeMsg(char* to, char* from) {
    for (; *from != '\0'; ++to, ++from)
	{
		// isxdigit -> 判断字符是不是16进制格式，取值在o~f
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			// 将16进制的数 -> 十进制， 将这个数值赋值给字符 int -> char(隐式转换)
			*to = hexToDec(from[1]) * 16 + hexToDec(from[2]);  //将3个字符变成一个字符，这个字符就是原始数据

			// 跳过 from[1] 和 from[2] 因为在当前循环中已经处理过了
			from += 2;
		}
		else
		{
			// 字符拷贝，赋值
			*to = *from;
		}
	}
	*to = '\0';
}
