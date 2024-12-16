# Speed up system calls

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
