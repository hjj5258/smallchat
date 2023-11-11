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

 /* ============================ ���ݽṹ =================================
  * �����ܸ������������޶ȵĶ����� �������һ���ܼ�
  * ��ʹ������Щ��̫�˽�C������˵Ҳ����ˡ�.
  * =========================================================================== */

#define MAX_CLIENTS 1000 // ��ʵ�����ǽϸߵ��ļ���������
#define SERVER_PORT 7711 // ����˿�

/* This structure represents a connected client. There is very little
 * info about it: the socket descriptor and the nick name, if set, otherwise
 * the first byte of the nickname is set to 0 if not set.
 * The client can set its nickname with /nick <nickname> command. */
struct client {
    int fd;     // Client socket.
    char *nick; // Nickname of the client.
};

/* This global structure encapsulates the global state of the chat. */
/* ���ȫ�ֽṹ��װ�����������ȫ��״̬. */
struct chatState {
    int serversock;     // �����Ķ˿�.
    int numclients;     // ���ӵĿͻ�������.
    int maxclient;      // ���ͻ���.
    struct client *clients[MAX_CLIENTS]; // �ͻ���ʵ��������鱣�棬�����fd��.
};

struct chatState *Chat; // ����ʱ��ʼ��.

/* ======================== Low level networking stuff ==========================
 * Here you will find basic socket stuff that should be part of
 * a decent standard C library, but you know... there are other
 * crazy goals for the future of C: like to make the whole language an
 * Undefined Behavior.
 * =========================================================================== */

 /* ======================== Low level networking stuff ==========================
  * ��������ᷢ��һЩ�������׽�������һ������ı�׼C�⣬����֪��������
    ����������C����δ���ķ��Ŀ�꣺ϲ������������
    δ�������Ϊ
  * =========================================================================== */

/* Create a TCP socket listening to 'port' ready to accept connections. */
/* ����һ��TCP���ӣ����� socket fd */
int createTCPServer(int port) {
    // s = socket fd
    int s, yes = 1;
    /*��ַ�ṹ��*/
    struct sockaddr_in sa;
    /*AF_INET IPV4  SOCK_STREAM �ֽ����׽��� 0 �Զ�ѡ��Ĭ��Э��*/
    /*����ʧ�ܷ���-1������*/
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) return -1;
    /* �����׽���ѡ�� ���⵱ TCP ����������ʱ�����Խ��׽��ְ󶨵�
       ��ǰ�Ѿ�ͬ TCP ���������Ķ˿���ʱ���ֵ� EADDRINUSE����ַ��ʹ�ã�����*/
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)); // Best effort.
    /*��ַ�ṹ�� ��ʼ��*/
    memset(&sa,0,sizeof(sa))

    /* AF_INET IPV4  */
    sa.sin_family = AF_INET;
    /* �˿� */
    sa.sin_port = htons(port)
    /*ip ��ַ INADDR_ANY*/
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    
    /*�������ǳ�ʼ���ĵ�ַ�ṹ�� ���԰� ������ �����׽��֣�socket���Խ��ܴ�������ӣ����511��*/
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
    /*  fcntl() ������ȡ��ǰ�׽��ֵı�־λ fd�ļ�״̬�ı�־λF_GETFL*/
    if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;
    /*���÷�����ģʽ*/
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

    /* This is best-effort. No need to check for errors. */
    /* �������ӳ� TCP_NODELAY �ӳ� ����Ҫ��������*/
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return 0;
}

/* If the listening socket signaled there is a new connection ready to
 * be accepted, we accept(2) it and return -1 on error or the new client
 * socket on success. */
 /*��������׽��ַ����źţ���ʾ��һ���µ�����׼������
  *��������ܣ����ǽ��ܣ�2�������ڳ��ִ�����¿ͻ���ʱ����-1
  *�׽��ֳɹ� */
int acceptClient(int server_socket) {
    // client fd
    int s;
    // ��ѭ��
    while(1) {
        struct sockaddr_in sa;
        socklen_t slen = sizeof(sa);
        // ���ӷ���ˣ�����fd
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
/* �����ڴ棬����ڴ治��ֱ���˳����� */
void *chatMalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        perror("Out of memory");
        exit(1);
    }
    return ptr;
}

/* Also aborting realloc(). */
/* ���·����ڴ棬����ڴ治��ֱ���˳����� */
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
/* ���ڴ���һ���µĿͻ��˲��󶨵�ָ�����ļ������� fd �ϡ�ͬʱ���������ȫ�ֵ� Chat ״̬�� */
struct client *createClient(int fd) {
    // ��ʼ���û�����������ʽ������
    char nick[32]; // Used to create an initial nick for the user.
    int nicklen = snprintf(nick,sizeof(nick),"user:%d",fd);
    // ����client�ڴ棬����ָ��
    struct client *c = chatMalloc(sizeof(*c));
    // �������ӳ�������
    socketSetNonBlockNoDelay(fd); // Pretend this will not fail.
    // �����client fd = ����fd
    c->fd = fd;
    // �����client �����ֶΣ������ڴ�
    c->nick = chatMalloc(nicklen+1);
    // ��ʼ������ ����
    memcpy(c->nick,nick,nicklen);
    // ����ȷ�� Chat->clients[c->fd] Ϊ�գ�����λ�ÿ���
    assert(Chat->clients[c->fd] == NULL); // This should be available.
    // ���� Chat �� clients ���飬���´����Ŀͻ��˼���
    Chat->clients[c->fd] = c;
    /* We need to update the max client set if needed. */
    // �����ǰ�ͻ��˵� fd ���� Chat->maxclient������� maxclient
    if (c->fd > Chat->maxclient) Chat->maxclient = c->fd;
    // �Ѿ����ӵĿͻ������� ���� Chat �� numclients ������
    Chat->numclients++;
    return c;
}

/* Free a client, associated resources, and unbind it from the global
 * state in Chat. */
/* �ͷſͻ��˺������Դ���������ȫ�ֽ����
 *�����е�״̬��*/
void freeClient(struct client *c) {
    // �ͷ��ڴ� nick ����
    free(c->nick);
    // �ر� fd
    close(c->fd);
    // ����ȫ��chat �ÿͻ���fd��clientsλ��ΪNULL
    Chat->clients[c->fd] = NULL;
    // ����ȫ��chat ���ӵĿͻ������� ��һ
    Chat->numclients--;
    if (Chat->maxclient == c->fd) {
        /* Ooops, this was the max client set. Let's find what is
         * the new highest slot used. */
        /* ��ɾ���Ŀͻ��������ͻ��ˡ�������Ҫ�ҳ��µ����ʹ�ò�λ�� */
        // ��Ҫ�ҵ��µ����ʹ�ò�λ��ͨ������ clients �����нϵ͵�λ�ã��ҵ���һ����Ϊ NULL �Ŀͻ��ˣ�
        // ������������ֵ�� maxclient�����û���ҵ�����˵�������пͻ��ˣ��� maxclient ����Ϊ -1��
        int j;
        for (j = Chat->maxclient-1; j >= 0; j--) {
            if (Chat->clients[j] != NULL) Chat->maxclient = j;
            break;
        }
        if (j == -1) Chat->maxclient = -1; // We no longer have clients.
    }
    // �ͷ� client 
    free(c);
}

/* Allocate and init the global stuff. */
/* ���䲢��ʼ��ȫ������ */
void initChat(void) {
    // ����ȫ��chat���ڴ�
    Chat = chatMalloc(sizeof(*Chat));
    // ��ʼ�� chat�������ֶ�Ϊ0
    memset(Chat,0,sizeof(*Chat));
    /* No clients at startup, of course. */
    // ������ʱ��û�пͻ���
    Chat->maxclient = -1;
    Chat->numclients = 0;

    /* Create our listening socket, bound to the given port. This
     * is where our clients will connect. */
    /* ���������׽��֣��󶨵�ָ���Ķ˿ڡ����ǿͻ��˽����ӵ��ĵط��� */
    Chat->serversock = createTCPServer(SERVER_PORT);
    // ������ص�-1��������ʧ�ܣ��˳�����
    if (Chat->serversock == -1) {
        perror("Creating listening socket");
        exit(1);
    }
}

/* Send the specified string to all connected clients but the one
 * having as socket descriptor 'excluded'. If you want to send something
 * to every client just set excluded to an impossible socket: -1. */
 /*��ָ�����ַ������͵���һ���ͻ���֮����������ӵĿͻ���
  *���С�excluded����Ϊ�׽������������������Ķ���
  *ÿ���ͻ��˶����ų���һ�������ܵ��׽���֮�⣺-1 */
void sendMsgToAllClientsBut(int excluded, char *s, size_t len) {
    // ѭ�����ӵĿͻ���
    for (int j = 0; j <= Chat->maxclient; j++) {
        // �����ǰ�ͻ��� == NULL ���� ����Ҫ���ų��ģ�������
        if (Chat->clients[j] == NULL ||
            Chat->clients[j]->fd == excluded) continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
         /*��Ҫ��ʾ�����ǲ����κλ��塣����ֻ��ʹ���ں�
          *�׽��ֻ�������������ݲ����ʣ����ǲ��ں���
          *Ϊ��ʹ���򱣳ּ򵥣���Ҫ������*/
        // �� fd��д�� msg
        write(Chat->clients[j]->fd,s,len);
    }
}

/* The main() function implements the main chat logic:
 * 1. Accept new clients connections if any.
 * 2. Check if any client sent us some new message.
 * 3. Send the message to all the other clients. */
/*
  ����һ����ں�����ʵ������������߼�
  1. �����¿ͻ������ӣ�����У�
  2. ����Ƿ��пͻ������Ƿ�����һЩ����Ϣ��
  3. ����Ϣ���͸����������ͻ��ˡ�
*/
int main(void) {
    // ��ʼ�� Chat
    initChat();

    // ��ѭ��
    while(1) {
        // fd set
        fd_set readfds;
        // ʱ��
        struct timeval tv;
        // ���ֵ
        int retval;
        // ���ڽ�һ���ļ�������������գ���ʼ��Ϊ�ռ�����
        FD_ZERO(&readfds);
        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */
         /* ������ϣ��ͨ�� select() ��֪ͨ�лʱ��
          * ��������׽����д�����Ŀͻ����������󣬻���������һ�ͻ���д�������ݡ� */
        // FD_SET() ��һ���꣬���ڽ�ָ�����ļ���������ӵ��ļ������������С�
        // serversock Chatȫ�ֵĿͻ��� fd
        FD_SET(Chat->serversock, &readfds);

        // ѭ�����ӵĿͻ��ˣ���fd ���� readfds
        for (int j = 0; j <= Chat->maxclient; j++) {
            if (Chat->clients[j]) FD_SET(j, &readfds);
        }

        /* Set a timeout for select(), see later why this may be useful
         * in the future (not now). */
         /* ���� select() �ĳ�ʱʱ�䣬��δ�����ܻ����ã�Ŀǰ����Ҫ��*/
        tv.tv_sec = 1; // 1 sec timeout
        tv.tv_usec = 0;

        /* Select wants as first argument the maximum file descriptor
         * in use plus one. It can be either one of our clients or the
         * server socket itself. */
         /* select() �ĵ�һ������������ļ���������һ��
         * �����������ǵ���һ�ͻ��˻������׽��֡� */
        int maxfd = Chat->maxclient;
        // �������fd С�� serversock���� maxfd = serversock
        if (maxfd < Chat->serversock) maxfd = Chat->serversock;
        // select() ��һ��ϵͳ���ã�������һ���ļ��������Ͻ����첽 I/O ��·���á�
        //  NULL����ʾ�����Ŀ�д�¼���NULL����ʾ�������쳣�¼�, tv��ʱʱ��
        retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select() error");
            exit(1);
        } else if (retval) {

            /* If the listening socket is "readable", it actually means
             * there are new clients connections pending to accept. */
             /* ��������׽��ֿɶ���ʵ������ζ�����µĿͻ���������������� */
            // FD_ISSET() ��һ���꣬���ڼ��ָ�����ļ��������Ƿ��ڸ������ļ������������о�����
            if (FD_ISSET(Chat->serversock, &readfds)) {
                // ���ӿͻ��ˣ�����fd
                int fd = acceptClient(Chat->serversock);
                // �����ͻ��ˣ����ؿͻ���ʵ����ָ��
                struct client *c = createClient(fd);
                /* Send a welcome message. */
                // ����һ�� ��ӭ����Ϣ
                char *welcome_msg =
                    "Welcome to Simple Chat! "
                    "Use /nick <nick> to set your nick.\n";
                // д��fd��������Ϣ��
                write(c->fd,welcome_msg,strlen(welcome_msg));
                // ��ӡ�Ѿ����ӵ��ͻ��� fd =
                printf("Connected client fd=%d\n", fd);
            }

            /* Here for each connected client, check if there are pending
             * data the client sent us. */
            /* ���ÿ�������ӵĿͻ��ˣ����Ƿ��д���������ݡ� */
            // ��ȡ�Ļ�����
            char readbuf[256];
            // ѭ���ͻ���
            for (int j = 0; j <= Chat->maxclient; j++) {
                // ������Ч�ͻ���
                if (Chat->clients[j] == NULL) continue;
                // ���fd�Ѿ�׼������
                if (FD_ISSET(j, &readfds)) {
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line is reached. */
                     /* ����ϣ�����յ�һ�������ġ���ʽ��ȷ����Ϣ��
                      * ��ʵ���Ͽ���ֻ��ȡ��һ������Ϣ��
                      * ��һ�������ĳ����У�Ϊ�˴����������������Ӧ��
                      * ���Ի����ȡ��ֱ��������β�� */
                    // ��ȡmsg��read �ɹ�����0������-1���Ǵ���
                    int nread = read(j,readbuf,sizeof(readbuf)-1);

                    if (nread <= 0) {
                        /* Error or short read means that the socket
                         * was closed. */
                        /* �����̶���ʾ�׽����ѹرա� */
                        printf("Disconnected client fd=%d, nick=%s\n",
                            j, Chat->clients[j]->nick);
                        // �ͷŸÿͻ���
                        freeClient(Chat->clients[j]);
                    } else {
                        /* The client sent us a message. We need to
                         * relay this message to all the other clients
                         * in the chat. */
                        /* �ͻ��˷�����һ����Ϣ��������Ҫ������Ϣת�����������е��������пͻ��ˡ� */
                        // �õ��ÿͻ��˵�ʵ����ָ�룩
                        struct client *c = Chat->clients[j];
                        // �ɹ� nread = 0������Ϊ0
                        readbuf[nread] = 0;

                        /* If the user message starts with "/", we
                         * process it as a client command. So far
                         * only the /nick <newnick> command is implemented. */
                         /* ����û���Ϣ�� "/" ��ͷ�����ǽ�����Ϊ�ͻ������
                         * Ŀǰ��ʵ���� /nick <newnick> ��� */
                        if (readbuf[0] == '/') {
                            /* Remove any trailing newline. */
                            /* ɾ���κ�β��Ļ��з��� */
                            char *p;
                            p = strchr(readbuf,'\r'); if (p) *p = 0;
                            p = strchr(readbuf,'\n'); if (p) *p = 0;
                            /* Check for an argument of the command, after
                             * the space. */
                            /* ��������Ĳ��������ո�֮������ݡ� */
                            char *arg = strchr(readbuf,' ');
                            if (arg) {
                                *arg = 0; /* Terminate command name. */ /* ��ֹ�������ơ� */   
                                arg++; /* Argument is 1 byte after the space. */ /* �����ڿո�֮��� 1 ���ֽڴ��� */
                            }
                            // ���������/nick
                            if (!strcmp(readbuf,"/nick") && arg) {
                                // �ͷ�ԭ�ȵ�nick
                                free(c->nick);
                                // ȡ�����nick
                                int nicklen = strlen(arg);
                                // ��������nick���ڴ�ռ�
                                c->nick = chatMalloc(nicklen+1);
                                // ���������õģ���arg���� copy ���µ��ڴ���
                                memcpy(c->nick,arg,nicklen+1);
                            } else {
                                /* Unsupported command. Send an error. */
                                /* ��֧�ֵ��������һ������ */
                                char *errmsg = "Unsupported command\n";
                                /* fdд�������Ϣ */
                                write(c->fd,errmsg,strlen(errmsg));
                            }
                        } else {
                            /* Create a message to send everybody (and show
                             * on the server console) in the form:
                             *   nick> some message. */
                             /*����һ����Ϣ���͸������ˣ�����ʾ
                              *�ڷ���������̨�ϣ�����ʽΪ��
                              *nick>һЩ��Ϣ*/
                            char msg[256];
                            // ��ʽ����Ϣ
                            int msglen = snprintf(msg, sizeof(msg),
                                "%s> %s", c->nick, readbuf);
                            // ��ֹ��Ϣ�������� ��ʽ����� msglen > msg�Ĵ�С��������Ϊmsg size-1
                            if (msglen >= (int)sizeof(msg))
                                msglen = sizeof(msg)-1;
                            printf("%s",msg);

                            /* Send it to all the other clients. */
                            /* ������Ϣ�����������ͻ��� */
                            sendMsgToAllClientsBut(j,msg,msglen);
                        }
                    }
                }
            }
        } else {
            /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */

             /*������ʱ����������ʲô������������
              *һ������£��˲��ֿ����ڶ��ڻ���
              *��ʹû�пͻ��˻*/
        }
    }
    return 0;
}
