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