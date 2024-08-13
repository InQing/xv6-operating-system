struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint timestamp; // 时间戳
  struct buf *next; // 哈希桶的链表指针
  uchar data[BSIZE];
};

