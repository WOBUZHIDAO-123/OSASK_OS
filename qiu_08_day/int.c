#include "bootpack.h" // 包含全局定义和函数声明

#define PORT_KEYDAT 0x0060 /* 键盘数据端口地址 */

/*PIC的初始化*/
void init_pic(void)
{
    // pic0是主片，pic1是从片，主片连接从片的IRQ2，所以主片的ICW3设置为0x04（0000 0100），
    // 表示从片连接在IRQ2上；从片的ICW3设置为0x02（0000 0010），表示它连接在主片的IRQ2上。
    io_out8(PIC0_IMR, 0xff); /*禁止所有中断*/
    io_out8(PIC1_IMR, 0xff); /*禁止所有中断*/

    io_out8(PIC0_ICW1, 0x11);   /*边缘触发模式*/
    io_out8(PIC0_ICW2, 0x20);   /*IRQ0-7由INT20-27接收*/
    io_out8(PIC0_ICW3, 1 << 2); /*PIC1由IRQ2连接*/
    io_out8(PIC0_ICW4, 0x01);   /*无缓冲模式*/

    io_out8(PIC1_ICW1, 0x11); /*边缘触发模式*/
    io_out8(PIC1_ICW2, 0x28); /*IRQ8-15由INT28-2f接收*/
    io_out8(PIC1_ICW3, 2);    /*PIC1由IRQ2连接*/
    io_out8(PIC1_ICW4, 0x01); /*无缓冲模式*/

    io_out8(PIC0_IMR, 0xfb); /*11111011 除了PIC1以外全部禁止*/
    io_out8(PIC1_IMR, 0xff); /*11111111 禁止所有中断*/

    return;
}

// 键盘缓冲区全局变量
// 中断处理函数 (inthandler21) 在中断发生时将扫描码写入此缓冲区
// 主循环 (HariMain) 轮询此缓冲区，取出数据显示到屏幕
// flag=0: 缓冲区空，flag=1: 缓冲区有数据尚未被读取
struct FIFO8 keyfifo;

// ============================================================
// inthandler21 — 键盘中断（IRQ1 → IDT 0x21）处理函数
// 由 _asm_inthandler21（naskfunc.nas）在保存现场后调用
//
// 职责：
//   ① 告知 PIC 中断处理完毕（发送 EOI），允许后续中断到达
//   ② 从键盘控制器数据端口 (0x0060) 读取扫描码
//   ③ 将扫描码存入 keybuf 缓冲区（若缓冲区为空）
//
// 参数: esp = 中断发生时的栈指针（指向被中断程序的 EFLAGS/CS/EIP）
// ============================================================
void inthandler21(int *esp)
{
    unsigned char data;
    // 发送 EOI（End Of Interrupt）到主 PIC
    // OCW2 = 0x0020, 写入 0x61 = 0x60 + IRQ1，通知 PIC IRQ1 处理完毕
    io_out8(PIC0_OCW2, 0x61);

    // 从键盘控制器的数据端口（0x0060）读取按下的键对应的扫描码
    // 键盘每次按键/松键都会产生一个扫描码字节
    data = io_in8(PORT_KEYDAT);
    fifo8_put(&keyfifo, data); // 将扫描码存入 keybuf 缓冲区（若缓冲区为空）

    return;
}

// ============================================================
// inthandler27 — PIC0 伪中断处理（IRQ7 → IDT 0x27）
//
// 当 PIC1 上有中断发生时，如果 PIC0 还没有从 IRQ2 接收完中断信号，
// PIC0 会自己产生一个 IRQ7 来补偿（伪中断 / spurious interrupt）。
// 此中断在 PIC 初始化时几乎必定发生一次（芯片组电气噪声），
// 只需发送 EOI 告知 PIC 处理完毕即可，不做任何实质性操作。
// 为什么不需要处理？因为这个中断是 PIC 初始化时的电气噪声产生的，
// 并非真正的外设中断请求，无需响应任何设备。
//
// 参数: esp = 中断发生时的栈指针
// ============================================================
void inthandler27(int *esp)
{
    // 发送 EOI 到主 PIC（0x67 = 0x60 + IRQ7），通知 IRQ7 处理完毕
    io_out8(PIC0_OCW2, 0x67);
    return;
}

// 鼠标数据 FIFO 环形缓冲区全局变量
//   中断处理函数 inthandler2c 将鼠标数据包写入此缓冲区，
//   主循环 (HariMain) 轮询读取，用于更新鼠标指针位置。
struct FIFO8 mousefifo;

// ============================================================
// inthandler2c — 鼠标中断（IRQ12 → IDT 0x2C）处理函数
// 由 _asm_inthandler2c（naskfunc.nas）在保存现场后调用
//
// 职责：
//   ① 通知从 PIC（PIC1）IRQ12 处理完毕（EOI）
//   ② 通知主 PIC（PIC0）IRQ2 处理完毕（PIC1 通过 IRQ2 级联）
//   (因为控制鼠标的 PIC1 连接在主 PIC 的 IRQ2 上，信号传导 cpu 需经过两片 PIC，所以需要同时通知两个 PIC 中断处理完毕)
//   ③ 从键盘控制器数据端口（0x0060）读取鼠标发来的数据包
//   ④ 将数据存入 mousefifo 环形缓冲区，供主循环轮询处理
//
// 鼠标数据格式（3 字节数据包，每次鼠标移动/按键触发）：
//   字节 0: bit0~2=按键状态, bit4~7=位移方向
//   字节 1: X 轴位移量（带符号）
//   字节 2: Y 轴位移量（带符号）
//
// 参数: esp = 中断发生时的栈指针（指向被中断程序的 EFLAGS/CS/EIP）
// ============================================================
void inthandler2c(int *esp)
{
    unsigned char data;

    // 发送 EOI（End Of Interrupt）到从片 PIC1
    // OCW2 = 0x00A0, 写入 0x64 = 0x60 + IRQ12，通知 PIC1 IRQ12 处理完毕
    io_out8(PIC1_OCW2, 0x64);

    // 发送 EOI 到主片 PIC0（因为 PIC1 通过 IRQ2 级联到 PIC0）
    // OCW2 = 0x0020, 写入 0x62 = 0x60 + IRQ2，通知 PIC0 IRQ2 处理完毕
    io_out8(PIC0_OCW2, 0x62);

    // 鼠标启用后，每次移动/按键都会向此端口发送一个数据字节
    data = io_in8(PORT_KEYDAT);

    fifo8_put(&mousefifo, data);

    return;
}
