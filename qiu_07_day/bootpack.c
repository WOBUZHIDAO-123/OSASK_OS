/* ============================================================
 * bootpack.c — 内核主函数
 *
 * 第7天新增功能：键盘按键显示
 *   在屏幕左上角显示按下的键盘按键的16进制编码（扫描码）
 *   通过轮询键盘缓冲区 keybuf，由中断处理函数填充数据
 * ============================================================ */

#include "bootpack.h"
#include <stdio.h>

// 导入键盘缓冲区全局变量（由 int.c 定义，中断处理函数填充数据）
extern struct KEYBUF keybuf;

// ============================================================
// HariMain — 系统主入口
// 流程：初始化硬件 → 画桌面/鼠标 → 允许中断 → 主循环轮询键盘
// ============================================================
void HariMain(void)
{
	struct BOOTINFO *binfo = (struct BOOTINFO *)ADR_BOOTINFO;
	char s[40], mcursor[256];
	int mx, my, i, j;

	// ---------- 硬件初始化 ----------
	init_gdtidt(); // 设置 GDT + IDT（内存段描述符+中断描述符表）
	init_pic();	   // 初始化 PIC（中断控制器，映射 IRQ→INT 号）
	io_sti();	   // IDT/PIC 初始化结束，解除 CPU 中断禁止（允许中断）

	// ---------- 图形界面初始化 ----------
	init_palette();														 // 设置 VGA 调色板（前 16 色）
	init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);				 // 绘制桌面背景+任务栏
	mx = (binfo->scrnx - 16) / 2;										 // 鼠标指针 X 坐标：屏幕水平居中
	my = (binfo->scrny - 28 - 16) / 2;									 // 鼠标指针 Y 坐标：桌面区域垂直居中
	init_mouse_cursor8(mcursor, COL8_008484);							 // 生成鼠标图案到缓冲区
	putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); // 画鼠标到屏幕
	sprintf(s, "(%d, %d)", mx, my);										 // 组装坐标字符串
	putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);		 // 在左上角显示坐标

	// ---------- 允许特定设备中断 ----------
	io_out8(PIC0_IMR, 0xf9); /* 11111001 → 允许 PIC1（级联）和键盘（IRQ1）*/
	io_out8(PIC1_IMR, 0xef); /* 11101111 → 允许鼠标（IRQ12）*/

	// ============================================================
	// 主循环：轮询键盘缓冲区
	//
	// 工作流程：
	//   ① 关中断 → 检查 keybuf.flag
	//   ② 若无数据 → STI + HLT（开中断 + 待机，等待键盘中断唤醒）
	//   ③ 若有数据 → 读取扫描码 → 清 flag → 开中断 → 屏幕显示
	//
	// 为什么要关中断再检查 keybuf？
	//   防止在读取 keybuf 的瞬间中断发生，导致前后数据不一致。
	//   关中断 → 读取 → 开中断，保证原子操作。
	// ============================================================
	for (;;)
	{
		io_cli();			  // 关中断：原子操作保护
		if (keybuf.next == keybuf.read) // 环形缓冲区中next等于read表示没有数据
		{
			io_stihlt(); // 开中断 + HLT 待机，等待中断唤醒
		}
		else // 键盘缓冲区有数据
		{
			i = keybuf.data[keybuf.read]; // 取出键盘扫描码
			keybuf.read=(keybuf.read+1)%32; // 更新缓冲区状态（标记已读取）
			io_sti(); // 开中断（显示操作不受影响）

			// 将扫描码格式化为 2 位 16 进制字符串并显示
			sprintf(s, "%02X", i);											 // 例：按键 'A' → "1E"
			boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 16, 15, 31); // 清空显示区域（覆盖旧数据）
			putfonts8_asc(binfo->vram, binfo->scrnx, 0, 16, COL8_FFFFFF, s); // 显示键盘数据
		}
	}
}
