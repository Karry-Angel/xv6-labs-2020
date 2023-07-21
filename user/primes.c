// Lab Xv6 and Unix utilities
// primes.c

#include "kernel/types.h"
#include "user/user.h"
#include "stddef.h"

// 文件描述符重定向(讲成映射比较好)
void
mapping(int n, int pd[])
{
  // 关闭文件描述符 n
  close(n);
  // 将管道的 读或写 端口复制到描述符 n 上
  // 即产生一个 n 到 pd[n] 的映射
  dup(pd[n]);
  // 关闭管道中的描述符
  close(pd[0]);
  close(pd[1]);
}

// 求素数
void
primes()
{
  // 定义变量获取管道中的数
  int previous, next;
  // 定义管道描述符数组
  int fd[2];
  // 从管道读取数据
  if (read(0, &previous, sizeof(int)))
  {
    // 第一个一定是素数，直接打印
    printf("prime %d\n", previous);
    // 创建管道
    pipe(fd);
    // 创建子进程
    if (fork() == 0)
    {
      // 子进程
      // 子进程将管道的写端口映射到描述符 1 上
      mapping(1, fd);
      // 循环读取管道中的数据
      while (read(0, &next, sizeof(int)))
      {
        // 如果该数不是管道中第一个数的倍数
        if (next % previous != 0)
        {
          // 写入管道
          write(1, &next, sizeof(int));
        }
      }
    }
    else
    {
      // 父进程
      // 等待子进程把数据全部写入管道
      wait(NULL);
      // 父进程将管道的读端口映射到描述符 0 上
      mapping(0, fd);
      // 递归执行此过程
      primes();
    }  
  }  
}

int 
main(int argc, char *argv[])
{
  // 定义描述符
  int fd[2];
  // 创建管道
  pipe(fd);
  // 创建进程
  if (fork() == 0)
  {
    // 子进程
    // 子进程将管道的写端口映射到描述符 1 上
    mapping(1, fd);
    // 循环获取 2 至 35
    for (int i = 2; i < 36; i++)
    {
      // 将其写入管道
      write(1, &i, sizeof(int));
    }
  }
  else
  {
    // 父进程
    // 等待子进程把数据全部写入管道
    wait(NULL);
    // 父进程将管道的读端口映射到描述符 0 上
    mapping(0, fd);
    // 调用 primes() 函数求素数
    primes();
  }
  // 正常退出
  exit(0);
}

