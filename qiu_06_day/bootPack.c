#include <stdio.h>
#include "bootpack.h"	// 包含 BOOTINFO 结构体定义和函数声明

// ============================================================
// 内核主函数 —— 由 asmhead.nas 跳转至此（已处于 32 位保护模式）
// ============================================================
void HariMain(void)
{
	// 通过 BOOTINFO 结构体读取 asmhead 保存的启动信息
	// 只需记住一个基址 0x0ff0，成员偏移由编译器自动计算
	struct BOOTINFO *binfo = (struct BOOTINFO *)0x0ff0;

	init_gdtidt();																// 初始化 GDT 和 IDT（必须最先调用）
	init_palette();																// ① 设定调色板（16 种颜色）
	init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);						// ② 绘制桌面背景 + 任务栏 + 按钮
	putfonts8_asc(binfo->vram, binfo->scrnx, 8, 8, COL8_FFFFFF, "hello world"); // ③ 在屏幕左上角显示字符串

	// 显示变量
	char s[40];
	sprintf(s, "scrnx=%d", binfo->scrnx);
	putfonts8_asc(binfo->vram, binfo->scrnx, 16, 64, COL8_FFFFFF, s);

	// 显示鼠标指针（16×16 图案，坐标 (80,80) 约在屏幕中央）
	char mcursor[256];													 // 鼠标图案缓冲区（= 16×16 像素）
	int mx = 80, my = 80;												 // 鼠标初始坐标
	init_mouse_cursor8(mcursor, COL8_008484);							 // ④ 准备鼠标形状（背景色=桌面色→透明效果）
	putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); // ⑤ 把鼠标画到显存

	for (;;)
	{ // ④ 主循环：空闲即停机，由硬件中断唤醒
		io_hlt();
	}
}
