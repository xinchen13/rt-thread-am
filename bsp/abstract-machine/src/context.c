#include <am.h>
#include <klib.h>
#include <rtthread.h>

#define ROUNDDOWN(a, sz)    ((((uintptr_t)a)) & ~((sz) - 1))    // from klib-macros.h

static Context* ev_handler(Event e, Context *c) {
    switch (e.event) {
        case EVENT_YIELD:
            c = *(Context**)(rt_thread_self()->user_data);
            break;
        default: printf("Unhandled event ID = %d\n", e.event); assert(0);
    }
    return c;
}

void __am_cte_init() {
    cte_init(ev_handler);
}

void rt_hw_context_switch_to(rt_ubase_t to) {
    rt_ubase_t user_data_tmp = rt_thread_self()->user_data;
    rt_thread_self()->user_data = to;
    yield();
    rt_thread_self()->user_data = user_data_tmp;
}

void rt_hw_context_switch(rt_ubase_t from, rt_ubase_t to) {
    rt_ubase_t user_data_tmp = rt_thread_self()->user_data;
    from = user_data_tmp;
    rt_thread_self()->user_data = to;
    yield();
    rt_thread_self()->user_data = user_data_tmp;
}

void rt_hw_context_switch_interrupt(void *context, rt_ubase_t from, rt_ubase_t to, struct rt_thread *to_thread) {
    assert(0);
}

typedef struct {
    void *tentry;
    void *parameter;
    void *texit;
} Context_wrapper;

static void wrapper (void *arg) {
    Context_wrapper *ctx = arg;
    ((void (*)(void *))(ctx->tentry))(ctx->parameter);
    ((void (*)(void))(ctx->texit))();
}

rt_uint8_t *rt_hw_stack_init(void *tentry, void *parameter, rt_uint8_t *stack_addr, void *texit) {
    rt_uint8_t *new_stack_addr = (rt_uint8_t *)ROUNDDOWN((uintptr_t)stack_addr, sizeof(uintptr_t)) - sizeof(Context_wrapper);
    
    Context_wrapper *context_wrapper = (Context_wrapper *)new_stack_addr;
    *context_wrapper = (Context_wrapper) {
        .tentry = tentry,
        .parameter = parameter,
        .texit = texit,
    };

    Area kstack;
    kstack.end = new_stack_addr;
    kstack.start = new_stack_addr - sizeof(Context);

    Context *ctx = kcontext(kstack, wrapper, context_wrapper);

    return (rt_uint8_t *)ctx;
}

/*
    在调用kcontext函数时，将void wrapper(void *arg)函数的地址传递给entry参数
    在kcontext函数内部，将new_c->mepc字段设置为wrapper函数的地址
    在创建新上下文后，当这个上下文被切换执行时，程序会从wrapper函数的地址开始执行
    wrapper函数会被调用，接收arg作为参数(entry是内核线程的入口, arg则是内核线程的参数)
    在wrapper函数内部，首先将arg转换为Context_wrapper *类型的指针ctx
    然后，wrapper函数会调用ctx->tentry所指向的函数，也就是entry传入的函数指针，将ctx->parameter作为参数传递给它
    接着，wrapper函数会执行第二个函数调用，调用ctx->texit指向的函数
*/
