# 为 AM 适配 RT-Thread
RT-Thread是一个流行的商业级嵌入式实时OS, 具备完善的OS功能模块, 并支撑各种应用程序的运行

RT-Thread中有两个抽象层, 一个是BSP(Board Support Package), 另一个是libcpu. BSP为各种型号的板卡定义了一套公共的API, 并基于这套API实现RT-Thread内核; 而对于一款板卡, 只需要实现相应的API, 就可以将RT-Thread内核运行在这款板卡上. libcpu则是为各种CPU架构定义了一套公共的API, RT-Thread内核也会调用其中的某些API. 这一思想和AM非常类似. BSP也不仅仅是针对真实的板卡, 也可以对应QEMU等模拟器, 毕竟RT-Thread内核无需关心底层是否是一个真实的板卡

## 获取并初步运行 RT-Thread
- 获取移植之后的RT-Thread (未实现上下文的创建和切换功能): `git clone git@github.com:NJU-ProjectN/rt-thread-am.git`
- 修改后的 [rt-thread-am](https://github.com/xinchen13/rt-thread-am.git) 项目: 即本仓库
- 安装项目构建工具scons: `sudo apt-get install scons`
- 在`rt-thread-am/bsp/abstract-machine/`目录下执行`make init`, 进行一些编译前的准备工作 (该部分需要 `ysyx` 项目提供的环境变量, 如 `$AM_HOME`)
- 在相同目录下通过`make ARCH=native`等方式编译或运行RT-Thread, 默认的运行输出如下(由于代码未完成, 触发了assertion): 

```
am-apps.data.size = 31076, am-apps.bss.size = 440932
heap: [0x01000000 - 0x09000000]

 \ | /
- RT -     Thread Operating System
 / | \     5.0.1 build Aug 26 2024 23:19:41
 2006 - 2022 Copyright by RT-Thread team
Assertion fail at /home/xinchen/Downloads/rt-thread-am/bsp/abstract-machine/src/context.c:29
Exit code = 01h
make: *** [/home/xinchen/ysyx/abstract-machine/scripts/native.mk:25: run] Error 1
```

## AM API
该项目去掉了原RT-Thread项目中所有BSP和libcpu, 添加了一个用AM的API来实现BSP的API的BSP, 具体见`rt-thread-am/bsp/abstract-machine/`目录下的代码. 用到的AM API如下:

- 用TRM的heap实现RT-Thread中的堆
- 用TRM的`putch()`实现RT-Thread中的串口输出功能
- 暂不使用IOE
- 用CTE的`iset()`实现RT-Thread中开/关中断功能
- 通过CTE实现RT-Thread中上下文的创建和切换功能 (下面来实现它)

## 上下文的创建
实现`rt-thread-am/bsp/abstract-machine/src/context.c`中的`rt_hw_stack_init()`函数. 
- 它的功能是以stack_addr为栈底创建一个入口为tentry, 参数为parameter的上下文, 并返回这个上下文结构的指针. 传入的stack_addr可能没有任何对齐限制, 最好将它对齐到`sizeof(uintptr_t)`再使用
- 若上下文对应的内核线程从tentry返回, 则调用texit, RT-Thread会保证代码不会从texit中返回
- CTE的`kcontext()`要求不能从入口返回, 因此需要一种新的方式来支持texit的功能. 一种方式是构造一个包裹函数, 让包裹函数来调用tentry, 并在tentry返回后调用texit, 然后将这个包裹函数作为`kcontext()`的真正入口, 不过这还要求我们将tentry, parameter和texit这三个参数传给包裹函数

全局变量造成问题的原因是它会被多个线程共享, 应该使用不会被多个线程共享的存储空间"栈". 只需要让`rt_hw_stack_init()`将包裹函数的三个参数放在上下文的栈中, 将来包裹函数执行的时候就可以从栈中取出这三个参数, 而且系统中的其他线程都不能访问它们.

最后还需要考虑参数数量的问题, `kcontext()` 要求入口函数只能接受一个类型为`void *`的参数. 不过我们可以自行约定用何种类型来解析这个参数(整数, 字符, 字符串, 指针等皆可)

## 上下文切换
实现`rt-thread-am/bsp/abstract-machine/src/context.c`中的`rt_hw_context_switch_to()`函数和`rt_hw_context_switch()`函数

- `rt_ubase_t`类型其实是`unsigned long`, `to`和`from`都是指向上下文指针变量的指针(二级指针)
- `rt_hw_context_switch_to()`用于切换到`to`指向的上下文指针变量所指向的上下文, 而`rt_hw_context_switch()`还需要额外将当前上下文的指针写入`from`指向的上下文指针变量中
- 为了进行切换, 可以通过`yield()`触发一次自陷, 在事件处理回调函数`ev_handler()`中识别出EVENT_YIELD事件后, 再处理`to`和`from`. 同样地, 需要思考如何将`to`和`from`这两个参数传给`ev_handler()`. 

根据分析, 上面两个功能的实现都需要处理一些特殊的参数传递问题. 对于上下文的切换, 以`rt_hw_context_switch()`为例, 我们需要在`rt_hw_context_switch()`中调用`yield()`, 然后在`ev_handler()`中获得`from`和`to`. `rt_hw_context_switch()`和`ev_handler()`是两个不同的函数, 但由于CTE机制的存在, 使得`rt_hw_context_switch()`不能直接调用`ev_handler()`. 因此, 一种直接的方式就是借助全局变量来传递信息

能否不使用全局变量来实现上下文的切换呢? 同样地, 需要寻找一种不会被多个线程共享的存储空间. 不过对于调用`rt_hw_context_switch()`的线程来说, 它的栈正在被使用, 往其中写入数据可能会被覆盖, 甚至可能会覆盖已有数据, 使当前线程崩溃. `to`的栈虽然当前不使用, 也不会被其他线程共享, 但需要考虑如何让`ev_handler()`访问到`to`的栈, 这又回到了我们一开始想要解决的问题.

除了栈之外, 还有没有其他不会被多个线程共享的存储空间呢? 那就是PCB, 因为每个线程对应一个PCB, 而一个线程不会被同时调度多次, 所以通过PCB来传递信息也是一个可行的方案. 要获取当前线程的PCB, 自然是用current指针了

在RT-Thread中, 可以通过调用`rt_thread_self()`返回当前线程的PCB. 阅读RT-Thread中PCB结构体的定义, 发现其中有一个成员`user_data`, 它用于存放线程的私有数据, 这意味着RT-Thread中调度相关的代码必定不会使用这个成员, 因此它很适合我们用来传递信息. 不过为了避免覆盖`user_data`中的已有数据, 我们可以先把它保存在一个临时变量中, 在下次切换回当前线程并从`rt_hw_context_switch()`返回之前再恢复它. 这个临时变量使用局部变量: 局部变量是在栈上分配的

## 其他
在`rt-thread-am/bsp/abstract-machine/src/context.c`中还有一个`rt_hw_context_switch_interrupt()`函数, 目前RT-Thread的运行过程不会调用它, 因此目前可以忽略

## 在 nemu 中运行 RT-Thread
实现后, 尝试在NEMU中运行RT-Thread. 由于在移植后的RT-Thread中内置了一些Shell命令, 能看到RT-Thread启动后依次执行这些命令, 最后输出命令提示符`msh />`:

```
Welcome to riscv32-NEMU!
For help, type "help"
am-apps.data.size = ld, am-apps.bss.size = ld
heap: [0x802da000 - 0x88000000]

 \ | /
- RT -     Thread Operating System
 / | \     5.0.1 build Aug 28 2024 10:40:01
 2006 - 2022 Copyright by RT-Thread team
[I/utest] utest is initialize success.
[I/utest] total utest testcase num: (0)
Hello RISC-V!
msh />help
RT-Thread shell commands:
date             - get date and time or set (local timezone) [year month day hour min sec]
list             - list objects
version          - show RT-Thread version information
clear            - clear the terminal screen
free             - Show the memory usage in the system.
ps               - List threads in the system.
help             - RT-Thread shell help.
tail             - print the last N - lines data of the given file
echo             - echo string to file
df               - disk free
umount           - Unmount the mountpoint
mount            - mount <device> <mountpoint> <fstype>
mkfs             - format disk with file system
mkdir            - Create the DIRECTORY.
pwd              - Print the name of the current working directory.
cd               - Change the shell working directory.
rm               - Remove(unlink) the FILE(s).
cat              - Concatenate FILE(s)
mv               - Rename SOURCE to DEST.
cp               - Copy SOURCE to DEST.
ls               - List information about the FILEs.
utest_run        - utest_run [-thread or -help] [testcase name] [loop num]
utest_list       - output all utest testcase
memtrace         - dump memory trace information
memcheck         - check memory data
am_fceux_am      - AM fceux_am
am_snake         - AM snake
am_typing_game   - AM typing_game
am_microbench    - AM microbench
am_hello         - AM hello

msh />date
[W/time] Cannot find a RTC device!
local time: Thu Jan  1 08:00:00 1970
timestamps: 0
timezone: UTC+8
msh />version

 \ | /
- RT -     Thread Operating System
 / | \     5.0.1 build Aug 28 2024 10:40:01
 2006 - 2022 Copyright by RT-Thread team
msh />free
total    : 131227544
used     : 33583576
maximum  : 33583576
available: 97643968
msh />ps
thread                   pri  status      sp     stack size max used left tick  error
------------------------ ---  ------- ---------- ----------  ------  ---------- ---
tshell                    20  running 0x000000a0 0x00001000    21%   0x0000000a OK
sys workq                 23  ready   0x000000a0 0x00002000    01%   0x0000000a OK
tidle0                    31  ready   0x000000a0 0x00004000    00%   0x00000020 OK
timer                      4  suspend 0x000000a0 0x00004000    01%   0x0000000a OK
main                      10  close   0x000000a0 0x00000800    16%   0x00000014 OK
msh />pwd
/
msh />ls
No such directory
msh />memtrace

memory heap address:
name    : heap
total   : 0x131227544
used    : 0x33583576
max_used: 0x33583608
heap_ptr: 0x802da048
lfree   : 0x822e1220
heap_end: 0x87fffff0

--memory item information --
[0x802da048 -   32M] NONE
[0x822da058 -   12K] NONE
[0x822dd370 -   176] NONE
[0x822dd430 -    2K] NONE
[0x822ddc40 -    16] main
[0x822ddc60 -    72] main
[0x822ddcb8 -   176] main
[0x822ddd78 -    8K] main
[0x822dfd88 -   952] main
[0x822e0150 -   176] main
[0x822e0210 -    4K] main
[0x822e1220 -   93M]     
msh />memcheck
msh />utest_list
[I/utest] Commands list : 
msh />
```

由于NEMU目前还不支持通过串口进行交互, 因此我们无法将终端上的输入传送到RT-Thread, 所有内置命令运行结束后, RT-Thread将进入空闲状态, 此时退出NEMU即可

## 在 RT-Thread 上运行 AM 程序
`make init`后`integrate-am-apps.py`中的`app_dir_list`数组中列出的AM程序编译到RT-Thread, 可以在内置的Shell命令中(位于`uart.c`)添加用于启动这些AM程序的命令(见help)

运行后会产生非法访存, 暂未修复()