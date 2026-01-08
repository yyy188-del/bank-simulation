#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* ================= 定义与结构体 ================= */

#define TYPE_WITHDRAW 1 // 取款/借款 (消耗资金)
#define TYPE_DEPOSIT  2 // 存款/还款 (增加资金)

// 客户结构体
typedef struct Customer {
    int id;
    int type;           // 1=取款, 2=存款
    double amount;      // 涉及金额
    double duration;    // 办理业务所需时间
    double arriveTime;  // 到达时间
    double enterQ2Time; // 如果进入Q2的时间
    struct Customer* next;
} Customer;

// 队列结构体 (用于 Q1 和 Q2)
typedef struct {
    Customer* front;
    Customer* rear;
    int size;
} Queue;

// 窗口结构体
typedef struct {
    int id;
    int isBusy;         // 0=空闲, 1=忙碌
    double finishTime;  // 当前服务结束时间
} Window;

// 事件类型
typedef enum {
    EVENT_ARRIVE,       // 客户到达
    EVENT_DEPARTURE     // 客户离开（服务结束）
} EventType;

// 事件节点 (优先队列)
typedef struct EventNode {
    double time;        // 事件发生时间
    EventType type;
    Customer* customer; // 关联的客户
    int windowId;       // 如果是离开事件，记录是哪个窗口
    struct EventNode* next;
} EventNode;

/* ================= 全局变量 ================= */

double G_TotalFunds = 0;        // 银行当前资金
double G_CurrentTime = 0;       // 当前模拟时钟
double G_CloseTime = 0;         // 关门时间

long long G_ServedCount = 0;    // 成功服务的客户数
double G_TotalStayTime = 0;     // 总逗留时间累计

/* =============== 动态生成相关全局变量 =============== */
int G_NextCustomerId = 1;       // 下一个客户的ID（不需要总数）
double G_ArrivalRate = 1.0;     // 客户到达率（每分钟多少个客户）

/* ================= 队列操作 ================= */

void q_init(Queue* q) {
    q->front = q->rear = NULL;
    q->size = 0;
}

int q_empty(Queue* q) { return q->size == 0; }

void q_push(Queue* q, Customer* c) {
    c->next = NULL;
    if (q->rear == NULL) {
        q->front = q->rear = c;
    } else {
        q->rear->next = c;
        q->rear = c;
    }
    q->size++;
}

void q_push_front(Queue* q, Customer* c) {
    if (q->front == NULL) {
        q->front = q->rear = c;
        c->next = NULL;
    } else {
        c->next = q->front;
        q->front = c;
    }
    q->size++;
}

Customer* q_pop(Queue* q) {
    if (q->front == NULL) return NULL;
    Customer* c = q->front;
    q->front = q->front->next;
    if (q->front == NULL) q->rear = NULL;
    c->next = NULL;
    q->size--;
    return c;
}

Customer* q_remove_after(Queue* q, Customer* prev) {
    Customer* target;
    if (prev == NULL) {
        return q_pop(q);
    } else {
        target = prev->next;
        if (target == NULL) return NULL;
        prev->next = target->next;
        if (target == q->rear) {
            q->rear = prev;
        }
        target->next = NULL;
        q->size--;
        return target;
    }
}

/* ================= 事件列表操作 ================= */

EventNode* event_list = NULL;

void event_insert(double time, EventType type, Customer* c, int winId) {
    EventNode* node = (EventNode*)malloc(sizeof(EventNode));
    node->time = time;
    node->type = type;
    node->customer = c;
    node->windowId = winId;
    node->next = NULL;

    if (event_list == NULL || node->time < event_list->time) {
        node->next = event_list;
        event_list = node;
    } else {
        EventNode* curr = event_list;
        while (curr->next != NULL && curr->next->time <= node->time) {
            curr = curr->next;
        }
        node->next = curr->next;
        curr->next = node;
    }
}

EventNode* event_pop() {
    if (event_list == NULL) return NULL;
    EventNode* node = event_list;
    event_list = event_list->next;
    return node;
}

/* ================= 核心业务逻辑 ================= */

void try_assign_windows(Queue* Q1, Queue* Q2, Window* windows, int numWindows) {
    for (int i = 0; i < numWindows; i++) {
        if (windows[i].isBusy) continue;
        if (q_empty(Q1)) break;

        Customer* c = Q1->front;
        int canService = 0;
        
        if (c->type == TYPE_DEPOSIT) {
            canService = 1;
        } else {
            if (G_TotalFunds >= c->amount) {
                canService = 1;
            } else {
                Customer* failed = q_pop(Q1);
                failed->enterQ2Time = G_CurrentTime;
                q_push(Q2, failed);
                printf("[%.1f] 客户%d(取%.0f) 资金不足(库房%.0f) -> 移入Q2等待\n",
                       G_CurrentTime, failed->id, failed->amount, G_TotalFunds);
                i--;
                continue;
            }
        }

        if (canService) {
            c = q_pop(Q1);
            windows[i].isBusy = 1;
            
            if (c->type == TYPE_WITHDRAW) {
                G_TotalFunds -= c->amount;
            }

            double finishTime = G_CurrentTime + c->duration;
            windows[i].finishTime = finishTime;
            event_insert(finishTime, EVENT_DEPARTURE, c, i);

            printf("[%.1f] 窗口%d 开始服务 客户%d(%s %.0f)\n",
                   G_CurrentTime, i+1, c->id, (c->type==1?"取":"存"), c->amount);
        }
    }
}

void check_Q2_after_deposit(Queue* Q1, Queue* Q2, double depositAmount) {
    if (q_empty(Q2)) return;

    printf("[%.1f] 存款发生(额度%.0f)，检查Q2队列... (当前资金: %.0f)\n",
           G_CurrentTime, depositAmount, G_TotalFunds);

    double threshold = G_TotalFunds - depositAmount;
    int initialSize = Q2->size;
    int count = 0;
    Customer* prev = NULL;
    Customer* curr = Q2->front;

    while (count < initialSize && curr != NULL) {
        if (G_TotalFunds <= threshold) {
            printf("    -> 资金已回落至阈值(%.0f)，停止检查Q2\n", threshold);
            break;
        }

        if (curr->amount <= G_TotalFunds) {
            Customer* lucky = NULL;
            if (prev == NULL) {
                lucky = q_pop(Q2);
                curr = Q2->front;
            } else {
                lucky = q_remove_after(Q2, prev);
                curr = prev->next;
            }
            printf("    -> 唤醒客户%d(取%.0f)，移回Q1队头\n", lucky->id, lucky->amount);
            q_push_front(Q1, lucky);
        } else {
            prev = curr;
            curr = curr->next;
        }
        count++;
    }
}

/* ================= 动态客户生成函数 ================= */

Customer* create_random_customer(int id, double arriveTime) {
    Customer* c = (Customer*)malloc(sizeof(Customer));
    c->id = id;
    c->arriveTime = arriveTime;
    
    // 随机决定业务类型（70%取款，30%存款）
    if (rand() % 10 < 7) {
        c->type = TYPE_WITHDRAW;
        c->amount = 100 + rand() % 2000; // 100~2100
    } else {
        c->type = TYPE_DEPOSIT;
        c->amount = 100 + rand() % 2000;
    }
    
    // 随机服务时间（1-6分钟）
    c->duration = 1.0 + (rand() % 50) / 10.0;
    c->enterQ2Time = 0;
    c->next = NULL;
    
    return c;
}

// 安排下一个客户到达（关键修改：没有总数限制）
void schedule_next_arrival() {
    // 如果当前时间已经超过或等于关门时间，不再安排新客户
    if (G_CurrentTime >= G_CloseTime) {
        return;
    }
    
    // 使用指数分布计算下一个到达间隔（更真实）
    double u = (double)rand() / RAND_MAX;
    double interval = -log(1.0 - u) / G_ArrivalRate;
    
    double nextTime = G_CurrentTime + interval;
    
    // 如果下一个到达时间在关门前，则安排这个客户
    if (nextTime < G_CloseTime) {
        Customer* nextCust = create_random_customer(G_NextCustomerId, nextTime);
        G_NextCustomerId++;
        event_insert(nextTime, EVENT_ARRIVE, nextCust, -1);
        
        // 可选：打印安排信息
        // printf("[调度] 已安排客户%d在%.2f到达\n", G_NextCustomerId-1, nextTime);
    }
}

/* ================= 主程序 ================= */

int main() {
    srand((unsigned)time(NULL));

    int N_Win;
    double initialFunds;

    printf("=== 银行业务模拟 (完全动态客户生成) ===\n");
    printf("说明：客户在整个营业时间内随机到达，没有固定总数\n");
    
    printf("输入客户到达率(每分钟多少个客户，推荐0.5-2.0): ");
    scanf("%lf", &G_ArrivalRate);
    printf("输入窗口数量: ");
    scanf("%d", &N_Win);
    printf("输入初始资金: ");
    scanf("%lf", &initialFunds);
    printf("输入关门时间(分钟): ");
    scanf("%lf", &G_CloseTime);

    G_TotalFunds = initialFunds;

    // 初始化窗口
    Window* windows = (Window*)malloc(sizeof(Window) * N_Win);
    for (int i = 0; i < N_Win; i++) {
        windows[i].id = i;
        windows[i].isBusy = 0;
        windows[i].finishTime = 0;
    }

    Queue Q1, Q2;
    q_init(&Q1);
    q_init(&Q2);

    printf("\n--- 开始模拟，客户将随机到达直到关门 ---\n");
    
    // 关键修改：不输入客户总数，直接安排第一个客户
    // 第一个客户在时间0到达
    Customer* firstCust = create_random_customer(G_NextCustomerId, 0.0);
    G_NextCustomerId++;
    event_insert(0.0, EVENT_ARRIVE, firstCust, -1);
    printf("第一个客户ID:%d 将在时间0到达 类型:%s 金额:%.0f 时长:%.1f\n",
           firstCust->id, (firstCust->type==1?"取款":"存款"), 
           firstCust->amount, firstCust->duration);

    // 事件循环
    while (event_list != NULL) {
        EventNode* e = event_pop();

        if (e->time > G_CloseTime) {
            G_CurrentTime = G_CloseTime;
            free(e);
            continue;
        }

        G_CurrentTime = e->time;

        if (e->type == EVENT_ARRIVE) {
            // 处理到达事件
            Customer* c = e->customer;
            q_push(&Q1, c);
            
            printf("[%.2f] 客户%d到达 (%s %.0f, 需要%.1f分钟)\n",
                   G_CurrentTime, c->id,
                   (c->type==1?"取款":"存款"), c->amount, c->duration);
            
            // 尝试分配窗口
            try_assign_windows(&Q1, &Q2, windows, N_Win);
            
            // 关键：安排下一个客户到达
            schedule_next_arrival();

        } else if (e->type == EVENT_DEPARTURE) {
            // 处理离开事件
            int winId = e->windowId;
            Customer* c = e->customer;

            double stayTime = G_CurrentTime - c->arriveTime;
            G_TotalStayTime += stayTime;
            G_ServedCount++;

            windows[winId].isBusy = 0;

            printf("[%.2f] 客户%d 离开 (窗口%d释放), 逗留:%.2f\n",
                   G_CurrentTime, c->id, winId+1, stayTime);

            if (c->type == TYPE_DEPOSIT) {
                G_TotalFunds += c->amount;
                check_Q2_after_deposit(&Q1, &Q2, c->amount);
            }

            free(c);
            try_assign_windows(&Q1, &Q2, windows, N_Win);
        }

        free(e);
    }

    // 处理关门时还在排队的人
    G_CurrentTime = G_CloseTime;
    
    printf("\n[%.2f] 银行关门，处理剩余排队客户...\n", G_CloseTime);
    
    // 清空 Q1
    while (!q_empty(&Q1)) {
        Customer* c = q_pop(&Q1);
        double stay = G_CloseTime - c->arriveTime;
        G_TotalStayTime += stay;
        G_ServedCount++;
        printf("  客户%d 未办理业务离开，等待:%.2f分钟\n", c->id, stay);
        free(c);
    }
    
    // 清空 Q2
    while (!q_empty(&Q2)) {
        Customer* c = q_pop(&Q2);
        double stay = G_CloseTime - c->arriveTime;
        G_TotalStayTime += stay;
        G_ServedCount++;
        printf("  客户%d (Q2等待) 未办理业务离开，等待:%.2f分钟\n", c->id, stay);
        free(c);
    }

    // 输出结果
    printf("\n=== 模拟结束 ===\n");
    printf("关门时间: %.2f分钟\n", G_CloseTime);
    printf("最终资金: %.2f元\n", G_TotalFunds);
    printf("总到达客户数: %d人\n", G_NextCustomerId - 1);
    printf("总服务客户数: %lld人\n", G_ServedCount);
    if (G_ServedCount > 0) {
        printf("平均逗留时间: %.4f分钟\n", G_TotalStayTime / G_ServedCount);
    } else {
        printf("无客户。\n");
    }

    free(windows);
    return 0;
}