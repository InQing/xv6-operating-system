## （一）xv6网络协议栈
xv6网络协议栈流程图
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/7c905227891e44a286cd7a67b29c1496.png#pic_center)
## （二）networking
### （1）e1000_transmit
hints中已经把步骤清晰地罗列出来了，照着写即可。
有以下几点需要额外注意：
- 发送数据帧的过程可能存在并发关系，对于描述符环和缓存区这两个临界资源，需要加锁。
- 描述符必要的cmd标志位有两个，E1000_TXD_CMD_EOP 和 E1000_TXD_CMD_RS，EOP表示数据包的结束，RS表示status字段有效，故二者都需要被设置。
```c
int e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  uint32 rear;

  acquire(&e1000_lock);
  // 1.获取下一个需要发送的数据包在环中的索引
  rear = regs[E1000_TDT];
  // 2.检查数据块是否带有E1000_TXD_STAT_DD标志，若无则数据还未完成转发
  if ((tx_ring[rear].status & E1000_TXD_STAT_DD) == 0)
  {
    release(&e1000_lock);
    return -1;
  }
  // 3.释放已转发的数据块
  if (tx_mbufs[rear])
  {
    mbuffree(tx_mbufs[rear]);
  }
  // 4.设置描述符与缓存区字段
  tx_ring[rear].addr = (uint64)m->head;
  tx_ring[rear].length = m->len;
  tx_ring[rear].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  tx_mbufs[rear] = m;
  // 5.修改环尾索引
  regs[E1000_TDT] = (rear + 1) % TX_RING_SIZE;

  release(&e1000_lock);

  return 0;
}
```
### （2）e1000_recv
同样地根据hints逐步进行，有以下几点需要注意：
- 接收数据帧的e1000_recv是由中断驱动的，处理完才会返回，因此不存在并发关系，不需要加锁
- 由hints提示可知，此处尾指针指向的是已被软件处理的数据帧, 其下一个才为当前需要处理的数据帧，因此索引需要加1
- 为了解决接收队列的缓存区溢出，超出上限的问题，采用了循环读取，每次尽量将队列读空。
```c
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // 1.获取下一个需要接收的数据包在环中的索引
  uint32 rear = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  // 2.检查E1000_TXD_STAT_DD标志
  while ((rx_ring[rear].status & E1000_TXD_STAT_DD))
  {
    if (rx_ring[rear].length > MBUF_SIZE)
    {
      panic("E1000 length overflow");
    }
    // 3.更新缓冲块信息，递交数据包给网络栈解封装
    rx_mbufs[rear]->len = rx_ring[rear].length;
    net_rx(rx_mbufs[rear]);
    // 4.分配新的缓存区，更新描述符
    rx_mbufs[rear] = mbufalloc(0);
    rx_ring[rear].addr = (uint64)rx_mbufs[rear]->head;
    rx_ring[rear].status = 0;

    rear = (rear + 1) % RX_RING_SIZE;
  }
  // 5.修改环尾索引
  // 此处由于while循环末端让rear = rear - 1了，所以尾指针索引需要减1
  // 若没有该循环，则此处不需要修改rear，因为尾指针指向的是已被软件处理的数据帧
  regs[E1000_RDT] = (rear - 1) % RX_RING_SIZE;
}
```

## （三）完结感想
本章博客写得非常水，主要是因为恰巧开学了（
但无论如何还是要发出来，为MIT6.S081这个系列收个尾。
也在此处，对整个系列做一个总结吧。

个人完成所有lab+xv6 book阅读+课程视频，耗时在两个半月左右。
对我来说最费心思的点莫过于xv6 book的阅读，个人的英语阅读水平不算太差，至少也裸考过了六级，但也许是第一次读相关专业英文教材的原因，阅读的过程若不借助翻译AI，会非常的吃力，直到整本书读完，也依然有这种感觉。每个单词都认识，每个语法结构都在高中学得滚瓜烂熟，但组合在一起总让人一脸懵逼。

而完成lab的过程，确实能感觉到自己的水平在一步步提升。从lab1的不知所云，lab2的手足无措，到遇上最难的lab3——pagetable，几乎每一步都是重重阻碍，要结合多家大佬的博客才能完全理解，抄一半写一半的完成代码。但到了后期，熟悉xv6各个部分的源码后，逐渐变得得心应手，即便是hard实验，也能独立地完成绝大部分（虽然总有一些意想不到的点需要参考大佬们的思路）

观看课程视频时，总能被mit大佬们的思路惊艳，确实能从中感觉到那种鸿沟，但是并无关系，走好自己的路，与自己比较即可。
个人理解知识点的顺序是视频->书籍，所以观看视频时，大多都抱着不求甚解的态度，等到阅读书籍时再结合源码理解，有时甚至还要回去看看当时学操作系统的王道教材。

略有遗憾的是，自己并没有在整个流程中，提升多少debug的水平。gdb用得含含糊糊，指令没懂多少，基本只会敲一个b panic和where。所以到头来，还是printf用得多（永远的好朋友）

至此，两个半月的OS学习也算圆满结束了，很高兴能与大家一起进步。
接下来或许不再打算做MIT相关的lab，个人没有读研的打算，升入大三，面临即将而来的秋招，确实再没有这个精力。
所以接下来的关注点，会聚焦在其他项目和面试上啦
目前选的是C++这条路线，还没有规划好具体方向，但无论如何，计算机基础打好是校招最重要的。

很荣幸我的博客能为大家提供一小点帮助，MIT6.S081系列至此结束，各位，我们有缘再见！


