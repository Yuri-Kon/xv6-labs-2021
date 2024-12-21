# Lab 3: Page tables

## 3.1 Speed up system calls

- 提议: 给系统调用函数`ugetpid()`提速. 方法是给每个进程的虚拟内存空间里添加一个`USYSCALL` page, 其中放置一个系统函数会经常使用的数据, 这里放的是`pid`

  - 这样, 当`ugetpid()`需要调用`pid`时,就会**直接访问进程的`USYSCALL`页**, 而**无需切换**到内核态  
- 如何添加`USYSCALL`页面?  
  - 完成页面映射, 可以认为`USYSCALL`页面就在`trampoline`和`trap frame`下面, 而`proc_pagetable()`是用来创建用户页表的, 所以在它们下面新增对`USYSCALL`页的创建

    ```C
    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if(mappages(pagetable, TRAMPOLINE, PGSIZE,
                (uint64)trampoline, PTE_R | PTE_X) < 0){
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe just below TRAMPOLINE, for trampoline.S.
    if(mappages(pagetable, TRAPFRAME, PGSIZE,
                (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }
    // map the USYSCALL page just blew TRAMPLINE and TRAPRAME
    // user can only read it
    if (mappages(pagetable, USYSCALL, PGSIZE, 
                    (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }
    ```

  - 分配内存空间  
    需要在结构体`struct proc`中添加一个`struct usyscall *usyscall`变量以指向这个新页

    ```C
    // Per-process state
    struct proc {
    struct spinlock lock;
        ...
    struct usyscall *usyscall;   // record info of syscall(pid) 
    };
    ```

    同时需要在`allocproc()`中为USYSCALL页面分配物理内存:  

    ```C
    // Allocate a trapframe page.
    if((p->trapframe = (struct trapframe *)kalloc()) == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    // 申请一个空闲物理页表,存储usyscall变量
    if ((p->usyscall = (struct usyscall*)kalloc()) == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    ```

    至此,结束对`USYSCALL`的申请和分配
  - 如果分配失败, 会调用`freeproc()`, 这里需要比之前多释放一个p->usyscall参数, 同时还会调用`proc_freepagetabel()`, 解除映射关系.

    ```C
    //in freeproc()
    if(p->trapframe)
    kfree((void*)p->trapframe);
    p->trapframe = 0;
    p->usyscall = 0;
    
    //in proc_freepagetable
    void
    proc_freepagetable(pagetable_t pagetable, uint64 sz)
    {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    //释放USYSCALL的映射关系
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmfree(pagetable, sz);
    }
    ```

- 总结:  

>这一项的关键是理解xv6创建页表(进行页面映射--分配物理内存)的过程. 这里之所以思路上先考虑映射的建立, 是因为操作系统可能不必立即为这些页表分配内存(*eager allocate*), 而是在需要被使用时才考虑分配

## 3.2 Print a page table

- 打印页表的详细信息
- 考察的主要是对页表的遍历
- 主要参考`freewalk()`函数
  - `freewalk()`函数

    ```C
    // Recursively free page-table pages.
    // All leaf mappings must already have been removed.
    void
    freewalk(pagetable_t pagetable)
    {
      // there are 2^9 = 512 PTEs in a page table.
      for(int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];
        if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
          // this PTE points to a lower-level page table.
          uint64 child = PTE2PA(pte);
          freewalk((pagetable_t)child);
          pagetable[i] = 0;
        } else if(pte & PTE_V){
          panic("freewalk: leaf");
        }
      }
      kfree((void*)pagetable);
    }
    ```

    **函数逻辑**  
    1. `freewalk()`函数的作用是递归地释放页表中的页表项  
      页表有512个页表项, 每个页表项指向8字节(64位)的数据

    2. 检查页表项是否有效和是否指向下级页表:  

        ```C
        pte_t pte = pagetable[i];
        if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)
        ```

        其中, `pte &PTE_V`检查页表项是否有效, `PTE_R | PTE_W | PTE_X`检查页表项是否是叶结点. 如果这三项全都为0, 则表明这个页表项指向了一个下级页表, 因此需要递归释放.

    3. 递归释放子页表

        ```C
        uint64 child = PTE2PA(pte);
        freewalk((pagetable_t)child);
        pagetable[i] = 0;
        ```

  - `vmprint()`函数  
  仍然需要全部遍历所有页表, 对于子页表的判断条件与`freewalk()`一致, 只是在具体处理上使用了打印语句.

    ```C
    // 打印页表，递归打印每一层页表
    void vmprint(pagetable_t pagetable, int level, uint64 index)
    {
      // 通过递归遍历页表的每一层，每一层有 512 项页表项（PTEs）
      for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];

        // 如果页表项无效，则跳过
        if ((pte & PTE_V) == 0)
          continue;

        // 打印当前页表项的详细信息
        uint64 pa = PTE2PA(pte);  // 获取物理地址

        // 打印当前层的页表项及其物理地址
        for (int j = 0; j < level; j++) {
          cprintf(" ..");  // 打印缩进
        }
        cprintf("%d: pte 0x%016llx pa 0x%016llx\n", i, pte, pa);

        // 如果页表项指向下级页表，递归调用
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
          // 获取下一级页表的物理地址并递归打印
          uint64 child = PTE2PA(pte);
          vmprint((pagetable_t)child, level + 1, i);
        }
      }
    }

    // 使用此函数打印根页表的信息
    void vmprint_root(pagetable_t pagetable)
    {
      cprintf("page table 0x%016llx\n", (uint64)pagetable);
      vmprint(pagetable, 0, 0);
    }
    ```

  **解释**:  
  1. `vmprint()`函数中, 使用`PTE2PA(pte)`提取物理地址, 并打印该页表项
  2. `vmprint_root()`函数是`vmprint()`的入口函数,打印根页表

## 3.3 Detecting which pages have been accessed
