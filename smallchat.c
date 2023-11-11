/* smallchat.c -- Read clients input, send to all the other connected clients.
 *
 * Copyright (c) 2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the project name of nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * 
 * 
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/select.h>

/* ============================ Data structures =================================
 * The minimal stuff we can afford to have. This example must be simple
 * even for people that don't know a lot of C.
 * =========================================================================== */

 /* ============================ 数据结构 =================================
  * 我们能负担得起的最低限度的东西。 这个例子一定很简单
  * 即使对于那些不太了解C的人来说也是如此。.
  * =========================================================================== */

#define MAX_CLIENTS 1000 // 这实际上是较高的文件描述符。
#define SERVER_PORT 7711 // 服务端口

/* This structure represents a connected client. There is very little
 * info about it: the socket descriptor and the nick name, if set, otherwise
 * the first byte of the nickname is set to 0 if not set.
 * The client can set its nickname with /nick <nickname> command. */
struct client {
    int fd;     // Client socket.
    char *nick; // Nickname of the client.
};

/* This global structure encapsulates the global state of the chat. */
/* 这个全局结构封装了聊天软件的全局状态. */
struct chatState {
    int serversock;     // 监听的端口.
    int numclients;     // 连接的客户端数量.
    int maxclient;      // 最大客户端.
    struct client *clients[MAX_CLIENTS]; // 客户端实例插槽数组保存，网络的fd等.
};

struct chatState *Chat; // 启动时初始化.

/* ======================== Low level networking stuff ==========================
 * Here you will find basic socket stuff that should be part of
 * a decent standard C library, but you know... there are other
 * crazy goals for the future of C: like to make the whole language an
 * Undefined Behavior.
 * =========================================================================== */

 /* ======================== Low level networking stuff ==========================
  * 在这里你会发现一些基本的套接字内容一个不错的标准C库，但你知道。。。
    还有其他对C语言未来的疯狂目标：喜欢让整个语言
    未定义的行为
  * =========================================================================== */

/* Create a TCP socket listening to 'port' ready to accept connections. */
/* 创建一个TCP连接，返回 socket fd */
int createTCPServer(int port) {
    // s = socket fd
    int s, yes = 1;
    /*地址结构体*/
    struct sockaddr_in sa;
    /*AF_INET IPV4  SOCK_STREAM 字节流套接字 0 自动选择默认协议*/
    /*创建失败返回-1错误码*/
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) return -1;
    /* 设置套接字选项 避免当 TCP 服务器重启时，尝试将套接字绑定到
       当前已经同 TCP 结点相关联的端口上时出现的 EADDRINUSE（地址已使用）错误。*/
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)); // Best effort.
    /*地址结构体 初始化*/
    memset(&sa,0,sizeof(sa))

    /* AF_INET IPV4  */
    sa.sin_family = AF_INET;
    /* 端口 */
    sa.sin_port = htons(port)
    /*ip 地址 INADDR_ANY*/
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    
    /*根据我们初始化的地址结构体 尝试绑定 ，或者 设置套接字（socket）以接受传入的连接（最大511）*/
    if (bind(s,(struct sockaddr*)&sa,sizeof(sa)) == -1 ||
        listen(s, 511) == -1)
    {
        close(s);
        return -1;
    }
    return s;
}

/* Set the specified socket in non-blocking mode, with no delay flag. */
int socketSetNonBlockNoDelay(int fd) {
    int flags, yes = 1;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    /*  fcntl() 函数获取当前套接字的标志位 fd文件状态的标志位F_GETFL*/
    if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;
    /*设置非阻塞模式*/
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

    /* This is best-effort. No need to check for errors. */
    /* 设置无延迟 TCP_NODELAY 延迟 不需要做错误检查*/
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return 0;
}

/* If the listening socket signaled there is a new connection ready to
 * be accepted, we accept(2) it and return -1 on error or the new client
 * socket on success. */
 /*如果侦听套接字发出信号，表示有一个新的连接准备就绪
  *如果被接受，我们接受（2），并在出现错误或新客户端时返回-1
  *套接字成功 */
int acceptClient(int server_socket) {
    // client fd
    int s;
    // 死循环
    while(1) {
        struct sockaddr_in sa;
        socklen_t slen = sizeof(sa);
        // 连接服务端，返回fd
        s = accept(server_socket,(struct sockaddr*)&sa,&slen);
        if (s == -1) {
            if (errno == EINTR)
                continue; /* Try again. */
            else
                return -1;
        }
        break;
    }
    return s;
}

/* We also define an allocator that always crashes on out of memory: you
 * will discover that in most programs designed to run for a long time, that
 * are not libraries, trying to recover from out of memory is often futile
 * and at the same time makes the whole program terrible. */
/* 分配内存，如果内存不足直接退出程序 */
void *chatMalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("Out of memory");
        exit(1);
    }
    return ptr;
}

/* Also aborting realloc(). */
/* 重新分配内存，如果内存不足直接退出程序 */
void *chatRealloc(void *ptr, size_t size) {
    ptr = realloc(ptr,size);
    if (ptr == NULL) {
        perror("Out of memory");
        exit(1);
    }
    return ptr;
}

/* ====================== Small chat core implementation ========================
 * Here the idea is very simple: we accept new connections, read what clients
 * write us and fan-out (that is, send-to-all) the message to everybody
 * with the exception of the sender. And that is, of course, the most
 * simple chat system ever possible.
 * =========================================================================== */

/* Create a new client bound to 'fd'. This is called when a new client
 * connects. As a side effect updates the global Chat state. */
/* 用于创建一个新的客户端并绑定到指定的文件描述符 fd 上。同时，它会更新全局的 Chat 状态。 */
struct client *createClient(int fd) {
    // 初始化用户的姓名，格式化姓名
    char nick[32]; // Used to create an initial nick for the user.
    int nicklen = snprintf(nick,sizeof(nick),"user:%d",fd);
    // 分配client内存，返回指针
    struct client *c = chatMalloc(sizeof(*c));
    // 设置无延迟无阻塞
    socketSetNonBlockNoDelay(fd); // Pretend this will not fail.
    // 分配的client fd = 参数fd
    c->fd = fd;
    // 分配的client 姓名字段，分配内存
    c->nick = chatMalloc(nicklen+1);
    // 初始化复制 姓名
    memcpy(c->nick,nick,nicklen);
    // 断言确保 Chat->clients[c->fd] 为空，即该位置可用
    assert(Chat->clients[c->fd] == NULL); // This should be available.
    // 更新 Chat 的 clients 数组，将新创建的客户端加入
    Chat->clients[c->fd] = c;
    /* We need to update the max client set if needed. */
    // 如果当前客户端的 fd 大于 Chat->maxclient，则更新 maxclient
    if (c->fd > Chat->maxclient) Chat->maxclient = c->fd;
    // 已经连接的客户端数量 增加 Chat 的 numclients 计数器
    Chat->numclients++;
    return c;
}

/* Free a client, associated resources, and unbind it from the global
 * state in Chat. */
/* 释放客户端和相关资源，并将其从全局解除绑定
 *聊天中的状态。*/
void freeClient(struct client *c) {
    // 释放内存 nick 姓名
    free(c->nick);
    // 关闭 fd
    close(c->fd);
    // 设置全局chat 该客户端fd在clients位置为NULL
    Chat->clients[c->fd] = NULL;
    // 设置全局chat 连接的客户端数量 减一
    Chat->numclients--;
    if (Chat->maxclient == c->fd) {
        /* Ooops, this was the max client set. Let's find what is
         * the new highest slot used. */
        /* 被删除的客户端是最大客户端。我们需要找出新的最高使用槽位。 */
        // 需要找到新的最高使用槽位。通过遍历 clients 数组中较低的位置，找到第一个不为 NULL 的客户端，
        // 并将其索引赋值给 maxclient。如果没有找到，则说明不再有客户端，将 maxclient 设置为 -1。
        int j;
        for (j = Chat->maxclient-1; j >= 0; j--) {
            if (Chat->clients[j] != NULL) Chat->maxclient = j;
            break;
        }
        if (j == -1) Chat->maxclient = -1; // We no longer have clients.
    }
    // 释放 client 
    free(c);
}

/* Allocate and init the global stuff. */
/* 分配并初始化全局内容 */
void initChat(void) {
    // 分配全局chat的内存
    Chat = chatMalloc(sizeof(*Chat));
    // 初始化 chat，所有字段为0
    memset(Chat,0,sizeof(*Chat));
    /* No clients at startup, of course. */
    // 启动的时候没有客户端
    Chat->maxclient = -1;
    Chat->numclients = 0;

    /* Create our listening socket, bound to the given port. This
     * is where our clients will connect. */
    /* 创建监听套接字，绑定到指定的端口。这是客户端将连接到的地方。 */
    Chat->serversock = createTCPServer(SERVER_PORT);
    // 如果返回的-1，代表创建失败，退出程序
    if (Chat->serversock == -1) {
        perror("Creating listening socket");
        exit(1);
    }
}

/* Send the specified string to all connected clients but the one
 * having as socket descriptor 'excluded'. If you want to send something
 * to every client just set excluded to an impossible socket: -1. */
 /*将指定的字符串发送到除一个客户端之外的所有连接的客户端
  *具有“excluded”作为套接字描述符。如果你想寄东西
  *每个客户端都被排除在一个不可能的套接字之外：-1 */
void sendMsgToAllClientsBut(int excluded, char *s, size_t len) {
    // 循环连接的客户端
    for (int j = 0; j <= Chat->maxclient; j++) {
        // 如果当前客户端 == NULL 或者 是需要被排除的，则跳过
        if (Chat->clients[j] == NULL ||
            Chat->clients[j]->fd == excluded) continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
         /*重要提示：我们不做任何缓冲。我们只是使用内核
          *套接字缓冲区。如果内容不合适，我们不在乎。
          *为了使程序保持简单，需要这样做*/
        // 向 fd中写入 msg
        write(Chat->clients[j]->fd,s,len);
    }
}

/* The main() function implements the main chat logic:
 * 1. Accept new clients connections if any.
 * 2. Check if any client sent us some new message.
 * 3. Send the message to all the other clients. */
/*
  这是一个入口函数，实现了主聊天的逻辑
  1. 接受新客户端连接（如果有）
  2. 检查是否有客户向我们发送了一些新消息。
  3. 将消息发送给所有其他客户端。
*/
int main(void) {
    // 初始化 Chat
    initChat();

    // 死循环
    while(1) {
        // fd set
        fd_set readfds;
        // 时间
        struct timeval tv;
        // 结果值
        int retval;
        // 用于将一个文件描述符集合清空（初始化为空集）。
        FD_ZERO(&readfds);
        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */
         /* 当我们希望通过 select() 被通知有活动时？
          * 如果监听套接字有待处理的客户端连接请求，或者其他任一客户端写入了数据。 */
        // FD_SET() 是一个宏，用于将指定的文件描述符添加到文件描述符集合中。
        // serversock Chat全局的客户端 fd
        FD_SET(Chat->serversock, &readfds);

        // 循环连接的客户端，把fd 加入 readfds
        for (int j = 0; j <= Chat->maxclient; j++) {
            if (Chat->clients[j]) FD_SET(j, &readfds);
        }

        /* Set a timeout for select(), see later why this may be useful
         * in the future (not now). */
         /* 设置 select() 的超时时间，在未来可能会有用（目前不需要）*/
        tv.tv_sec = 1; // 1 sec timeout
        tv.tv_usec = 0;

        /* Select wants as first argument the maximum file descriptor
         * in use plus one. It can be either one of our clients or the
         * server socket itself. */
         /* select() 的第一个参数是最大文件描述符加一。
         * 它可以是我们的任一客户端或服务端套接字。 */
        int maxfd = Chat->maxclient;
        // 如果最大的fd 小于 serversock，则 maxfd = serversock
        if (maxfd < Chat->serversock) maxfd = Chat->serversock;
        // select() 是一个系统调用，用于在一组文件描述符上进行异步 I/O 多路复用。
        //  NULL，表示不关心可写事件。NULL，表示不关心异常事件, tv超时时间
        retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select() error");
            exit(1);
        } else if (retval) {

            /* If the listening socket is "readable", it actually means
             * there are new clients connections pending to accept. */
             /* 如果监听套接字可读，实际上意味着有新的客户端连接请求待处理。 */
            // FD_ISSET() 是一个宏，用于检查指定的文件描述符是否在给定的文件描述符集合中就绪。
            if (FD_ISSET(Chat->serversock, &readfds)) {
                // 连接客户端，返回fd
                int fd = acceptClient(Chat->serversock);
                // 创建客户端，返回客户端实例的指针
                struct client *c = createClient(fd);
                /* Send a welcome message. */
                // 发送一个 欢迎的信息
                char *welcome_msg =
                    "Welcome to Simple Chat! "
                    "Use /nick <nick> to set your nick.\n";
                // 写入fd（发送消息）
                write(c->fd,welcome_msg,strlen(welcome_msg));
                // 打印已经连接到客户端 fd =
                printf("Connected client fd=%d\n", fd);
            }

            /* Here for each connected client, check if there are pending
             * data the client sent us. */
            /* 检查每个已连接的客户端，看是否有待处理的数据。 */
            // 读取的缓冲区
            char readbuf[256];
            // 循环客户端
            for (int j = 0; j <= Chat->maxclient; j++) {
                // 跳过无效客户端
                if (Chat->clients[j] == NULL) continue;
                // 如果fd已经准备好了
                if (FD_ISSET(j, &readfds)) {
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line is reached. */
                     /* 我们希望接收到一个完整的、格式正确的消息。
                      * 但实际上可能只读取了一部分消息。
                      * 在一个正常的程序中，为了处理这种情况，我们应该
                      * 尝试缓冲读取，直到遇到行尾。 */
                    // 读取msg，read 成功返回0，返回-1则是错误
                    int nread = read(j,readbuf,sizeof(readbuf)-1);

                    if (nread <= 0) {
                        /* Error or short read means that the socket
                         * was closed. */
                        /* 错误或短读表示套接字已关闭。 */
                        printf("Disconnected client fd=%d, nick=%s\n",
                            j, Chat->clients[j]->nick);
                        // 释放该客户端
                        freeClient(Chat->clients[j]);
                    } else {
                        /* The client sent us a message. We need to
                         * relay this message to all the other clients
                         * in the chat. */
                        /* 客户端发送了一条消息。我们需要将该消息转发给聊天室中的其他所有客户端。 */
                        // 拿到该客户端的实例（指针）
                        struct client *c = Chat->clients[j];
                        // 成功 nread = 0，设置为0
                        readbuf[nread] = 0;

                        /* If the user message starts with "/", we
                         * process it as a client command. So far
                         * only the /nick <newnick> command is implemented. */
                         /* 如果用户消息以 "/" 开头，我们将其视为客户端命令。
                         * 目前仅实现了 /nick <newnick> 命令。 */
                        if (readbuf[0] == '/') {
                            /* Remove any trailing newline. */
                            /* 删除任何尾随的换行符。 */
                            char *p;
                            p = strchr(readbuf,'\r'); if (p) *p = 0;
                            p = strchr(readbuf,'\n'); if (p) *p = 0;
                            /* Check for an argument of the command, after
                             * the space. */
                            /* 检查命令后的参数，即空格之后的内容。 */
                            char *arg = strchr(readbuf,' ');
                            if (arg) {
                                *arg = 0; /* Terminate command name. */ /* 终止命令名称。 */   
                                arg++; /* Argument is 1 byte after the space. */ /* 参数在空格之后的 1 个字节处。 */
                            }
                            // 如果命令是/nick
                            if (!strcmp(readbuf,"/nick") && arg) {
                                // 释放原先的nick
                                free(c->nick);
                                // 取输入的nick
                                int nicklen = strlen(arg);
                                // 重新设置nick的内存空间
                                c->nick = chatMalloc(nicklen+1);
                                // 将重新设置的，从arg数据 copy 到新的内存上
                                memcpy(c->nick,arg,nicklen+1);
                            } else {
                                /* Unsupported command. Send an error. */
                                /* 不支持的命令，发送一个错误 */
                                char *errmsg = "Unsupported command\n";
                                /* fd写入错误信息 */
                                write(c->fd,errmsg,strlen(errmsg));
                            }
                        } else {
                            /* Create a message to send everybody (and show
                             * on the server console) in the form:
                             *   nick> some message. */
                             /*创建一条消息发送给所有人（并显示
                              *在服务器控制台上），格式为：
                              *nick>一些消息*/
                            char msg[256];
                            // 格式化消息
                            int msglen = snprintf(msg, sizeof(msg),
                                "%s> %s", c->nick, readbuf);
                            // 防止消息溢出，如果 格式化后的 msglen > msg的大小，则重置为msg size-1
                            if (msglen >= (int)sizeof(msg))
                                msglen = sizeof(msg)-1;
                            printf("%s",msg);

                            /* Send it to all the other clients. */
                            /* 发送消息到所有其他客户端 */
                            sendMsgToAllClientsBut(j,msg,msglen);
                        }
                    }
                }
            }
        } else {
            /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */

             /*发生超时。我们现在什么都不做，但在
              *一般情况下，此部分可用于定期唤醒
              *即使没有客户端活动*/
        }
    }
    return 0;
}
