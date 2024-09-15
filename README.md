
## （一）前置知识：进程切换
### （1）xv6中的进程
- xv6中，每个进程有且只有**一个**用户线程与内核线程
- **用户线程**的空间独立。所有**内核线程**之间共享内存，但拥有单独的内核栈
- 此外，每个CPU拥有一个调度线程
- **调度线程存在的原因**：从旧线程切换到新线程时，调度器（调度程序）需要运行在调度线程上。若调度器运行在旧线程上，此时另一个核心也尝试调用该线程，可能会导致不同处理器使用同一个栈的致命冲突。（xv6 book中原文为——调度程序在旧进程的内核栈上执行是不安全的：其他一些核心可能会唤醒进程并运行它，而在两个不同的核心上使用同一个栈将是一场灾难）

### （2）进程切换流程
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/0818701b2a39499a96c5d7aa8080be92.png#pic_center)
1. 旧进程的用户线程陷入到内核态，将用户程序的寄存器保存在trapframe中，切换到内核线程
2. usertrap()中内核线程调用yield()
3. yield()为当前进程上锁，调用sched()
4. sched()检查进程锁一类的状态，确保安全后，调用swtch(&p->context,&c->context)，保存内核线程的上下文(包括ra,sp)，恢复到当前cpu调度线程的上下文中
5. 切换至调度线程(从context的ra中保存的返回地址进入)
6. 调度线程为旧进程解锁，然后选择一个RUNNABLE的进程，调用swtch(&c->context,&p->context)，切换到新进程的内核线程上下文
7. 切换至新的内核线程(从context的ra中保存的返回地址进入)
8. 内核线程由usertrapret()返回用户空间，切换至用户线程

**要点注意：**
- Swtch只保存被调用者保存的寄存器（callee-saved registers）；调用者保存的寄存器（caller-saved registers）在调用函数之前，会隐式地自动保存在**线程的堆栈上**（汇编代码中可见）
- 如果进程是第一次上处理机，则ra中保存的不是swtch()结束的地方（因为此前该进程没有调用过swtch）。进程创建时，allocproc中会将ra设置为forkret，因此第一次调度会通过forkret直接返回到用户空间
- p->lock的保护： 在调度过程完成之前，保持该内核栈不被其他CPU运行
	○ 改变进程状态为RUNNABLE，防止进程状态的不一致。
	○ swtch发生的上下文切换，防止保存和恢复寄存器不完整。
	○ 停止使用当前内核线程的内核栈，因此防止两个CPU使用同一个内核栈。
	○ 其他：exit和wait之间的交互，避免唤醒丢失，进程在exit时产生的竞争条件等。
- 持有锁时会关闭中断(aquire)

## （二）Uthread: switching between threads
### （1）实验要求
- 注：经评论区小伙伴提醒后发现，本实验即为**协程**的实现。

**协程概念**：
1. 轻量级，系统开销小。
2. 在单线程下实现高并发，也仅支持单核cpu。
3. 由用户程序调度，保存寄存器，实现上下文切换，而非内核进行调度。

本实验完成**用户态**下的线程切换（协程）。在一个进程中模拟多个线程（xv6每个进程本身只有一个线程），然后于用户态下进行线程调度。
uthread的过程是：先通过`thread_create()`创建三个线程，然后在`thread_schedule()`中调度线程，每个线程执行一段代码后，通过`thread_yield()`进入`thread_schedule()`，然后切换上下文，进入新的线程。
与xv6的线程切换基本一致。
### （2）实验步骤
只需要参照xv6中进程调度的过程编写代码即可，其核心无非是context中保存上下文（寄存器信息），调度过程中进行切换。

**1.设置context：**
此处虽然不能直接引用kernel/proc/h，但我们直接把其中的结构体搬过来即可，保存的寄存器内容一致，无需任何修改。
`uthread.c`中添加

```c
struct context
{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```
然后在线程结构体中，添加context字段

```c
struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct context context;       /*上下文，callee寄存器*/
};
```

**2.完善thread_create()**
完全参照`proc.c`中的`allocproc()`函数。
`allocproc()`中，进程被创建后，会将ra设置为forkret，然后在第一次调度时直接返回用户空间开始执行程序。同时，会为进程分配一页内核栈。如下：

```c
void
allocproc(void){
  // ...
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

thread_create()的任务和大同小异，需要设置ra与sp
ra返回的即为任务函数func()的入口
sp设置的即为该线程的栈（`struct thread`中的`char stack[STACK_SIZE]`）

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  memset(&t->context, 0, sizeof(struct context));
  t->context.ra = (uint64)func;
  t->context.sp = (uint64)t->stack + STACK_SIZE; // 栈由高地址向低地址扩展
}
```
注意**栈由高地址向低地址扩展**,所以栈的起始位置是`(uint64)t->stack + STACK_SIZE`

**3.编写thread_switch()**
这是保存旧进程context，恢复到新进程context的汇编代码，将swtch.S的内容复制粘贴到uthread_switch.S中即可

```c
	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
		
	ret    /* return to ra */

```
**thread_switch needs to save/restore only the callee-save registers. Why?**
答：Swtch只保存被调用者保存的寄存器（callee-saved registers）；调用者保存的寄存器（caller-saved registers）在调用函数之前，会隐式地自动保存在**线程的堆栈上**（汇编代码中可见）


**4.完善thread_schedule()**
调用编写好的thread_switch即可完成工作，此处不涉及锁，也没有所谓的调度线程，直接从旧线程的上下文切换到新线程的上下文即可

```c
void 
thread_schedule(void)
{
  struct thread *t, *next_thread;
	
  // ...

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->context, (uint64)&next_thread->context);
  }
  
  // ...
}
```

## （三）Using threads
### （1）实验要求
该哈希表程序存在键值丢失的问题
Why are there missing keys with 2 threads, but not with 1 thread? Identify a sequence of events with 2 threads that can lead to a key being missing. Submit your sequence with a short explanation in answers-thread.txt

答：

```c
the sequence leading to missing key:
p1:
insert(){
    e->next = n;
}
p2:
insert(){
    e->next = n; // the same n like p1 
}
p1:
insert(){
    *p = e;
}
p2:
insert(){
    *p = e; // so the key-value inserted by p1 disappeared
}
```
主要原因就是两个线程访问临界资源——哈希表，导致的冲突。所以，要求在临界区加锁来维持不变量。

### （2）实验思路
先熟悉一下该程序的哈希表：又NBUCKET个桶，随机数先通过取余确定bucket，然后通过put()，用头插法插入key-value

**先有** nthread个进程并行地插入键值对，每个线程插入NKEYS/nthread个键值对，互不相同
**后有** nthread个进程并行地查询，每个线程皆查询NKEYS个键值对
故put()和get()不会同时访问临界资源。又因为get()是只读操作，不会修改临界资源的值，所以，我们只需要给put()的临界区上锁即可。

**1.确定锁结构**
每个桶一个锁即可，不同的桶插入时不会相互干扰。

```c
pthread_mutex_t lock[NBUCKET];
```
**2.锁的初始化与销毁**
`main()`中初始化与销毁锁
```c
// 初始化锁
  for (int i = 0; i < NBUCKET; ++i)
    pthread_mutex_init(&lock[i], NULL);
```

```c
// 销毁锁
  for (int i = 0; i < NBUCKET; ++i)
    pthread_mutex_destroy(&lock[i]);
```
**3.put()中上锁**
在需要修改临界资源的前后上锁，即插入哈希表操作的前后
```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&lock[i]);
    insert(key, value, &table[i], table[i]);
    pthread_mutex_unlock(&lock[i]);
  }
}
```
额外提一点，这里增加桶的数量NBUCKET，会减少访问临界资源的概率，也就减少了锁导致的性能开销，因此也可以大大提高程序的并行效率。

## （四）Barrier
### （1）实验要求
在多线程编程中，Barrier（屏障）是一种同步机制，用于使一组线程在某个特定的同步点（即屏障点）等待，直到所有线程都达到这个点。
该实验要求使用条件变量，编写一个Barrire

### （2）条件变量的使用
条件变量要结合锁来使用，POSIX 标准中提供了三个函数
```c
pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
pthread_cond_signal(&cond);        // wake up one thread sleeping on cond
pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
```

下面以一个经典的生产者-消费者的程序举例
生产者：

```c
pthread_mutex_lock(&mutex);

// 循环检测任务队列是否为满，满则阻塞生产者进程
while ((que->rear+1)%MAXSIZE) == que->front)
{	
	// 调用wait使该线程sleep，同时释放mutex
	// 当被唤醒时，会重新尝试获取mutex，获取失败则继续sleep
    pthread_cond_wait(&NotFull, &mutex);
}

生产，向任务队列添加一个产品

pthread_cond_signal(&NotEmpty);
pthread_mutex_unlock(&mutex);
```

消费者：

```c
pthread_mutex_lock(&pool->mutexPool);

// 循环检测任务队列是否为空，空则阻塞消费者进程
while (que->front == que->rear)
{
    pthread_cond_wait(&notEmpty, &mutex);
}

消费，取走任务队列的一个产品

pthread_cond_signal(&NotFull);
pthread_mutex_unlock(&mutex);
```

### （3）实验思路
如果掌握了条件变量的使用，该实验就轻而易举了。
**思路**：一个线程完成任务后，检测一下完成的线程数是否等于总线程数，若否，则调用`wait`，阻塞等待在barrier处，同时让出cpu。当最后一个线程完成后，调用`broadcast`唤醒所有等待的线程，继续去往下一个barrier。

```c
static void barrier()
{
  pthread_mutex_lock(&bstate.barrier_mutex);
  ++bstate.nthread;
  if(bstate.nthread != nthread)
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  else{
    bstate.nthread = 0;
    ++bstate.round;
    pthread_cond_broadcast(&bstate.barrier_cond); // 所有线程已经完成任务，全部唤醒
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```
注意：
- 此处与条件变量的常规使用有一些不同，在于检测条件是if而不是while。通常情况下，我们需要使用while，因为线程被唤醒之后，检测的条件不一定满足，因此需要循环检测。但此处不会发生这种问题，为线程完成后，bstate.nthread就++了，不会出现减少的情况，因此线程被唤醒后，可以保证满足`bstate.nthread == nthread`的条件，不需要使用while循环检测
- 注意最后一个线程完成后，将bstate.nthread清空，同时round++，这个操作只需要进行一次就行了。
- 注意完成后解锁

此外，不知道为什么，程序没有给出**条件变量与锁的销毁**，安全起见，我们还是补充一下

```c
static void
barrier_destroy(){
  pthread_mutex_destroy(&bstate.barrier_mutex);
  pthread_cond_destroy(&bstate.barrier_cond);
}
```
```c
int
main(){
  // ...
  barrier_init();
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  barrier_destroy();
}
```
