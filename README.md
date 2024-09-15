
## （一）前置知识：文件系统
### （1）文件系统的七层
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/15e02f3ea5ff43f6b5f471974b42e33e.png#pic_center)
- **Disk Layer**： 实现硬件操作，即对磁盘块的读写；
- **Buffer Cache Layer**： 实现磁盘-内存的缓存，并管理进程对缓存块的并发访问；
- **Logging Layer**： 提供更新磁盘接口，并确保更新的原子性，同时实现Crash Recovery；
- **Inode Layer**： 为文件层提供接口，保证文件的独立并确定inode标识和相关信息；
- **Directory Layer**： 实现目录文件，其数据是一系列的目录条目，包含该目录下的文件名和文件的inode号；
- **Pathname Layer**： 提供符合文件系统层次结构的路径名，同时实现路径名称递归查找；
- **File Descriptor Layer**： 实现对底层资源（管道/设备/文件）的抽象；

### （2）Inode层
**1.索引节点inode**：
磁盘/内存中的数据结构, 磁盘: 文件大小, 数据块位置等; 内存: 内核信息 + 磁盘;
- 磁盘inode: dinode: 大小固定, 包含文件类型、引用链接计数(硬连接数)、文件大小、数据位置
- 内存inode: 内存中dinode的副本。 包含设备 、inode数 、 程序引用数 、 访问锁 、 读取标志位 + dinode结构

**2.接口**：
1. `ialloc`: 创建inode。遍历inode磁盘块找到空闲 -> 更新type字段 -> icache缓存 -> 利用iget返回inode指针 (同步机制依赖于Buffer Cache层)
2. `iget`: 通过设备号和inode号查找icache中indoe副本, 如没有,调用空槽位/回收槽位创建缓存块。最后返回inode指针
3. `iput`: 释放数据块inode,如果引用 + 硬连接==0 -> 则调用itrunc释放磁盘数据块 -> 使用iupdate更新磁盘数据
4. `iunlockput`：为inode解锁并释放数据块，是iunlock与ilock的结合
5.`iupdate`：将inode的更新写回磁盘

### （3）Inode Content
**1.内容**：
inode中的addrs数组：12个直接地址+一个间接地址（混合索引分配）。间接地址中存储另一个块的块号，该块中包含256个数据块的地址
因此，最大文件大小为：12+256=268KB
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/aab9e386105f460c841d76825a7c2736.png#pic_center)
**2.接口**：
1.`bmap`：根据inode指针与数据块块号，在直接块与间接块中查找，返回数据块地址（数据块号），若不存在则分配
2.`itrunc`: 释放给定inode的直接/间接块, 并将其大小和地址截断为0
3.上层接口`readi`,`writei`：从inode读取数据/向inode写入数据

### （4）软连接与硬连接
**1.软连接（Soft Link）:**
- 也称为符号链接（Symbolic Link）。
- 软连接是一个特殊类型（link类型）的文件，它包含指向另一个文件的路径名。
- 它类似于 Windows 中的快捷方式，指向目标文件的路径。
- 软连接可以跨越不同的文件系统与磁盘设备，因为它只是一个路径引用。
- 如果原始文件被删除，软连接会变成“悬空链接”（dangling link），指向的目标不再存在。
- 优点:
     - 可以跨文件系统/磁盘设备创建。
	- 可以指向目录。
- 缺点:
	- 如果目标文件被删除或移动，软连接会失效。

**2.硬连接（Hard Link）:**
- 硬连接是文件系统中多个目录项（文件名）指向同一个 inode 的引用。
- 它们是完全等价的文件，每个硬连接都直接引用相同的数据块和 inode。
- 硬连接不能跨文件系统创建，也不能指向目录（除非是超级用户）。
- 删除其中一个硬连接不会影响其他硬连接或文件数据。只有当所有硬连接都被删除时，文件数据才会被释放。nlink记录硬连接数
- 优点:
  -  提供文件的备份，因为删除一个硬链接不会删除文件数据。
  - 更节省空间，因为硬链接与原始文件共享相同的数据块。
- 缺点:
	- 不能跨越文件系统创建。
	- 不能指向目录（除非是特殊的情况）。

## （二）Large files
### （1）实验要求
Unix系统支持三级间址，inode结构如下图：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/f79c9d7bb8754430a5a0527f1959f7b5.png#pic_center)
其有10个直接块，分别有一个一二三级间接块，一块数据块的大小为4KB，能存储1KB的块号，故支持的最大文件大小为：
10 * 4KB+1KB * 4KB + 1KB * 1KB * 4KB + 1KB * 1KB * 1KB * 4KB

而xv6中，仅支持一级间址，其有12个直接块，一个一级间接块，一块数据块的大小为1KB，能存储256的块号，故支持的最大文件大小为：
12 * 1KB + 256 * 1KB

本实验要求为xv6添加二级间址，使支持的最大文件大小上升为：
11 * 1KB + 256 * 1KB + 256 * 256 *1KB

### （2）实验步骤
**1.修改相关宏与Inode结构体**
让出一个直接块作为二级间址块，故`NDIRECT`减为11
增添表示二级间址块数的宏`NDOUBLEINDIRECT`
修改文件最大大小的宏
fs.h中：

```c
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDOUBLEINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLEINDIRECT)
```
同时修改inode与dinode结构体中的addrs数组：

```c
struct inode {
  // ...
  uint addrs[NDIRECT+1+1];
};
```
```c
struct dinode {
  // ...
  uint addrs[NDIRECT+1+1];   // Data block addresses
};
```
**2.修改`bmap`**
`bmap`根据inode指针与逻辑块号（addrs数组中的偏移量），返回所指向的**磁盘块号**
函数中使用了uint类型的addr，它所指的就是磁盘块的块号。

增加二级索引的方式很简单，`bmap`已经给出了一级间址的架构，仿照着索引两次即可。

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load single-indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // 二级间址
  if(bn < NDOUBLEINDIRECT){
    // Load double-indirect block, allocating if necessary.

    // 一级索引
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev); 
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if ((addr = a[bn / NINDIRECT]) == 0){
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    // 二级索引
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn % NINDIRECT]) == 0){
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    return addr;
  }
```
**注：** 每次使用`bread`读取缓存区时，要记得把之前的缓存区释放掉，即`brelse(bp)`

**3.修改`itrunc`**
该函数用于释放一个inode所指向所有数据块，此处也要释放掉二级间址块。
同样，在理解一级间址的块的释放逻辑后，很容易得到二级间址块释放的代码，只需两重循环，先释放二级块，再释放一级块即可。

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp, *double_bp;
  uint *a, *b;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // 释放二级间址块
  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    for (i = 0; i < NINDIRECT; ++i){
      if(a[i]){
        double_bp = bread(ip->dev, a[i]);
        b = (uint*)double_bp->data;
        for (j = 0; j < NINDIRECT; ++j){
          if (b[j])
            bfree(ip->dev, b[j]);
        }
        brelse(double_bp);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```
## （三）Symbolic links
### （1）实验要求
为xv6添加软连接。
即添加`symlink(target, path)`系统调用，在path路径下添加一个“symlink”类型的文件，文件内容为target（target是目标文件的路径）。

### （2）实验步骤
**1.添加系统调用相关声明**
在kernel/syscall.h, kernel/syscall.c, user/usys.pl 和 user/user.h 中添加symlink系统调用的声明，在makefile中将symlinktest加入编译。
可以回顾lab2，此处不再赘述。

**2.添加相关宏**
stat.h中：

```c
#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   // symbolic link
```
fcntl.h中：

```c
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW  0x004
```
`O_NOFOLLOW`的具体值没有要求，不与其它宏冲突即可，这里选了0x004

**3.实现系统调用`sys_symlink`**
如果没有思路，可以先参阅其它系统调用如sys_link，对接口有一定了解后，思路就明晰了。
该函数流程如下：
1. 调用`create`，在path上**创建**一个新的inode，文件类型为
2. 调用`writei`，将target写入inode的数据块中

注意：
- 文件操作要写在事务里，用`begin_op()`与`end_op()`标识，出现错误提前返回时，也要记得加上`end_op()`
- `create`创建并返回一个**上锁**的inode，并**增加硬连接数**。所以操作时不需要加锁，但释放时记得解锁
- 注意inode中引用数与硬连接数的区别，每引用一个inode就会增加ref，`iput`是减少ref。

```c
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;
  int n;

  if ((n = argstr(0, target, MAXPATH)) < 0 || argstr(1, path, MAXPATH) < 0)
  {
    return -1;
  }

  begin_op();

  // 创建软连接类型的inode
  if ((ip = create(path, T_SYMLINK, 0, 0)) == 0)
  {
    end_op();
    return -1;
  }

  // 将target写入inode
  if (writei(ip, 0, (uint64)target, 0, n) != n)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlockput(ip);

  end_op();
  return 0;
}
```
**4.修改`sys_open`**
修改`sys_open`调用，以便能够打开软连接类型的文件
要点如下：
- 由于存在软连接指向软连接的套娃行为，所以此处肯定存在一个循环，例如`while(ip->type == T_SYMLINK)`，直到打开的文件不再是软连接为止
- 软连接可能成环（称为循环引用）。处理方案有三，用哈希表记录走过的路径，或者进行深度计数，或二者结合。Unix中使用的是深度计数，hints中也指明了这种方法，所以我们维护一个depth变量，记录软连接迭代的深度，超过10返回错误即可。
- 当含有O_NOFOLLOW标志位时，不进行软连接索引，而是直接返回软连接原文
- 主要函数有两个，`readi`用于读取inode中的软连接，`namei`用于获去path上文件的inode

```c
uint64
sys_open(void)
{
  // ...
	else
	  {
	    if ((ip = namei(path)) == 0)
	    {
	      end_op();
	      return -1;
	    }
	    ilock(ip);
	
	    // 软连接类型，且不含O_NOFOLLOW标志位，则迭代追踪目标路径
	    while (ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0)
	    {
	      if (++depth > 10)
	      { // 迭代上限
	        iunlockput(ip);
	        end_op();
	        return -1;
	      }
	      if (readi(ip, 0, (uint64)target, 0, MAXPATH) < 0)
	      {
	        iunlockput(ip);
	        end_op();
	        return -1;
	      }
	      
	      iunlockput(ip);
	      if ((ip = namei(target)) == 0)
	      {
	        // 此处不能iput(ip)
	        end_op();
	        return -1;
	      }
	      ilock(ip);
	    }
	
	    if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV))
	    {
	      iunlockput(ip);
	      end_op();
	      return -1;
	    }
	
	    if (ip->type == T_DIR && omode != O_RDONLY)
	    {
	      iunlockput(ip);
	      end_op();
	      return -1;
	    }
	  }
  // ...
}
```
注意索引软连接文件的代码应该放在检测文件类型检测代码的前面，因为通过软连接打开最终文件后，还要进行文件类型的检测。


## （四）总结
- 本实验虽然名为File system，但在文件系统的七个层次中，主要涉及的还是inode层往上的层次，层次较高。上个实验涉及了Buffer cache层。但中间重要的一层logging层并没有过多涉及
- 本实验较为简单，手撕代码的时间可能总共不超过一小时，但debug的时间嘛……一整天（
