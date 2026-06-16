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
// inthandler27 — PIC1（从片）伪中断处理（IRQ7 → IDT 0x27）
// 当 PIC1 上有中断发生时，如果 PIC0 还没有从 IRQ2 接收完中断信号，
// PIC0 会自己产生一个 IRQ7 来补偿。这个中断可以忽略。
// 参数: esp = 中断发生时的栈指针
// ============================================================
void inthandler27(int *esp)
{
    // 在屏幕左上角显示提示信息，表示伪中断已收到
    struct BOOTINFO *binfo = (struct BOOTINFO *)ADR_BOOTINFO;
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, "INT 27 (IRQ-7) : PIC1 spurious");
    for (;;)
    {
        io_hlt();
    }
}

// ============================================================
// inthandler2c — 鼠标中断（IRQ12 → IDT 0x2C）处理函数
// 由 _asm_inthandler2c（naskfunc.nas）在保存现场后调用
// 参数: esp = 中断发生时的栈指针
// ============================================================
void inthandler2c(int *esp)
{
    struct BOOTINFO *binfo = (struct BOOTINFO *)ADR_BOOTINFO;
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, "INT 2C (IRQ-12) : PS/2 mouse");
    for (;;)
    {
        io_hlt();
    }
}