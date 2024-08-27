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

## 其他
在`rt-thread-am/bsp/abstract-machine/src/context.c`中还有一个`rt_hw_context_switch_interrupt()`函数, 目前RT-Thread的运行过程不会调用它, 因此目前可以忽略
