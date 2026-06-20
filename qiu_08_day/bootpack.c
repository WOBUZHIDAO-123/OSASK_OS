/* ============================================================
 * bootpack.c — 内核主函数
 *
 * 第8天新增功能：鼠标指针跟踪移动
 *   鼠标移动时实时更新指针在屏幕上的位置，
 *   并在左上角显示鼠标坐标和按键状态。
 *   键盘中断 → keyfifo（IRQ1）
 *   鼠标中断 → mousefifo（IRQ12）
 *   主循环轮询两个 FIFO 缓冲区，由中断处理函数填充数据
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

// 键盘和鼠标 FIFO 缓冲区全局变量
//   中断处理函数 (inthandler21, inthandler2c) 在中断发生时将数据写入这些缓冲区，
//   主循环 (HariMain) 轮询读取，用于更新显示。
extern struct FIFO8 keyfifo;
extern struct FIFO8 mousefifo;

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
void enable_mouse(struct MOUSE_DEC *mdec)
{
	wait_KBC_sendready();
	io_out8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
	wait_KBC_sendready();
	io_out8(PORT_KEYDAT, MOUSECMD_ENABLE);
	mdec->phase = 0; // 初始化鼠标解码状态：phase=0 表示等待鼠标 ACK (0xfa) 确认
	return;
}

// ============================================================
// mouse_decode — 鼠标数据包解码
//
// 功能：从鼠标 FIFO 逐字节接收并组装 3 字节数据包，
//       完成字节拼接后解析出按键状态、X/Y 位移量。
//
// 解码流程（4 阶段状态机）：
//    phase=0 — 等待 ACK（0xfa），确认鼠标已就绪
//    phase=1 — 接收字节 0（按键状态 + 符号位），同步校验
//    phase=2 — 接收字节 1（X 轴位移量，需符号扩展）
//    phase=3 — 接收字节 2（Y 轴位移量，需符号扩展 + Y 轴取反）
//
// 鼠标数据包格式（字节 0）：
//   bit0   — 左键（1=按下）
//   bit1   — 右键（1=按下）
//   bit2   — 中键（1=按下）
//   bit3   — 固定为 1（用于同步校验）
//   bit4   — X 位移符号位（1=负数）
//   bit5   — Y 位移符号位（1=负数）
//   bit6   — 固定为 1（用于同步校验）
//   bit7   — 溢出标志（0=正常）
//
// 参数：
//   mdec — 鼠标解码结构体，保存组装状态和解析结果
//   dat  — 从鼠标 FIFO 读取的一个字节
//
// 返回：
//   0 — 数据包尚未收齐（继续等待下一字节）
//   1 — 3 字节收齐且解析完成，可通过 mdec->btn/x/y 读取数据
// ============================================================
int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat)
{
	if (mdec->phase == 0)
	{
		// phase 0: 等待鼠标 ACK（0xfa）
		// enable_mouse 发送 0xf4 后，鼠标回复 0xfa 表示"已就绪"
		if (dat == 0xfa)
		{
			mdec->phase = 1; // ACK 确认，开始接收数据包
		}
		return 0;
	}
	if (mdec->phase == 1)
	{
		// phase 1: 接收字节 0（按键状态 + 符号位）
		// 0xc8 = 1100 1000，作为掩码只检查 bit3（同步标志，必须为 1）
		// 用 == 0x08 要求：bit3=1 且 bit6/bit7=0（无溢出）
		// 若不符合则丢弃，防止数据不同步导致显示错乱
		if ((dat & 0xc8) == 0x08)
		{
			mdec->buf[0] = dat; // buf[0] 低 3 位 = 按键状态，bit4/5 = 位移符号
			mdec->phase = 2;
		}
		return 0;
	}
	if (mdec->phase == 2)
	{
		// phase 2: 接收字节 1（X 轴位移量，带符号，1 字节范围）
		mdec->buf[1] = dat;
		mdec->phase = 3;
		return 0;
	}
	if (mdec->phase == 3)
	{
		// phase 3: 接收字节 2（Y 轴位移量，带符号，1 字节范围）
		// 3 字节收齐 → 解析 → 回到 phase 1 等待下一数据包
		mdec->buf[2] = dat;
		mdec->phase = 1; // 回到 phase 1（不回 0，ACK 只出现一次）

		// --- 解析数据包 ---
		mdec->btn = mdec->buf[0] & 0x07; // 取 buf[0] 低 3 位：左中右键状态
		mdec->x = mdec->buf[1];			 // X 位移量（需要符号扩展）
		mdec->y = mdec->buf[2];			 // Y 位移量（需要符号扩展）

		// 符号扩展：若 buf[0] 的 bit4=1，X 为负数 → 将高 24 位置 1
		if ((mdec->buf[0] & 0x10) != 0)
		{
			mdec->x |= 0xffffff00;
		}
		// 符号扩展：若 buf[0] 的 bit5=1，Y 为负数 → 将高 24 位置 1
		if ((mdec->buf[0] & 0x20) != 0)
		{
			mdec->y |= 0xffffff00;
		}

		mdec->y = -mdec->y; // Y 轴取反：鼠标数据包中 Y 向下为正，
							// 若要匹配屏幕坐标（Y 向下为正），此处不应取反；
							// 暂时保留取反以维持当前显示行为
		return 1;			// 返回 1 通知调用方数据包已就绪
	}
	return -1; // 不应到达的分支（容错）
}

// ============================================================
// HariMain — 系统主入口
// 流程：初始化硬件 → 画桌面/鼠标 → STI 恢复响应 PIC 中断 → 初始化键盘/鼠标 → 主循环轮询
// ============================================================
void HariMain(void)
{
	struct BOOTINFO *binfo = (struct BOOTINFO *)ADR_BOOTINFO;
	struct MOUSE_DEC mdec; // 鼠标数据解码用结构体，保存鼠标数据包和接收状态

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
	char mcursor[256];													 // 鼠标图案缓冲区，256 字节（16x16 位图）
	int mx = (binfo->scrnx - 16) / 2;									 // 水平居中
	int my = (binfo->scrny - 28 - 16) / 2;								 // 垂直居中（避开任务栏）
	init_mouse_cursor8(mcursor, COL8_008484);							 // 生成鼠标图案到 mcursor，背景色为 COL8_008484
	putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); // 将鼠标显示到屏幕上

	// --- 左上角信息显示 ---
	char s[40];
	sprintf(s, "(%d, %d)", mx, my);									// 组装坐标字符串
	putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s); // 显示坐标

	// ============================================================
	// 第三步：初始化 FIFO8 软件缓冲区 + 允许设备中断
	//
	// 【双缓冲架构说明】
	// 键盘和鼠标数据从设备到显示，都经过两层缓冲区：
	//
	//   ┌─ 键盘（IRQ1）────────────────────────────┐
	//   │  ① KBC 硬件缓冲区（1 字节）               │
	//   │    位于键盘控制器 8042 芯片内部。         │
	//   │    键盘按下 → 扫描码存入此寄存器 → IRQ1。 │
	//   │    若 CPU 不及时读走，下一个按键会覆盖。   │
	//   │    由 inthandler21 通过 IN PORT_KEYDAT 读 │
	//   │                                           │
	//   │  ② keyfifo 软件缓冲区（32 字节，RAM）     │
	//   │    inthandler21 存入，HariMain 主循环读取 │
	//   └───────────────────────────────────────────┘
	//
	//   ┌─ 鼠标（IRQ12）───────────────────────────┐
	//   │  ① KBC 硬件缓冲区（1 字节，与键盘共用）   │
	//   │    鼠标移动/按键 → 数据包（分3字节发送）→ │
	//   │    每字节触发一次 IRQ12。                 │
	//   │    由 inthandler2c 通过 IN PORT_KEYDAT 读 │
	//   │                                           │
	//   │  ② mousefifo 软件缓冲区（128 字节，RAM） │
	//   │    inthandler2c 存入，HariMain 主循环读取 │
	//   └───────────────────────────────────────────┘
	//
	// ============================================================

	// --- 初始化 FIFO8 软件缓冲区（位于 RAM） ---
	char keybuf[32];					   // 键盘数据缓冲区，32 字节
	char mousebuf[128];					   // 鼠标数据缓冲区，128 字节
	fifo8_init(&keyfifo, 32, keybuf);	   // 初始化键盘 FIFO，32 字节容量
	fifo8_init(&mousefifo, 128, mousebuf); // 初始化鼠标 FIFO，128 字节容量

	// --- 设置 PIC 中断屏蔽寄存器 ---
	io_out8(PIC0_IMR, 0xf9); /* 11111001 → 允许 PIC1（级联）和键盘（IRQ1）*/
	io_out8(PIC1_IMR, 0xef); /* 11101111 → 允许鼠标（IRQ12）*/

	// --- 初始化键盘控制器（设置模式，启用键盘/鼠标中断） ---
	init_keyboard();

	// --- 启用鼠标设备（向鼠标发送 0xf4，鼠标开始上报数据，触发 IRQ12） ---
	enable_mouse(&mdec); // 启用鼠标，传入 mdec 初始化 phase=0（等待 ACK），供 mouse_decode 使用

	// ============================================================
	// 第四步：主循环 — 轮询键盘和鼠标缓冲区
	//
	// 鼠标数据包组装（3 状态有限状态机）：
	//   鼠标启用后，每次移动/按键发送一个 3 字节数据包，
	//   但每个字节通过独立的 IRQ12 中断到达。
	//   因此主循环需要用 mouse_phase 状态机将 3 个字节拼回完整数据包。
	//
	//   状态机流程：
	//     phase=0  — 等待鼠标 ACK（0xfa）
	//     phase=1  — 接收字节 0（按键状态 + 符号位）
	//     phase=2  — 接收字节 1（X 轴位移量）
	//     phase=3  — 接收字节 2（Y 轴位移量）→ 拼包完成，重置为 phase=1
	//
	// 工作流程：
	//   ① CLI 清0 IF → 屏蔽 PIC 中断信号 → 同时检查两个缓冲区
	//   ② 均无数据 → STI + HLT 待机，等待键盘/鼠标中断唤醒
	//   ③ 键盘有数据 → fifo8_get 读取 → STI → 在左侧显示扫描码
	//   ④ 鼠标有数据 → fifo8_get 读取 → STI → mouse_decode 拼装数据包
	//      解析出位移/按键后：更新指针位置、边界裁剪、刷新显示
	//
	// 为什么要先屏蔽 PIC 中断信号再检查？
	//   防止在检查/读取缓冲区的瞬间 PIC 发来中断，导致状态不一致。
	//   CLI → 检查/读取 → STI，保证原子操作。
	// ============================================================
	for (;;)
	{
		io_cli();													// 屏蔽 PIC 中断信号：保证后续检查和读取不会被中断干扰
		if (fifo8_status(&keyfifo) + fifo8_status(&mousefifo) == 0) // 键盘和鼠标均无数据？
		{
			io_stihlt(); // STI 恢复响应 + HLT 待机，等待键盘/鼠标中断唤醒
		}
		else
		{
			if (fifo8_status(&keyfifo) != 0)
			{
				int i = fifo8_get(&keyfifo); // 从键盘 FIFO 取出一个字节（扫描码）
				io_sti();					 // 恢复 PIC 信号响应（显示操作允许被打断）
				// 在屏幕左上角第二行左侧（x=0, y=16）显示键盘扫描码
				sprintf(s, "%02X", i);											 // 格式化为 2 位 16 进制（如 "1E"）
				boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 16, 15, 31); // 清空键盘显示区域
				putfonts8_asc(binfo->vram, binfo->scrnx, 0, 16, COL8_FFFFFF, s); // 显示键盘数据
			}
			else if (fifo8_status(&mousefifo) != 0)
			{
				int i = fifo8_get(&mousefifo); // 从鼠标 FIFO 取出一个字节
				io_sti();					   // 恢复 PIC 信号响应（显示操作允许被打断）

				// 将字节送入解码函数，mouse_decode 根据 phase 自动拼装
				// 返回 0 = 还没收完, 返回 1 = 3 字节收齐
				if (mouse_decode(&mdec, i) != 0)
				{
					/* ---- 显示鼠标信息（左上角第二行右侧） ---- */

					// 模板 "[lcr %4d %4d]"，l/c/r 分别标记左/中/右键
					// 若按键按下，对应小写字母替换为大写
					sprintf(s, "[lcr %4d %4d]", mdec.x, mdec.y);
					// btn 是 buf[0] 的低 3 位（bit0~bit2），每一位代表一个按键
					// & 0x01 提取 bit0 → 1=左键按下
					// & 0x02 提取 bit1 → 1=右键按下
					// & 0x04 提取 bit2 → 1=中键按下
					if ((mdec.btn & 0x01) != 0)
					{
						s[1] = 'L'; // bit0 → 左键
					}
					if ((mdec.btn & 0x02) != 0)
					{
						s[3] = 'R'; // bit1 → 右键
					}
					if ((mdec.btn & 0x04) != 0)
					{
						s[2] = 'C'; // bit2 → 中键
					}

					boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 32, 16, 32 + 15 * 8 - 1, 31);
					putfonts8_asc(binfo->vram, binfo->scrnx, 32, 16, COL8_FFFFFF, s);

					/* ---- 更新鼠标屏幕位置 ---- */

					boxfill8(binfo->vram, binfo->scrnx, COL8_008484, mx, my, mx + 15, my + 15); // 擦除旧指针
					mx += mdec.x;																// 累加 X 位移
					my += mdec.y;																// 累加 Y 位移
					if (mx < 0)
					{
						mx = 0;
					} // 左边界
					if (my < 0)
					{
						my = 0;
					} // 上边界
					if (mx > binfo->scrnx - 16)
					{
						mx = binfo->scrnx - 16;
					} // 右边界
					if (my > binfo->scrny - 16)
					{
						my = binfo->scrny - 16;
					} // 下边界

					sprintf(s, "(%3d,%3d)", mx, my); // 显示新坐标
					boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 0, 79, 15);
					putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);
					putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); // 绘制新指针
				}
			}
		}
	}
}
