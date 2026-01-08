#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { TYPE_A = 1, TYPE_B = 2 } BizType;

typedef struct Customer {
    int id;
    double arriveTime;
    BizType type;
    double amount;
    double serviceTime;
    double enterTime;   // = arriveTime
    struct Customer* next;
} Customer;

typedef struct {
    Customer* front;
    Customer* rear;
    int size;
} Queue;

typedef struct {
    int id;
    double arriveTime;
    BizType type;
    double amount;
    double serviceTime;
} CustomerRec;

/* ---------- Queue (linked) ---------- */
void q_init(Queue* q) {
    q->front = q->rear = NULL;
    q->size = 0;
}
int q_empty(Queue* q) { return q->size == 0; }

void q_push(Queue* q, Customer* node) {
    node->next = NULL;
    if (q->rear == NULL) {
        q->front = q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }
    q->size++;
}
Customer* q_pop(Queue* q) {
    if (q->front == NULL) return NULL;
    Customer* node = q->front;
    q->front = q->front->next;
    if (q->front == NULL) q->rear = NULL;
    node->next = NULL;
    q->size--;
    return node;
}

/* ---------- Helpers ---------- */
int cmp_arrive(const void* a, const void* b) {
    const CustomerRec* x = (const CustomerRec*)a;
    const CustomerRec* y = (const CustomerRec*)b;
    if (x->arriveTime < y->arriveTime) return -1;
    if (x->arriveTime > y->arriveTime) return 1;
    return x->id - y->id;
}

Customer* make_customer_from_rec(const CustomerRec* r) {
    Customer* c = (Customer*)malloc(sizeof(Customer));
    c->id = r->id;
    c->arriveTime = r->arriveTime;
    c->type = r->type;
    c->amount = r->amount;
    c->serviceTime = r->serviceTime;
    c->enterTime = r->arriveTime;
    c->next = NULL;
    return c;
}

/* 关门清空并计入逗留（按 closeTime 离开） */
void clear_queue_at_close(Queue* q, double closeTime, double* sumStay, long long* count) {
    while (!q_empty(q)) {
        Customer* c = q_pop(q);
        double stay = closeTime - c->enterTime;
        if (stay < 0) stay = 0; // 防御
        *sumStay += stay;
        (*count)++;
        free(c);
    }
}

/* ---------- Input / Random ---------- */
void input_customers(CustomerRec* arr, int n) {
    int i;// 输入格式：到达时间 type(1=A取/借,2=B存/还) 金额 服务时长
    for (i = 0; i < n; i++) {
        arr[i].id = i + 1;
        int tp;
        printf("客户%d: arriveTime type(1=A,2=B) amount serviceTime = ", i + 1);
        if (scanf("%lf %d %lf %lf", &arr[i].arriveTime, &tp, &arr[i].amount, &arr[i].serviceTime) != 4) {
            printf("输入错误。\n");
            exit(1);
        }
        arr[i].type = (tp == 1 ? TYPE_A : TYPE_B);
    }
}

double urand() { return (double)rand() / (double)RAND_MAX; }

/* 随机生成示例：到达时间递增，type随机，金额/服务时长在区间内 */
void random_customers(CustomerRec* arr, int n) {
    double t = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        arr[i].id = i + 1;
        t += 0.5 + urand() * 2.0; // 到达间隔 0.5~2.5
        arr[i].arriveTime = t;
        arr[i].type = (urand() < 0.5 ? TYPE_A : TYPE_B);
        arr[i].amount = 50 + urand() * 450;      // 50~500
        arr[i].serviceTime = 0.5 + urand() * 2;  // 0.5~2.5
    }
}

/* ---------- Simulation Core ---------- */
int main() {
    int mode;
    int N;
    double S0, closeTime;

    printf("银行业务模拟(事件驱动)\n");
    printf("选择模式: 1=手动输入  2=随机生成 : ");
    scanf("%d", &mode);

    printf("输入客户数量N: ");
    scanf("%d", &N);
    if (N <= 0) {
        printf("N必须>0\n");
        return 0;
    }

    printf("输入初始资金S0: ");
    scanf("%lf", &S0);
    printf("输入关门时间Tclose: ");
    scanf("%lf", &closeTime);

    CustomerRec* recs = (CustomerRec*)malloc(sizeof(CustomerRec) * N);
    if (mode == 1) {
        input_customers(recs, N);
    } else {
        srand((unsigned)time(NULL));
        random_customers(recs, N);
        printf("\n--- 随机生成客户数据(可用于实验) ---\n");
        int i;
        for (i = 0; i < N; i++) {
            printf("id=%d arrive=%.2f type=%d amount=%.2f service=%.2f\n",
                   recs[i].id, recs[i].arriveTime, recs[i].type, recs[i].amount, recs[i].serviceTime);
        }
        printf("---------------------------------\n\n");
    }

    // 排序（保证按到达事件顺序）
    qsort(recs, N, sizeof(CustomerRec), cmp_arrive);

    Queue Q1, Q2;
    q_init(&Q1);
    q_init(&Q2);

    double S = S0;           // 当前资金
    double t = 0.0;          // 模拟时钟
    int i = 0;               // 下一个到达客户索引
    double sumStay = 0.0;
    long long count = 0;

    while (1) {
        // 关门条件：时间到且没必要继续服务（题意：关门时所有客户立即离开）
        if (t >= closeTime) break;

        // 把所有在时刻t之前到达的客户入Q1（且到达时间必须 < closeTime 才能进入）
        while (i < N && recs[i].arriveTime <= t && recs[i].arriveTime < closeTime) {
            Customer* c = make_customer_from_rec(&recs[i]);
            q_push(&Q1, c);
            i++;
        }

        // 如果窗口空闲且Q1为空：跳到下一个到达时间（事件推进）
        if (q_empty(&Q1)) {
            if (i < N && recs[i].arriveTime < closeTime) {
                t = recs[i].arriveTime; // 直接跳时钟
                continue;
            } else {
                // 没有新到达了，直接等到关门
                t = closeTime;
                break;
            }
        }

        // 服务Q1队首（任意时刻只开一个窗口，所以一次只处理一个）
        Customer* c = q_pop(&Q1);
        if (c->type == TYPE_A) {
            if (c->amount <= S) {
                // 满足并服务耗时
                t += c->serviceTime;
                if (t > closeTime) {
                    // 服务跨过关门：按题意关门时立即离开，视为未完成/或直接离开
                    // 这里按“关门立即离开”处理：离开时间=closeTime
                    double stay = closeTime - c->enterTime;
                    if (stay < 0) stay = 0;
                    sumStay += stay;
                    count++;
                    free(c);
                    t = closeTime;
                    break;
                }
                S -= c->amount;
                double stay = t - c->enterTime;
                sumStay += stay;
                count++;
                free(c);
            } else {
                // 不满足：立即进入Q2等待（不耗时）
                q_push(&Q2, c);
            }
        } else { // TYPE_B
            double S_before = S;
            t += c->serviceTime;
            if (t > closeTime) {
                // 跨关门，按关门离开
                double stay = closeTime - c->enterTime;
                if (stay < 0) stay = 0;
                sumStay += stay;
                count++;
                free(c);
                t = closeTime;
                break;
            }
            S += c->amount;
            double stay = t - c->enterTime;
            sumStay += stay;
            count++;
            free(c);

            // 检查Q2（不耗时）
            int len = Q2.size;
            int checked = 0;
            while (!q_empty(&Q2) && checked < len && S > S_before) {
                Customer* x = q_pop(&Q2);
                checked++;
                if (x->amount <= S) {
                    S -= x->amount;
                    // 检查不耗时，所以离开时间仍为t
                    double st = t - x->enterTime;
                    if (st < 0) st = 0;
                    sumStay += st;
                    count++;
                    free(x);
                } else {
                    q_push(&Q2, x); // 放回队尾
                }
            }
        }
    }

    // 关门：剩余队列客户立即离开
    clear_queue_at_close(&Q1, closeTime, &sumStay, &count);
    clear_queue_at_close(&Q2, closeTime, &sumStay, &count);

    if (count == 0) {
        printf("没有完成统计的客户。\n");
    } else {
        printf("\n统计客户数 = %lld\n", count);
        printf("平均逗留时间 = %.6f\n", sumStay / (double)count);
    }

    free(recs);
    return 0;
}
