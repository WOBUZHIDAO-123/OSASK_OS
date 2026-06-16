/* ============================================================
 * bootpack.c — 内核主函数
 *
 * 第7天新增功能：键盘按键显示
 *   在屏幕左上角显示按下的键盘按键的16进制编码（扫描码）
 *   通过轮询 FIFO8 环形缓冲区 keyfifo，由中断处理函数填充数据
 * ============================================================ */

#include "bootpack.h"
#include <stdio.h>

/* ============================================================
 * 键盘控制器 I/O 端口宏
 *
 * 键盘控制器（8042 / KBC）管理键盘和鼠标两个设备，
 * 使用两个 I/O 端口：
 *   0x0060 — 数据端口（读写键盘数据/扫描码、鼠标数据包）
 *   0x0064 — 状态端口（只读）/ 控制端口（只写）
 *   同地址不同功能，由 IN/OUT 指令区分
 *
 * 鼠标通过 KBC 以"转发"方式通信：
 *   向 PORT_KEYCMD 写入 0xd4 → 后续 PORT_KEYDAT 的字节被转发给鼠标
 *   鼠标的中断走 IRQ12（从片 PIC1 的第 4 个 IRQ）
 * ============================================================ */

// 键盘数据端口：读取扫描码、写入键盘控制器的配置数据
//   IN  — 读键盘控制器输出缓冲区（扫描码）
//   OUT — 向键盘控制器写入命令参数或配置字节
#define PORT_KEYDAT 0x0060

// 键盘状态端口（只读）：读取键盘控制器状态字节
//   bit1 = 1 表示键盘控制器输出缓冲区为空，不可写入数据
#define PORT_KEYSTA 0x0064

// 键盘控制端口（只写）：向键盘控制器发送命令
//   写入命令码后，通常还需向 PORT_KEYDAT 写入参数
#define PORT_KEYCMD 0x0064

// 键盘控制器状态掩码：CPU 能否向 KBC 发送数据
//   0x02 = 0000 0010（bit1）
//   状态字节 bit1=1 → 键盘输出缓冲区满 → 不可发送数据
//   bit1=1 → KBC 正忙（上次 CPU 发的数据还没处理完）
//   bit1=0 → KBC 空闲，可以接受 CPU 发送的命令/数据
//   CPU 在写 PORT_KEYCMD / PORT_KEYDAT 前，必须先轮询此位确认空闲
#define KEYSTA_SEND_NOTREADY 0x02

// 键盘控制器命令：设置模式
//   发送此命令后，需向 PORT_KEYDAT 写入模式字节
#define KEYCMD_WRITE_MODE 0x60

// 键盘控制器模式：启用鼠标中断
//   0x47 = 0100 0111
//   bit0=1 — 启用键盘中断
//   bit1=1 — 启用鼠标中断
//   bit2=1 — 系统标志
//   bit6=1 — 兼容 IBM PC 模式
#define KBC_MODE 0x47

// 键盘控制器命令：将下一字节转发给鼠标
//   向 PORT_KEYCMD 写入 0xd4 后，再向 PORT_KEYDAT 写入的命令数据
//   会被 KBC 转发到鼠标设备，用于初始化或配置鼠标
#define KEYCMD_SENDTO_MOUSE 0xd4

// 鼠标命令：启用鼠标数据上报
//   发送此命令到鼠标后，鼠标开始主动发送位移/按键数据包
//   在此之前鼠标处于静默状态，不会产生任何中断
#define MOUSECMD_ENABLE 0xf4

// 导入键盘 FIFO 缓冲区全局变量（由 int.c 定义，中断处理函数填充数据）
extern struct FIFO8 keyfifo;

// ============================================================
// wait_KBC_sendready — 等待键盘控制器（KBC）就绪
//
// 功能：轮询 PORT_KEYSTA 的状态位，直到 KBC 的发送缓冲区空闲。
//       KBC 处理完上一条命令前不会接受新数据，必须等待。
//
// 原理：
//   读取状态端口 0x0064，检查 bit1（KEYSTA_SEND_NOTREADY）。
//   bit1=0 → KBC 空闲，可以发送下一条命令/数据
//   bit1=1 → KBC 仍在处理上一条，继续循环等待
//
// 使用场景：
//   每次向 PORT_KEYCMD 或 PORT_KEYDAT 写入前都应调用此函数。
// ============================================================
void wait_KBC_sendready(void)
{
	for (;;)
	{
		if ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) // 读取状态端口，检查 bit1 是否为 0（空闲）
		{
			break;
		}
	}
}

// ============================================================
// init_keyboard — 初始化键盘控制器
//
// 功能：向键盘控制器发送模式设置命令，启用键盘中断和鼠标中断。
//
// 流程：
//   ① wait_KBC_sendready — 等待 KBC 空闲
//   ② 写命令码 KEYCMD_WRITE_MODE (0x60) 到 PORT_KEYCMD
//   ③ wait_KBC_sendready — 等待 KBC 处理完命令码
//   ④ 写模式字节 KBC_MODE (0x47) 到 PORT_KEYDAT
//
// 模式字节 0x47 含义：
//   bit0=1 — 启用键盘中断
//   bit1=1 — 启用鼠标中断
//   bit2=1 — 系统标志
//   bit6=1 — 兼容 IBM PC 模式
// ============================================================
void init_keyboard(void)
{
	wait_KBC_sendready();
	io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
	wait_KBC_sendready();
	io_out8(PORT_KEYDAT, KBC_MODE);
	return;
}

// ============================================================
// enable_mouse — 启用鼠标设备
//
// 功能：通过键盘控制器（KBC）向鼠标发送启用命令，
//       让鼠标开始主动上报数据（位移和按键状态）。
//
// 流程：
//   ① wait_KBC_sendready — 等待 KBC 空闲
//   ② 写 KEYCMD_SENDTO_MOUSE (0xd4) 到 PORT_KEYCMD
//      指示 KBC：下一字节转发给鼠标，而非发给键盘
//   ③ wait_KBC_sendready — 等待 KBC 处理完命令
//   ④ 写 MOUSECMD_ENABLE (0xf4) 到 PORT_KEYDAT
//      此字节被 KBC 转发给鼠标，鼠标收到后开始发送数据包
//
// 备注：
//   鼠标启用后，每次移动或按键都会触发 IRQ12 中断，
//   中断处理函数 inthandler2c 从 PORT_KEYDAT 读取鼠标数据。
// ============================================================
void enable_mouse(void)
{
	wait_KBC_sendready();
	io_out8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
	wait_KBC_sendready();
	io_out8(PORT_KEYDAT, MOUSECMD_ENABLE);
	return;
}

// ============================================================
// HariMain — 系统主入口
// 流程：初始化硬件 → 画桌面/鼠标 → STI 恢复响应 PIC 中断 → 主循环轮询键盘
// ============================================================
void HariMain(void)
{
	struct BOOTINFO *binfo = (struct BOOTINFO *)ADR_BOOTINFO;

	// ============================================================
	// 第一步：硬件初始化
	//   ① 设置 GDT（全局描述符表）和 IDT（中断描述符表）
	//   ② 初始化 PIC（可编程中断控制器），映射 IRQ → INT 号
	//   ③ STI 设 IF=1，CPU 开始响应 PIC 发来的中断信号
	// ============================================================
	init_gdtidt();
	init_pic();
	io_sti();

	// ============================================================
	// 第二步：图形界面初始化
	//   ① 设置调色板、绘制桌面背景和任务栏
	//   ② 计算鼠标指针位置（屏幕中央）、生成鼠标图案并显示
	//   ③ 在左上角显示鼠标坐标字符串
	// ============================================================

	// --- 调色板和桌面 ---
	init_palette();
	init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);

	// --- 鼠标指针 ---
	char mcursor[256];
	int mx = (binfo->scrnx - 16) / 2;									 // 水平居中
	int my = (binfo->scrny - 28 - 16) / 2;								 // 垂直居中（避开任务栏）
	init_mouse_cursor8(mcursor, COL8_008484);							 // 生成鼠标图案
	putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); // 画到屏幕上

	// --- 左上角信息显示 ---
	char s[40];
	sprintf(s, "(%d, %d)", mx, my);									// 组装坐标字符串
	putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s); // 显示坐标

	// ============================================================
	// 第三步：初始化键盘 FIFO 缓冲区 + 允许设备中断
	//
	// 【双缓冲架构说明】
	// 键盘数据从按下到显示，经过两层缓冲区：
	//
	//   ① KBC 硬件缓冲区（1 字节）
	//      位于键盘控制器 8042 芯片内部，只有一个寄存器。
	//      键盘按下 → 扫描码存入此寄存器 → 触发 IRQ1 中断。
	//      若 CPU 不及时读走，下一个按键会覆盖它。
	//      中断处理函数 inthandler21 通过 IN PORT_KEYDAT 将其读走。
	//
	//   ② FIFO8 软件缓冲区（32 字节，位于 RAM）
	//      中断处理函数从硬件寄存器读出的数据存入此缓冲区。
	//      主循环 HariMain 从这里慢慢取数据显示。
	//      因为有 32 字节容量，即使主循环偶尔被其他事耽搁，数据也不会丢。
	//
	// ============================================================

	// --- 初始化 FIFO8 软件缓冲区（32 字节，位于 RAM） ---
	char keybuf[32];
	fifo8_init(&keyfifo, 32, keybuf);

	// --- 设置 PIC 中断屏蔽寄存器 ---
	io_out8(PIC0_IMR, 0xf9); /* 11111001 → 允许 PIC1（级联）和键盘（IRQ1）*/
	io_out8(PIC1_IMR, 0xef); /* 11101111 → 允许鼠标（IRQ12）*/

	// --- 初始化键盘控制器（设置模式，启用键盘/鼠标中断） ---
	init_keyboard();

	// --- 启用鼠标设备（向鼠标发送 0xf4，鼠标开始上报数据，触发 IRQ12） ---
	enable_mouse();

	// ============================================================
	// 第四步：主循环 — 轮询键盘缓冲区
	//
	// 工作流程：
	//   ① CLI 清0 IF → 屏蔽 PIC 中断信号 → 检查缓冲区（fifo8_status）
	//   ② 无数据 → STI（恢复响应）+ HLT 待机，等待键盘中断唤醒
	//   ③ 有数据 → fifo8_get 读取扫描码 → STI 恢复响应 → 屏幕显示
	//
	// 为什么要先屏蔽 PIC 中断信号再检查？
	//   防止在读取缓冲区的瞬间 PIC 发来中断，导致数据状态不一致。
	//   CLI → 检查/读取 → STI，保证原子操作。
	// ============================================================
	for (;;)
	{
		io_cli();						 // 屏蔽 PIC 中断信号：保证后续检查和读取不会被中断干扰
		if (fifo8_status(&keyfifo) == 0) // 缓冲区为空？
		{
			io_stihlt(); // STI 恢复响应 + HLT 待机，等键盘中断唤醒
		}
		else
		{
			// 缓冲区有数据 → 读取扫描码
			int i = fifo8_get(&keyfifo); // 从环形缓冲区取出一个字节
			io_sti();					 // 恢复 PIC 信号响应（显示操作允许被打断）

			// 在屏幕左上角第二行（y=16）显示扫描码
			sprintf(s, "%02X", i);											 // 格式化为 2 位 16 进制（如 "1E"）
			boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 16, 15, 31); // 清空显示区域
			putfonts8_asc(binfo->vram, binfo->scrnx, 0, 16, COL8_FFFFFF, s); // 显示键盘数据
		}
	}
}
