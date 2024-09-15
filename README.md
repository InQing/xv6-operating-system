## （一）前置知识：从用户空间陷入

### （1）trap概述流程图
以系统调用write为例，先概括一下trap的流程
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/1c4b4911f23646ab90cc59c929786f8f.png#pic_center)
### （2）trap中的寄存器
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/06c2dee297fb4bb783c5b5b58634545e.png)
### （3）trap的详细流程
**1.硬件操作（主要是硬件指令ecall）**
- 如果陷阱是设备中断，并且状态SIE位被清空（被关中断了），则不执行以下任何操作。
- 清除SIE以禁用中断。
- 将**用户pc保存到sepc**。
- 将当前模式（用户或管理）保存在状态的SPP位中。
- 设置scause以反映产生陷阱的原因。
- 将模式设置为管理模式。
- 将stvec复制到pc。
- 在新的pc上开始执行，转至trampoline.S中执行uservec。
**注：**硬件不操作，由软件完成的事：切换至内核页表、切换至内核栈、保存寄存器（目的：提高软件灵活性）

**2.汇编指令uservec**
- (前置）内核模式在返回用户模式之前，将sscratch寄存器的值设置为指向trapframe的指针
-  csrrw指令交换sscratch寄存器和a0寄存器的内容，用户代码的a0被保存在sscratch中，现在a0拥有指向trapframe的指针
-  读取a0的值，在trapframe中保存所有用户寄存器
- 将stap切换至内核页表
- 调用trap.c中的usertrap（转向C代码）
**注：** trampoline蹦床页面在内核与用户空间都有映射，并且映射在同一个虚拟地址上，所以在用户态下（切换页表前）可以跳转到trapframe处

**3.usertrap**
- 改变stvec为kernelvec，代表处理内核空间中的trap；
- 保存sepc（用户空间pc）避免usertrap中的上下文切换导致sepc被覆写（此时sepc会+4，指向ecall的下一条指令）
- 根据trap类型执行不同处理：系统调用——syscall、设备中断——devintr、其他异常——杀死用户进程；
- 检查相应状态，进行操作（跳转入usertrapret/时间片用完调用yield让出CPU/杀死进程）

开始恢复状态，返回用户空间
**4.usertrapret**
- 关闭中断，将stvec修改为指向uservec（以便后续的trap从stvec中跳转至uservec）；
- 保存内核相关参数：包括当前CPU的hartid、usertrap的地址和内核页表的地址等；
- 清空SPP位同时允许中断；
- 设置sepc为之前保存的用户程序计数器
- 调用userret

**5.uesrret**
- 切换回用户页表
- 复制trapframe保存的用户a0到sscratch；
- 从trapframe中恢复寄存器；
- 使用csrrw交换a0和sscratch -> a0为用户的a0，sscratch为陷阱帧
- 调用sret返回用户空间
- 从sepc处的指令继续执行用户程序

至此，trap结束

## （二）RISC-V assembly
(1)Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?

answers：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/3e91a136aab1481daf0fc3eec209cfae.png#pic_center)

函数的参数寄存器为a0~a7；main中printf的参数13保存在a2中

(2)Where is the call to function f in the assembly code for main? Where is the call to g?

answers：并没有直接的调用，g(x)被内联到了f(x)中（相当于代码直接插入，而非函数调用），而f(x)又被内联到了main中

(3)At what address is the function printf located?

answers：注释中已经写出来了，就是0x630

(4)What value is in the register ra just after the jalr to printf in main?

answers：使用jalr跳转printf后，需要保存返回地址到ra，以供printf结束后回到main继续执行指令。
所以jalr会将PC+4，也就是下一条指令的地址存入ra中。
当前PC指向0x34，故ra=PC+4=0x38

(5)Run the following code.

	unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i);
      
What is the output? Here's an ASCII table that maps bytes to characters.
The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set i to in order to yield the same output? Would you need to change 57616 to a different value?

answers：57616 = 0xe110；0x00646c72小端存储为72-6c-64-00，对照ASCII码表转为字符
72:r，6c:l，64:d，00:充当字符串结尾标识
故输出：He110 World
若为大端存储，i应颠倒为0x726c6400，不需改变57616

(6)In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen?

	printf("x=%d y=%d", 3);

answers：根据call的汇编代码
li	a2,13
li	a1,12
可以看出，printf的参数从a1、a2……中取，所以这里应该也是a2寄存器中的值

## （三）Backtrace
### （1）实验要求
写一个回溯函数Backtrace，在栈中自顶向下遍历所有函数栈帧，并打印出每个函数栈帧的返回地址
### （2）栈帧结构
栈空间中，为每个函数都分配了独立的存储空间——堆栈帧，结构如下：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/b948b04ce793444380549e3a0626c9b0.png#pic_center)
- 栈由高地址向低地址扩展，栈顶为sp。
- 每个函数栈帧中，依此存储返回地址、前一个栈帧的帧指针、保存的寄存器和局部变量等信息。
- s0寄存器中保存着**当前栈帧**的帧指针fp：指向当前栈帧的基址
- fp-16指向的位置保存**上一个栈帧**的fp

### （3）实验思路
先根据hints，添加backtrace()的声明，再添加内联函数r_fp()，用于获取s0寄存器的值，这里省略这两步。

接下来就可以实现backtrace()的具体内容了，有几点需要实现：
- 回溯遍历栈帧：用r_fp()获取当前栈帧的fp后，让fp指向fp-16就可以跳转至上一个栈帧，如此迭代即可
- 回溯的终点：hints中给出，每个栈分配一页的存储空间，通过PGROUNDUP(fp)可以获取栈的最高地址。而回溯是从栈顶至下，也就是从低地址至高地址的，所以fp一直往高处跳，但个人不是很确定最后跳出的一跳是往哪里跳，所以就把fp夹在PGROUNDUP(fp)和PGROUNDDOWN(fp)之间
- 返回地址 `*((uint64 *)(fp - 8))`的来源： fp本质是uint64类型的指针，指向栈帧的第一个元素，表示栈帧的基址。但是从s0中取出的fp是uint64类型的，所以要转换成uint64*。最后，对指针解引用得到该地址处存储的值，也就是返回地址。pre fp也是同理。

综上，得到代码：

```c
void
backtrace(void){
  uint64 fp = r_fp();
  // 栈只占一个页面，所以可以通过取整获取栈的最低和最高地址
  uint64 bottom = PGROUNDDOWN(fp); 
  uint64 top = PGROUNDUP(fp);

  while(fp >= bottom && fp < top){
    printf("%p\n", *((uint64 *)(fp - 8)));
    fp = *((uint64 *)(fp - 16));
  }
}
```
最后记得在sys_sleep和panic中添加对backtrace()的调用，就不赘述了。


## （四）Alarm-test0
### 	（1）实验要求
- 添加一个系统调用sigalarm(interval, handler)，每隔interval的时间，中断并切换到执行handler函数
- 添加另一个系统调用sigreturn，功能test1/2再揭晓
### （2）实验步骤
**1.添加系统调用**
- makefile中添加alarmtest.c
- 用户态的系统调用声明(user.h)：

```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```
- usys.pl中添加入口
- syscall.h中添加系统调用号
- syscall.c中添加系统调用号到函数名的索引

**2. sys_sigalarm()的功能实现**
按照要求，sys_sigalarm()的作用是将系统调用的两个参数interval和handler存入PCB中。
如何实现中断并调用handler，我们是放在usertrap()中的，先不考虑。

在proc.h中新增三个变量
```c
struct proc{
	// ...
	int alarmticks;              // 时钟间隔
  	void (*handler)();           // 处理动作
  	int passedticks;             // 距离下一次alarm的时间
  	// ...
};
```
在lab2中已经知道，argint用于获取寄存器中的int型数据，argaddr获取地址型数据。系统调用的两个参数按顺序存储在a0与a1中。
p->passedticks初始化为0
```c
uint64
sys_sigalarm(void){
  int alarmticks;
  uint64 handler;
  if(argint(0, &alarmticks) < 0 || argaddr(1, &handler) < 0)
    return -1;

  struct proc *p = myproc();
  p->alarmticks = alarmticks;
  p->handler = (void (*)())handler;
  p->passedticks = 0;

  return 0;
}
```
**3.新成员的初始化与释放**
allocproc()中：
```c
// 初始化alarm 相关参数
  p->alarmticks = 0;
  p->handler = 0;
  p->passedticks = 0;
```
freeproc中：
```c
  p->alarmticks = 0;
  p->handler = 0;
  p->passedticks = 0;
```
**4.中断与handler调用**
已知CPU每隔tick的时间都会发出一个时钟中断，那么我们便可将passedticks++，等到passedticks==alarmticks时调用handler就可以达到目标了。
有关中断的代码是放在usertrap()中的，有以下几点需要注意：
- 判断时钟中断：if(which_dev == 2)
- 每次调用要将计时器passedticks重置
- hints中提到了，handler的地址有可能是0，所以我们判断是否有alarm在进行不能根据handler来，要根据alarmticks是否为0
- 如何调用handler：处理中断时我们是处于内核态的，内核态下无法调用用户函数handler。一是handler是用户空间下的虚拟地址，处于内核态的系统没有用户页表，无法寻址。二是从pagetable实验中我们可知，为了保证安全性，用户程序的pte设有PTE_U标志位，内核是无法访问的。三是就算能访问，也没有PTE_W和PTE_X标志位，只可读。隔离性决定了不能从内核态直接跳入用户程序。
所以，只能等回到用户态再执行handler，那么在此处不妨将sepc寄存器设置为handler的地址，当trap结束回到user space后，会从**sepc处的指令**继续执行用户程序。
但是又有了问题，sepc改变了，执行完handler后我们如何回到原来的用户程序继续执行呢？别急，test1/test2就是处理这个问题的

```c
void
usertrap(void)
{
  // ...
  // timer interrupt
  if(which_dev == 2){
    if(p->alarmticks != 0 && ++p->passedticks == p->alarmticks){
      p->passedticks = 0;
      // 因为页表已经切换成内核页表了，所以无法索引到handler的物理地址
      // 只能将程序计数器切到handler，等回到用户空间后再执行
      p->trapframe->epc = (uint64)p->handler;
    }
  }
  // ...
}

```

## （五）Alarm-test1/2
### （1）实验要求
test0编写的sigalarm存在哪些问题呢？
假设我们有一个用户程序test调用了sigalarm(interval, handler)，当执行到一半时，发生了时钟中断，这时test中寄存器的值会被保存到trapframe中，然后进入中断判断，发现时钟周期已满，故将trapframe中寄存器的值归位，然后返回到用户空间执行handler。
执行handler途中，寄存器中很多的值（原本属于test的）将会被新程序的值（handler）覆盖，当handler结束后返回到test后，我们已经丢失了很多寄存器的值，故test会出错。这就是问题所在。
而这个实验的要求，就是想办法回复这些寄存器的值
### （2）实验思路
所以，在调用sigalarm时，我们要将test中寄存器的值存储起来，handler结束后会调用sigreturn，届时再将寄存器中的值恢复为test程序该有的值。
有哪些寄存器的值需要存储呢？hints中回答道：it will be many
太多了数不清，那么我们不妨将整个trapframe都存储起来，在proc中添加一个trapframe的副本即可。

### （3）实验步骤
**1.添加trapfram副本**
参考https://blog.csdn.net/LostUnravel/article/details/121341055文章中PeakCrosser大佬的思路，这里有一个绝妙的方法。
- 注：PeakCrosser大佬实验中存在一些bug，是关于`p->trapframecopy`移动字节的问题，他写的是`p->trapframe + 512`，个人认为应该是`p->trapframecopy = (struct trapframe*)((char *)p->trapframe + 512)`，这样才能正确的移动512个字节，与p->trapframe位于同一页面，而非移动512*sizeof(struct trapframe)字节，后文已修改。

如果直接添加一个指向副本的trapframecopy指针，然后在新的地方开辟内存，那么我们需要在allocproc()中调用kalloc()分配新内存，在freeproc()中调用kfree()释放内存，略显麻烦。
注意到，kalloc()为trapframe分配了PGSIZE（4096B）的内存，但一个trapframe结构体实际只占288B内存，产生很大的内部碎片。由此引发思考——我们不妨用这些剩下的空间存储trapframecopy，这样做，即可节省空间，又可避免内存开辟与释放的麻烦（kfree()同样一次性释放一页内存）

```c
  struct trapframe *trapframe; // data page for trampoline.S
  struct trapframe *trapframecopy; // trapframe的副本
```
**2.复制副本trapframecopy**
每次返回调用handler前，我们都要将trapframe存储起来，故修改一下usertrap()中的代码，将trapframecopy存到trapframe的后面，它们位于同一页中，可以一起分配与销毁

```c
// timer interrupt
    if(which_dev == 2){
      if(p->alarmticks != 0 && ++p->passedticks == p->alarmticks){
        // 在修改寄存器前，存一下trapframe的副本
        // 移动到p->trapframe后的512个字节处，位于同一页面
        p->trapframecopy = (struct trapframe*)((char *)p->trapframe + 512);
        // 不要使用memcpy，string.h中可以看到memcpy被替换成了memmove
        // memmove可以避免内存冲突问题
        memmove(p->trapframecopy, p->trapframe, sizeof(struct trapframe));

        // 因为页表已经切换成内核页表了，所以无法索引到handler的物理地址
        // 只能将程序计数器切到handler，等回到用户空间后再执行
        p->trapframe->epc = (uint64)p->handler;
      }
    }
```
**3.sys_sigreturn()的实现**
sys_sigreturn()要做的事也很简单，就是恢复寄存器的值，也就是将trapframecopy拷贝回trapframe
还有两个很重要的问题
- 在handler调用sigreturn返回前，内核不能再调用handler，防止冲突，这要如何实现？
可以设置一个flag，标记是否有handler正在执行，但有一个更简单的方法。
只需要将p->passedticks = 0从原本的 usertrap() 移至 sys_sigreturn() 中即可，这样旧的handler在sys_sigreturn返回前，会一直卡在if(p->alarmticks != 0 && ++p->passedticks == p->alarmticks)的条件上而无法执行新的handler，直到返回后，计时器才清零，重新计时
- sys_sigreturn()不能直接返回0，因为返回值会存储在a0中，如果返回其它值，会覆盖test中原有的a0，所以只能返回p->trapframe->a0

```c
uint64
sys_sigreturn(){
  struct proc *p = myproc();
  // 恢复寄存器内容
  // 这里不能直接让p->trapframe = p->trapframecopy，会造成原p->trapframe的内存无法释放
  if(p->trapframecopy != (struct trapframe *)((char *)p->trapframe + 512)) {
    return -1;
  }
  memmove(p->trapframe, p->trapframecopy, sizeof(struct trapframe));

  // 返回前再重新开始计时，这样就不会冲突
  p->passedticks = 0;

  // 返回值会存储在a0中，如果返回其它值，会覆盖原有的a0，所以只能返回p->trapframe->a0
  return p->trapframe->a0;
}
```
## （六）总结
- trap的详细流程，更深入的了解了系统调用的过程
- 函数的独立存储空间——栈帧结构
- xv6中trap保存寄存器与恢复现场的方式——蹦床页面与trapframe结构
