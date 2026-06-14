#include "bootpack.h"    // 包含全局定义和函数声明

/*PIC的初始化*/
void init_pic(void)
{
    //pic0是主片，pic1是从片，主片连接从片的IRQ2，所以主片的ICW3设置为0x04（0000 0100），
    //表示从片连接在IRQ2上；从片的ICW3设置为0x02（0000 0010），表示它连接在主片的IRQ2上。
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


// ============================================================
// inthandler21 — 键盘中断（IRQ1 → IDT 0x21）处理函数
// 由 _asm_inthandler21（naskfunc.nas）在保存现场后调用
// 参数: esp = 中断发生时的栈指针（指向被中断程序的 EFLAGS/CS/EIP）
// ============================================================
void inthandler21(int *esp)
{
    // 通过 BOOTINFO 结构体读取显存地址和屏幕尺寸
    // ADR_BOOTINFO = 0x0ff0，asmhead 在启动时写入了这些数据
    struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;

    // 在屏幕左上角画一个红底白字的提示条：
    // boxfill8 填充红色矩形（宽 32*8=256 像素，高 16 像素，从 (0,0) 开始）
    // 这个位置刚好覆盖屏幕最上面一行文字区
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);

    // 在矩形内显示白色文字，提示键盘中断已收到
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, "INT 21 (IRQ-1) : PS/2 keyboard");

    // 进入死循环，不再返回被中断的程序
    // 注意：这只是演示性的中断处理，实际系统应该在处理后返回
    for (;;) {
        io_hlt();   // 停机待机，等待下一个中断
    }
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
    struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, "INT 27 (IRQ-7) : PIC1 spurious");
    for (;;) {
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
    struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, "INT 2C (IRQ-12) : PS/2 mouse");
    for (;;) {
        io_hlt();
    }
}