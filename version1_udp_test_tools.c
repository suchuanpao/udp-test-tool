/*************************************************************************
	> File Name: linux_udp_test_tools.c
	> Author: 
	> Mail: 
	> Created Time: 2016年06月13日 星期一 13时28分22秒
 ************************************************************************/

#include<stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/kernel.h>
#define MAX_LENGTH 2048
#define STOP 0X77
#define START 0X88
#define CONTINUE 0x99

#define MSG_FORMAT "ip address\t Bits\t\t speed\t\t recv packets\t lose packets\t packet speed\t state\t\n"
#define DATA_FORMAT "%s\t %ld(Bits)\t %d(KB/s)\t %d\t\t %d\t\t %d\t\t %s\t\n"

typedef struct __ClientMsg
{
        struct sockaddr_in addr;
        long int tr_len;
        int tr_time;
        int tr_speed;
	int packet_counts;
	int last_time_packet_counts;
	int packet_speed;
	int lose_packets;
	int state;
        struct __ClientMsg * next;
}ClientMsg;
typedef struct __ClientMsgHead
{
        ClientMsg * head;
        ClientMsg * (*find_by_addr)(struct __ClientMsgHead *, struct __ClientMsg *);
        int (*show_recv_message)(struct __ClientMsgHead *);
	int (*add_new_client)(struct __ClientMsgHead*, struct __ClientMsg *);
	int (*list_for_each)(struct __ClientMsgHead * ,int (*ops)(void *, void*),void*);
}ClientMsgHead;
typedef struct __TestData
{
        int packet_length;
	int packet_counts;
        int rate;
        int continue_time;
        int port;
        char ip[16];

        int span;
        int real_span;
        long long already_tr;
        long long should_tr;
        int percent;

        struct sockaddr_in addr;
        int sock;
        char message[MAX_LENGTH];

	ClientMsg cli_msg;
	ClientMsgHead head;

        pthread_cond_t cond;
        pthread_mutex_t mutex;

        int (*start)(struct __TestData *);
        void* (*thread_func[5])(void *);
        int (*wait_thread_over)(struct __TestData *);
        int (*init)(struct __TestData *);
        void (*release)(struct __TestData *);
	void (*sig_handler)(int);
}TestData;

TestData data;

void print_notice();
int is_ip(const char * ip);
int parse_arg(int argc, char *argv[],TestData * data);
int set_init_func(TestData * data, int flag);
int timeval_subtract(struct timeval* result, struct timeval* x, struct timeval* y);

int init_send_test_data(TestData *data);
int init_recv_test_data(TestData * );
int show_recv_message(ClientMsgHead *head);
void * show_send_process_thread(void * param);
void * show_recv_process_thread(void *param);
void * judge_new_client_thread(void *param);
void release_send_test_data(TestData * data);
void release_recv_test_data(TestData * data);
int wait_udp_send_over(TestData * data);
int wait_udp_recv_over(TestData * data);
int start_udp_send(TestData * data);
int start_udp_recv(TestData * data);
int create_thread(TestData * data);

void send_message(int sig);
void sig_for_release(int sig);

int add_new_client(struct __ClientMsgHead*, struct __ClientMsg *);
int list_for_each(struct __ClientMsgHead *, int (*ops)(void *, void *),void*);
ClientMsg* find_by_addr(ClientMsgHead * head, ClientMsg * cli_msg);
int free_node(void *, void *);
int opt_msg(void *, void *);

int is_ip(const char *ip)
{
        int ip_value[4] = {0};
        int ip_num = 0;
        int point_num = 0;
        int i = 0;
        while (*(ip+i)) {
                if (isdigit(*(ip+i))) {
                        ip_value[ip_num] = ip_value[ip_num] *10;
                        ip_value[ip_num] += *(ip+i)-'0';
                        if (ip_value[ip_num] > 255 || ip_value[ip_num] < 0)
				return 0;
                }
                else if (*(ip+i) == '.') {
                        if (i == 0 || *(ip+i+1) == '.' || *(ip+i+1) == '\0')
				return 0;
                        else if (point_num > 3)
				return 0;
                        point_num ++;
                        ip_num++;
                } else {
                        return 0;
                }
                i++;
        }
        if (point_num != 3) {
                return 0;
        }
        return 1;
}
int list_for_each(struct __ClientMsgHead * head, int (*ops)(void *, void *),void * param)
{
	ClientMsg * tmp1 = NULL;
	ClientMsg * tmp2 = NULL;
	tmp1 = head->head;
	while (tmp1) {
		tmp2 = tmp1->next;
		if (ops(tmp1,param?param:tmp2) < 0) {
			return -1;
		}
		tmp1 = tmp2;
	}
	return 0;
}
enum ARGS_FLAG
{
        IP_FLAG = 0,
        PORT_FLAG = 1,
        PACKET_LENGTH_FLAG = 2,
        RATE_FLAG = 3,
        CONTINUE_TIME_FLAG = 4,
        SEND_FLAG = 5,
        RECV_FLAG = 6
};
#define FLAG_BITS 7
int set_init_func(TestData * data, int flag)
{
        if (flag&(1<<SEND_FLAG)) {
                if ( (flag & 1 << IP_FLAG) &&
                  (flag & 1 << PORT_FLAG) &&
                  (flag & 1 << PACKET_LENGTH_FLAG) &&
                  (flag & 1 << RATE_FLAG) &&
                  (flag & 1 << CONTINUE_TIME_FLAG) ) {
                        data->init = init_send_test_data;
                }
                else {
			goto args_flag_err;
                }
        } else if (flag&(1<<RECV_FLAG)) {
                if ((flag & 1 << PORT_FLAG)) {
                        data->init = init_recv_test_data;
                } else {
			goto args_flag_err;
		}
        }
        else
        {
                return -1;
        }
	return 0;
args_flag_err:
	puts("args is error\n");
	return -1;

}
int parse_arg(int argc, char *argv[],TestData * data)
{
        char * str = NULL;
        int ch;
        int flag = 0;
        while ((ch = getopt(argc,argv,"i:p:l:b:t:sr")) !=-1) {
                switch(ch) {
                case 'i':
                        if (!is_ip(optarg)) {
                                puts("ip error\n");
                                return -1;
                        }
                        strcpy(data->ip,optarg);
                        flag |= 1<<IP_FLAG;
                        break;
                case 'p':
                        data->port = strtol(optarg,&str,0);
                        if (*str != '\0') {
                                puts("port error\n");
                                return -1;
                        }
                        else if (data->port < 1 || data->port > 65535) {
                                puts("port out of range\n");
                        }
                        flag |= 1<<PORT_FLAG;
                        break;
                case 'l':
                        data->packet_length = strtol(optarg,&str,0);
                        if (*str != '\0') {
                                puts("packet length error\n");
                                return -1;
                        }
                        else if (data->packet_length < 18 || data->packet_length > MAX_LENGTH) {
                                puts("packet length out of range\n");
                        }
                        flag |= 1<<PACKET_LENGTH_FLAG;
                        break;
                case 'b':
                        data->rate = strtol(optarg,&str,0);
                        if (*str != '\0') {
                                puts("send rate error\n");
                                return -1;
                        }
                        else if (data->rate < 1 || data->rate > 100) {
                                puts("send rate out of range\n");
                        }
                        flag |= 1<<RATE_FLAG;
                        break;
                case 't':
                        data->continue_time = strtol(optarg,&str,0);
                        if (*str != '\0') {
                                puts("continue time error\n");
                                return -1;
                        }
                        else if (data->continue_time < 1 || data->continue_time > 3600) {
                                puts("continue time out of range\n");
                        }
                        flag |= 1<<CONTINUE_TIME_FLAG;
                        break;
                case 'r':
                        flag |= 1<<RECV_FLAG;
                        break;
                case 's':
                        flag |= 1<<SEND_FLAG;
                        break;
                default:
                        putchar(ch);
                        puts("option error\n");
                        return -1;
                }
        }
        return flag;
}

ClientMsg* find_by_addr(ClientMsgHead * head, ClientMsg * cli_msg)
{
	ClientMsg * tmp = head->head;
	if (!cli_msg) {
		return NULL;
	}
	while (tmp) {
		if (memcmp(&tmp->addr.sin_addr, \
		&cli_msg->addr.sin_addr, sizeof(struct in_addr)) == 0) {
			return tmp;
		}
		tmp = tmp->next;
	}
	return NULL;
}
int add_new_client(ClientMsgHead * head, struct __ClientMsg* cli)
{
	ClientMsg * tmp = malloc(sizeof(*cli));
	if (!tmp) {
		perror("malloc ClientMsg failed");
		return -1;
	}
	memcpy(tmp,cli,sizeof(*tmp));
	tmp->next = NULL;
	tmp->next = head->head;
	head->head = tmp;

	return 0;
}
void print_notice()
{
        puts("+-------------------------------------------------------------------------------------------+\n");
        puts("Usage: udptest [-r|s] [-i <ip address>] [-p <port>] [-l <packet length>] [-b <rate>] [-t <continue time>]\n");
        puts("\ti:\tip address              like 127.0.0.1\n");
        puts("\tp:\tport                    range 1~65535\n");
        puts("\tl:\tpacket length           range 18~2048\n");
        puts("\tb:\tsend bits rate(Mb/s)    range 1~100\n");
        puts("\tt:\tcontinue time(s)        range 1~3600\n");
        puts("\ts:\tsend bits\n");
        puts("\tr:\trecv bits\n");
        puts("example:\n");
        puts("udptest -s -i 127.0.0.1 -p 8888 -l 200 -b 20 -t 20\n");
        puts("udptest -r -p 8888\n");
        puts("+-------------------------------------------------------------------------------------------+\n");
}
int timeval_subtract(struct timeval* result, struct timeval* x, struct timeval* y)
{
        if ( x->tv_sec>y->tv_sec   )
		return -1;
        if ( (x->tv_sec==y->tv_sec) && (x->tv_usec>y->tv_usec)   )
		return -1;
        result->tv_sec = ( y->tv_sec-x->tv_sec   );
        result->tv_usec = ( y->tv_usec-x->tv_usec   );
        if (result->tv_usec<0) {
                result->tv_sec--;
                result->tv_usec+=1000000;
        }
        return 0;
}

int init_recv_test_data(TestData * data)
{
        //回调函数初始化
        data->wait_thread_over = wait_udp_recv_over;
        data->release = release_recv_test_data;
        data->start = start_udp_recv;
        data->thread_func[0] = show_recv_process_thread;
	//data->thread_func[1] = judge_new_client_thread;
	data->sig_handler = sig_for_release;

	data->head.find_by_addr = find_by_addr;
	data->head.add_new_client = add_new_client;
	data->head.show_recv_message = show_recv_message;
	data->head.list_for_each = list_for_each;
        //初始化锁
        pthread_cond_init(&data->cond,NULL);
        pthread_mutex_init(&data->mutex,NULL);
	//初始化服务器套接字
        data->addr.sin_family = AF_INET;
        data->addr.sin_port = htons(data->port);
        data->addr.sin_addr.s_addr = INADDR_ANY;
        data->sock = socket(AF_INET,SOCK_DGRAM,0);
        if (data->sock < 0) {
                perror("sock failed");
                return -1;
        }
        if (bind(data->sock,(struct sockaddr *)&data->addr, sizeof(data->addr)) != 0) {
                perror("bind failed");
                return -1;
        }
	//发送的信息表持续
	data->message[0] = CONTINUE;
        return 0;
}

int init_send_test_data(TestData *data)
{
        //回调函数初始化
        data->thread_func[0] = show_send_process_thread;
        data->wait_thread_over = wait_udp_send_over;
        data->release = release_send_test_data;
        data->start = start_udp_send;
	data->sig_handler = send_message;
        //初始化锁
        pthread_cond_init(&data->cond,NULL);
        pthread_mutex_init(&data->mutex,NULL);
        //初始化发送套接字
        data->addr.sin_family = AF_INET;
        data->addr.sin_port = htons(data->port);
        if (inet_pton(AF_INET,data->ip,(struct sockaddr*)&(data->addr.sin_addr)) < 0) {
                perror("inet_pton failed");
                return -1;
        }
        data->sock = socket(AF_INET,SOCK_DGRAM,0);
        if (data->sock < 0) {
                perror("get sock descripter failed");
                return -1;
        }
        //计算发送频率:单位微秒
        data->span =8*data->packet_length/data->rate;
        data->should_tr = data->continue_time*data->rate*1000000/8;//应当发送字节数
        data->already_tr = 0;
        //发送频率应大于中断最小时间间隔
        struct timeval start, end, diff = {0};
        gettimeofday(&start,0);
        sendto(data->sock,data->message,data->packet_length,0, \
               (struct sockaddr *)&data->addr,sizeof(data->addr));
        gettimeofday(&end,0);
        timeval_subtract(&diff,&start,&end);
        data->real_span = diff.tv_usec;//平均值
        if (data->span < data->real_span) {
                printf("发送数据时，时间间隔为%d.00us小于实际发送时间%d.00us微秒，不合理值.\n",data->span,data->real_span);
                return -1;
        }
        printf("发送时间间隔为:%d.00us,实际发送一次时间间隔为%d.00us\n",data->span,data->real_span);
        return 0;
}

int show_recv_message(ClientMsgHead *head)
{
	int ret = 0;
	int time_span = 1;
	system("clear");
	printf(MSG_FORMAT);
	ret = head->list_for_each(head, opt_msg, (void*)&time_span);
	return ret;
}
void * show_recv_process_thread(void *param)
{
	int ret;
        pthread_detach(pthread_self());
        TestData * data = (TestData*)param;
	ClientMsgHead * head = &data->head;
        pthread_cond_signal(&data->cond);

        pthread_mutex_lock(&data->mutex);
        pthread_cond_wait(&data->cond, &data->mutex);
        pthread_mutex_unlock(&data->mutex);

	while (1) {
		sleep(1);
		pthread_mutex_lock(&data->mutex);
		ret = head->show_recv_message(head);
		pthread_mutex_unlock(&data->mutex);
		if (ret < 0) {
			break;
		}
	}
        return 0;
}
void * judge_new_client_thread(void *param)
{
        pthread_detach(pthread_self());
        TestData * data = (TestData*)param;

        pthread_cond_signal(&data->cond);

        pthread_mutex_lock(&data->mutex);
        pthread_cond_wait(&data->cond, &data->mutex);
        pthread_mutex_unlock(&data->mutex);


        return 0;
}
void * show_send_process_thread(void * param)
{

        pthread_detach(pthread_self());
        TestData * data = (TestData*)param;
        int percent = 0;
        data->percent = -1;

        pthread_cond_signal(&data->cond);

        pthread_mutex_lock(&data->mutex);
        pthread_cond_wait(&data->cond, &data->mutex);
        pthread_mutex_unlock(&data->mutex);

        puts("start send:");
        while (data->percent < 100) {
                percent = 100*data->already_tr/data->should_tr;
                if (percent != data->percent) {
                        data->percent = percent;
                        printf("\b\b\b\b%3d%%",data->percent);
                        fflush(stdout);
                }
        }
        return 0;
}

int create_thread(TestData * data)
{
        int ret = 0;
        pthread_t tid;
        void * (**pfunc)(void *);
        pfunc = data->thread_func;
        while (*pfunc) {
                ret = pthread_create(&tid,NULL,*pfunc++,data);
                if (ret) {
                        perror("pthread create failed");
                        return -1;
                }
                //等待线程唤醒主线程，标志线程开始运行
                pthread_mutex_lock(&data->mutex);
                pthread_cond_wait(&data->cond,&data->mutex);
                pthread_mutex_unlock(&data->mutex);
        }
        return 0;
}
int set_timer_vs(int it_value, int it_interval)
{
        struct itimerval tick;
        tick.it_value.tv_sec = 0;
        tick.it_value.tv_usec = it_value;
        tick.it_interval.tv_sec = 0;
        tick.it_interval.tv_usec = it_interval;
        return setitimer(ITIMER_REAL, &tick, NULL);
}

void sig_for_release(int sig)
{
	puts("over");
	data.release(&data);
	exit(0);
}
void send_message(int sig)
{
	if (data.already_tr < data.should_tr) {
		if (sendto(data.sock,data.message,data.packet_length-8,0, \
			(struct sockaddr*)&data.addr,sizeof(data.addr)) <= 0) {
			perror("send failed");
		}
		//发送数据长度自加
		data.already_tr += data.packet_length;
		data.packet_counts++;
	}
}
int free_node(void *node, void *nop)
{
	ClientMsg * tmp = (ClientMsg *)node;
	free(tmp);
	return 0;
}
int opt_msg(void *node, void *v)
{
	char ip[16] = {0};
	ClientMsg * tmp = (ClientMsg *)node;
	int value = *(int*)v;

	if (tmp->state != STOP) {
		tmp->tr_time += value;
		tmp->tr_speed = tmp->tr_len/tmp->tr_time/1024;
		tmp->packet_speed = tmp->packet_counts/tmp->tr_time;
	}
	if (inet_ntop(AF_INET, &tmp->addr.sin_addr, ip, 16) < 0) {
		perror("inet net to ip failed");
	}
	printf(DATA_FORMAT,ip,tmp->tr_len, \
		tmp->tr_speed, tmp->packet_counts, tmp->lose_packets, \
		tmp->packet_speed,tmp->state != STOP?"continue":"over");
	tmp->last_time_packet_counts = tmp->packet_counts;
	return 0;
}
int start_udp_recv(TestData * data)
{
	int ret;
	ClientMsg tmp;
	ClientMsg * pmsg = NULL;
	ClientMsgHead * head = &data->head;
	ClientMsg * p = NULL;
	socklen_t len = sizeof(struct sockaddr_in);
	memset(&tmp, 0, sizeof(tmp));
	if (signal(SIGINT,data->sig_handler) == SIG_ERR) {
		perror("register sig hangler failed");
		return -1;
	}
        if (pthread_cond_signal(&data->cond) < 0) {//唤醒线程
                perror("thread awake faild");
                return -1;
	}
	while (1) {
		tmp.tr_len = recvfrom(data->sock, data->message, sizeof(data->message), 0, \
			(struct sockaddr *)&tmp.addr, &len);
		if (tmp.tr_len <= 0) {
			perror("recv error");
			return -1;
		}
		p = head->find_by_addr(head, &tmp);
		if (!p) {
			pthread_mutex_lock(&data->mutex);
			ret = head->add_new_client(head, &tmp);
			pthread_mutex_unlock(&data->mutex);
			if (ret < 0) {
				return -1;
			}
			continue;
		} else {
			p->state = data->message[0];
			if (p->state == STOP) {
				pmsg = (ClientMsg*)(data->message+1);
				p->lose_packets = pmsg->packet_counts - p->packet_counts;
				//解析数据信息
			} else {
				p->tr_len += tmp.tr_len;
				p->packet_counts ++;
			}
		}
	}
	return 0;
}
int start_udp_send(TestData * data)
{
	data->message[0] = START;//发送信息表开始
        if (signal(SIGALRM,data->sig_handler) == SIG_ERR) {
                perror("signal handler register failed");
                return -1;
        }
        if (pthread_cond_signal(&data->cond) < 0) {
                perror("thread awake faild");
                return -1;
        }
        if (set_timer_vs(10,data->span) < 0) {
                perror("set timer failed");
                return -1;
        }
        return 0;
}
int wait_udp_recv_over(TestData * data)
{
        return 0;
}
int wait_udp_send_over(TestData * data)
{
	int ret = 0;
        while (data->percent<100);
        ret = set_timer_vs(0,0);
	if (ret < 0) {
		perror("set time vs failed");
	}
	data->message[0] = STOP;//发送结束标志
	data->cli_msg.tr_len = data->should_tr;
	data->cli_msg.packet_counts = data->packet_counts;
	data->cli_msg.tr_time = data->continue_time;
	data->cli_msg.tr_speed = ((data->rate*1024)/8);
	memcpy(data->message+1,&data->cli_msg,sizeof(ClientMsg));
	if (sendto(data->sock,data->message,sizeof(ClientMsg)+1,0, \
		(struct sockaddr*)&data->addr,sizeof(data->addr)) <= 0) {
		perror("send failed");
	}
	return ret;
}
void release_recv_test_data(TestData * data)
{
	close(data->sock);
	list_for_each(&data->head,free_node,NULL);
	data->head.head = NULL;

        pthread_mutex_destroy(&data->mutex);
        pthread_cond_destroy(&data->cond);
}
void release_send_test_data(TestData * data)
{
	close(data->sock);
        pthread_mutex_destroy(&data->mutex);
        pthread_cond_destroy(&data->cond);
}
int main(int argc, char * argv[])
{
        memset(&data,0,sizeof(data));
	int flag = 0;
        if ((flag = parse_arg(argc,argv,&data)) < 0) {
                goto err_args;
        }
	if (set_init_func(&data,flag) < 0) {
                goto err_args;
	}
        if (data.init && data.init(&data) < 0) {
                goto err_args;
        }
        if (*data.thread_func && create_thread(&data) < 0) {
                goto err_run;
        }
        if (data.start && data.start(&data) < 0) {
                goto err_run;
        }
        if (data.wait_thread_over && data.wait_thread_over(&data) < 0) {
                goto err_run;
        }
        putchar('\n');
        if (data.release) {
                data.release(&data);
        }
        return 0;
err_run:
        if (data.release) {
                data.release(&data);
        }
        return -1;
err_args:
        print_notice();
        return -1;
}


