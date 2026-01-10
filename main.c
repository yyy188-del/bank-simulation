#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>


#define take 1 // 取款
#define save 2  // 存款

// 客户结构体
typedef struct Customer
{
    int id;
    int type;           // 1=取款, 2=存款
    double amt;         // 涉及金额
    double duration;    // 办理业务所需时间
    double arriveTime;  // 到达时间
    double enterQ2Time; // 如果进入Q2的时间
    struct Customer *next;
} Customer;

// 队列结构体 (用于 Q1 和 Q2)
typedef struct
{
    Customer *front;
    Customer *rear;
    int size;
} Queue;

// 窗口结构体
typedef struct
{
    int id;
    int flag;                // 0=空闲, 1=忙碌
    double finishTime;       // 当前服务结束时间
    Customer *currCustomer;  // 正在服务的顾客
} Window;

// 事件类型
typedef enum
{
    EVENT_ARRIVE,   // 客户到达
    EVENT_DEPARTURE // 客户离开
} EventType;

// 事件节点
typedef struct EventNode
{
    double time; // 事件发生时间
    EventType type;
    Customer *customer; // 关联的客户
    int windowId;       // 如果是离开事件，记录是哪个窗口
    struct EventNode *next;
} EventNode;


double moneys = 0;  // 银行当前资金
double nowTime = 0; // 当前模拟时钟
double closeTime = 0;   // 关门时间

long long servedCount = 0; // 成功服务的客户数
double totalStayTime = 0;  // 总逗留时间累计

int nextId = 1;   // 下一个客户的ID
double arrivalRate = 1.0; // 客户到达率（每分钟多少个客户）


void init(Queue *q)
{
    q->front = q->rear = NULL;
    q->size = 0;
}

int empty(Queue *q) { return q->size == 0; }

void push(Queue *q, Customer *c)
{
    c->next = NULL;
    if (q->rear == NULL)
    {
        q->front = q->rear = c;
    }
    else
    {
        q->rear->next = c;
        q->rear = c;
    }
    q->size++;
}

void push_front(Queue *q, Customer *c)
{
    if (q->front == NULL)
    {
        q->front = q->rear = c;
        c->next = NULL;
    }
    else
    {
        c->next = q->front;
        q->front = c;
    }
    q->size++;
}

Customer *pop(Queue *q)
{
    if (q->front == NULL)
        return NULL;
    Customer *c = q->front;
    q->front = q->front->next;
    if (q->front == NULL)
        q->rear = NULL;
    c->next = NULL;
    q->size--;
    return c;
}

Customer *take_target(Queue *q, Customer *prev)
{
    Customer *target;
    if (prev == NULL)
    {
        return pop(q);
    }
    else
    {
        target = prev->next;
        if (target == NULL)
            return NULL;
        prev->next = target->next;
        if (target == q->rear)
        {
            q->rear = prev;
        }
        target->next = NULL;
        q->size--;
        return target;
    }
}


EventNode *event_list = NULL;

void event_insert(double time, EventType type, Customer *c, int winId)
{
    EventNode *node = (EventNode *)malloc(sizeof(EventNode));
    node->time = time;
    node->type = type;
    node->customer = c;
    node->windowId = winId;
    node->next = NULL;

    if (event_list == NULL || node->time < event_list->time)
    {
        node->next = event_list;
        event_list = node;
    }
    else
    {
        EventNode *curr = event_list;
        while (curr->next != NULL && curr->next->time <= node->time)
        {
            curr = curr->next;
        }
        node->next = curr->next;
        curr->next = node;
    }
}

EventNode *event_pop()
{
    if (event_list == NULL)
        return NULL;
    EventNode *node = event_list;
    event_list = event_list->next;
    return node;
}


void try_windows(Queue *Q1, Queue *Q2, Window *windows, int numWindows)
{
    int i;
    for (i = 0; i < numWindows; i++)
    {
        if (windows[i].flag)
            continue;
        if (empty(Q1))
            break;
        Customer *c = Q1->front;
        int canService = 0;
        if (c->type == save)
        {
            canService = 1;
        }
        else
        {
            if (moneys >= c->amt)
            {
                canService = 1;
            }
            else
            {
                Customer *failed = pop(Q1);
                failed->enterQ2Time = nowTime;
                //                记录这个客户进入Q2队列的时间
                push(Q2, failed);
                printf("[%.2f] 客户%d(取%.0f) 资金不足(库房%.0f) -> 移入Q2等待\n",
                       nowTime, failed->id, failed->amt, moneys);
                i--;
                continue;
            }
        }

        if (canService)
        {
            c = pop(Q1);
            windows[i].flag = 1;
            windows[i].currCustomer = c; // 记录当前服务的客户
            if (c->type == take)
            {
                moneys -= c->amt;
            }

            double finishTime = nowTime + c->duration;
            //            计算服务结束时间 = 当前时间 + 服务时长
            windows[i].finishTime = finishTime;
            //            记录窗口的结束时间
            event_insert(finishTime, EVENT_DEPARTURE, c, i); // 插入时间轴
            printf("[%.2f] 窗口%d 开始服务 客户%d(%s %.0f)\n",
                   nowTime, i + 1, c->id, (c->type == 1 ? "取" : "存"), c->amt);
        }
    }
}


void check(Queue *Q1, Queue *Q2, double depositamt)
{
    if (empty(Q2))
        return;

    printf("[%.2f] 存款发生(额度%.0f)，检查Q2队列... (当前资金: %.0f)\n",
           nowTime, depositamt, moneys);

    double threshold = moneys - depositamt;
    int Size = Q2->size;
    int count = 0;
    Customer *prev = NULL;
    Customer *curr = Q2->front;
    //	遍历Q2队列

    while (count < Size && curr != NULL)
    {
        if (moneys <= threshold)
        {
            printf("    -> 资金已回落至阈值(%.0f)，停止检查Q2\n", threshold);
            break;
        }

        if (curr->amt <= moneys)
        {
            Customer *lucky = NULL;
            //            条件判断: 当前客户的取款金额 <= 当前总资金
            if (prev == NULL)
            {
                lucky = pop(Q2);
                curr = Q2->front;
            }
            else
            {
                lucky = take_target(Q2, prev);
                curr = prev->next;
            }
            printf("    -> 唤醒客户%d(取%.0f)，移回Q1队头\n", lucky->id, lucky->amt);
            push_front(Q1, lucky);
        }
        else
        {
            prev = curr;
            curr = curr->next;
        }
        count++;
    }
}



Customer *create_customer(int id, double arriveTime)
{
    Customer *c = (Customer *)malloc(sizeof(Customer));
    c->id = id;
    c->arriveTime = arriveTime;

    if (rand() % 10 < 7)
    {
        c->type = take;
        c->amt = 100 + rand() % 2000; // 100~2100
    }
    else
    {
        c->type = save;
        c->amt = 100 + rand() % 2000;
    }

    // 随机服务时间（1-6分钟）
    c->duration = 1.0 + (rand() % 50) / 10.0;
    c->enterQ2Time = 0;
    c->next = NULL;

    return c;
}

// 安排下一个客户到达
void next_arrival()
{
    // 如果当前时间已经超过或等于关门时间，不再安排新客户
    if (nowTime >= closeTime)
    {
        return;
    }

    double u = (double)rand() / RAND_MAX;
    double interval = -log(1.0 - u) / arrivalRate;

    double nextTime = nowTime + interval;

    //  如果下一个到达时间在关门前，则安排这个客户
    if (nextTime < closeTime)
    {
        Customer *nextCust = create_customer(nextId, nextTime);
        nextId++;
        event_insert(nextTime, EVENT_ARRIVE, nextCust, -1);

    }
}


int main()
{
    srand((unsigned)time(NULL));

    int N_Win;
    double fund;

    printf("=== 银行业务模拟 (完全动态客户生成) ===\n");
    printf("说明：客户在整个营业时间内随机到达，没有固定总数\n");

    printf("输入客户到达率(每分钟多少个客户，推荐0.5-2.0): ");
    scanf("%lf", &arrivalRate);
    printf("输入窗口数量: ");
    scanf("%d", &N_Win);
    printf("输入初始资金: ");
    scanf("%lf", &fund);
    printf("输入关门时间(分钟): ");
    scanf("%lf", &closeTime);

    moneys = fund;

    // 初始化窗口
    Window *windows = (Window *)malloc(sizeof(Window) * N_Win);
    int i;
    for (i = 0; i < N_Win; i++)
    {
        windows[i].id = i;
        windows[i].flag = 0;
        windows[i].finishTime = 0;
    }

    Queue Q1, Q2;
    init(&Q1);
    init(&Q2);

    printf("\n--- 开始模拟，客户将随机到达直到关门 ---\n");

    // 第一个客户在时间0到达
    Customer *firster = create_customer(nextId, 0.0);
    nextId++;
    event_insert(0.0, EVENT_ARRIVE, firster, -1);
    printf("第一个客户ID:%d 将在时间0到达 类型:%s 金额:%.0f 时长:%.1f\n",
           firster->id, (firster->type == 1 ? "取款" : "存款"),
           firster->amt, firster->duration);

    // 事件循环
    while (event_list != NULL)
    {
        EventNode *e = event_pop();
        //       从事件链表中弹出最早的事件
        if (e->time > closeTime)
        {
            nowTime = closeTime;
            free(e);
            continue;
            //            处理剩余排队客户
        }
        // 边界检查,确保不会处理关门后的事件
        nowTime = e->time;

        if (e->type == EVENT_ARRIVE)
        {
            // 处理到达事件
            Customer *c = e->customer;
            push(&Q1, c);

            printf("[%.2f] 客户%d到达 (%s %.0f, 需要%.1f分钟)\n",
                   nowTime, c->id,
                   (c->type == 1 ? "取款" : "存款"), c->amt, c->duration);

            // 多窗口，尝试分配窗口
            try_windows(&Q1, &Q2, windows, N_Win);

            // 安排下一个客户到达
            next_arrival();
        }
        else if (e->type == EVENT_DEPARTURE)
        {
            // 处理离开事件
            int winId = e->windowId;
            Customer *c = e->customer;

            double stayTime = nowTime - c->arriveTime;
            totalStayTime += stayTime;
            servedCount++;

            windows[winId].flag = 0;
            windows[winId].currCustomer = NULL; // 清除当前客户

            printf("[%.2f] 客户%d 离开 (窗口%d释放), 逗留:%.2f\n",
                   nowTime, c->id, winId + 1, stayTime);

            if (c->type == save)
            {
                moneys += c->amt;
                check(&Q1, &Q2, c->amt);
            }

            free(c);
            try_windows(&Q1, &Q2, windows, N_Win);
        }

        free(e);
    }

    // 处理关门时还在排队的人
    nowTime = closeTime;

    printf("\n[%.2f] 银行关门，处理剩余排队客户...\n", closeTime);

    // 统计仍在服务的窗口（即正在办理业务的客户）

    for (i = 0; i < N_Win; i++)
    {
        if (windows[i].flag)
        {
            // 计算这个客户的逗留时间
            Customer *c = windows[i].currCustomer; 
            if (c != NULL)
            {
                double stay = closeTime - c->arriveTime;
                totalStayTime += stay;
                servedCount++;
                printf("  客户%d (正在窗口服务) 在关门时立即完成服务，逗留:%.2f分钟\n",
                       c->id, stay);

                // 如果是存款，需要更新资金
                if (c->type == save) {
                    moneys += c->amt;
                }
                free(c);
            }
            windows[i].flag = 0;
        }
    }

    // 清空 Q1
    while (!empty(&Q1))
    {
        Customer *c = pop(&Q1);
        double stay = closeTime - c->arriveTime;
        totalStayTime += stay;
        servedCount++;
        printf("  客户%d 未办理业务离开，等待:%.2f分钟\n", c->id, stay);
        free(c);
    }

    // 清空 Q2
    while (!empty(&Q2))
    {
        Customer *c = pop(&Q2);
        double stay = closeTime - c->arriveTime;
        totalStayTime += stay;
        servedCount++;
        printf("  客户%d (Q2等待) 未办理业务离开，等待:%.2f分钟\n", c->id, stay);
        free(c);
    }

    // 输出结果
    printf("\n=== 模拟结束 ===\n");
    printf("关门时间: %.2f分钟\n", closeTime);
    printf("最终资金: %.2f元\n", moneys);
    printf("总到达客户数: %d人\n", nextId - 1);
    printf("总服务客户数: %lld人\n", servedCount);
    if (servedCount > 0)
    {
        printf("平均逗留时间: %.4f分钟\n", totalStayTime / servedCount);
    }
    else
    {
        printf("无客户。\n");
    }

    free(windows);
    return 0;
}