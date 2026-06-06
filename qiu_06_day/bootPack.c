#include <stdio.h>
#include "bootpack.h"

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

// ============================================================
// init_palette — 初始化前 16 种颜色的调色板
// 显存中每个像素只存颜色编号(0~255)，VGA 硬件用调色板把编号翻译成真实 RGB
// static char table_rgb[16*3] 等于汇编的 DB 指令：编译时在数据段分配 48 字节
// ============================================================
void init_palette(void)
{
	static unsigned char table_rgb[16 * 3] = {
		// 16 色 × 3 分量(R,G,B)
		0x00, 0x00, 0x00, //  0: 黑     (  0,   0,   0)
		0xff, 0x00, 0x00, //  1: 亮红   (255,   0,   0)
		0x00, 0xff, 0x00, //  2: 亮绿   (  0, 255,   0)
		0xff, 0xff, 0x00, //  3: 亮黄   (255, 255,   0)
		0x00, 0x00, 0xff, //  4: 亮蓝   (  0,   0, 255)
		0xff, 0x00, 0xff, //  5: 亮紫   (255,   0, 255)
		0x00, 0xff, 0xff, //  6: 浅亮蓝 (  0, 255, 255)
		0xff, 0xff, 0xff, //  7: 白     (255, 255, 255)
		0xc6, 0xc6, 0xc6, //  8: 亮灰   (198, 198, 198)
		0x84, 0x00, 0x00, //  9: 暗红   (132,   0,   0)
		0x00, 0x84, 0x00, // 10: 暗绿   (  0, 132,   0)
		0x84, 0x84, 0x00, // 11: 暗黄   (132, 132,   0)
		0x00, 0x00, 0x84, // 12: 暗蓝   (  0,   0, 132)
		0x84, 0x00, 0x84, // 13: 暗紫   (132,   0, 132)
		0x00, 0x84, 0x84, // 14: 暗浅蓝 (  0, 132, 132)
		0x84, 0x84, 0x84  // 15: 暗灰   (132, 132, 132)
	};
	set_palette(0, 15, table_rgb); // 将 0~15 号全部写入 VGA
	return;
}

// ============================================================
// set_palette — 将 RGB 数组写入 VGA 硬件调色板
// 硬件协议：
//   ① 向 0x03C8 写入起始编号 → VGA 内部计数器 = start
//   ② 向 0x03C9 连续写入 R、G、B（每色 6 位，共 18 位）
//   ③ VGA 接收完 3 字节后，内部计数器自动 +1，指向下一编号
// 为什么要关中断？写入过程中若被中断打断，中断代码也可能操作调色板，导致颜色错乱。
// 为什么要 /4？VGA 每个颜色分量只有 6 位(0~63)，而 table_rgb 是 8 位(0~255)，/4=右移 2 位。
// ============================================================
void set_palette(int start, int end, unsigned char *rgb)
{
	int i, eflags;
	eflags = io_load_eflags(); // 保存中断许可标志（可能之前已经开中断）
	io_cli();				   // 关闭中断，保证调色板写入是原子操作
	io_out8(0x03c8, start);	   // 告诉 VGA："从编号 start 开始改写"

	for (i = start; i <= end; i++)
	{
		// i 仅作循环计数；VGA 自己会递增内部编号，代码无需干预
		io_out8(0x03c9, rgb[0] / 4); // 写入当前编号的 R 分量
		io_out8(0x03c9, rgb[1] / 4); // 写入当前编号的 G 分量
		io_out8(0x03c9, rgb[2] / 4); // 写入当前编号的 B 分量（3 字节写完→编号+1）
		rgb += 3;					 // 指针跳到下一个颜色的 RGB 数据
	}
	io_store_eflags(eflags); // 恢复中断许可标志
	return;
}

// ============================================================
// boxfill8 — 在显存中填充一个纯色矩形
// 参数: vram=显存基址, xsize=屏幕每行像素数(用于寻址),
//       c=颜色编号, (x0,y0)/(x1,y1)=左上角/右下角坐标(均为包含)
// 偏移公式: vram[y * xsize + x]
//   显存是一维数组，屏幕是二维的。y*xsize 跳过前 y 整行，
//   +x 在该行内右移 x 列。等价于"第 y 页第 x 个字"。
// ============================================================
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1)
{
	int x, y;
	for (y = y0; y <= y1; y++)
	{ // 逐行
		for (x = x0; x <= x1; x++)
		{							 // 逐列
			vram[y * xsize + x] = c; // 写入 1 字节颜色号
		}
	}
	return;
}

// ============================================================
// init_screen8 — 绘制桌面布局
// 布局结构（y=屏幕高度=200）：
//   y=0    ┌──────────────────────────┐
//          │  浅蓝背景 (COL8_008484)    │  ← 0 .. y-29
//   y=172  ├──────────────────────────┤  ← 分割线 1 (COL8_C6C6C6)
//   y=173  │  白线 (COL8_FFFFFF)       │  ← 分割线 2（高光效果）
//   y=174  ├──────────────────────────┤  ← 分割线 3 (COL8_C6C6C6)
//          │         任务栏            │
//          │  [按钮1 消る]  [按钮2]    │  ← y-24 .. y-1
//   y=199  └──────────────────────────┘
//
// 按钮坐标说明：
//   按钮1（左）: 写死 x=3~59，因为左侧位置不随屏幕宽度变化
//   按钮2（右）: 用 xsize-47 .. xsize-4，自动贴右边缘（适应不同分辨率）
// 按钮立体感：上+左白边 → 高光；下+右灰边 → 阴影；内部黑底
// ============================================================
void init_screen8(char *vram, int xsize, int ysize)
{
	struct BOOTINFO *bootinfo;
	// 从地址 0x0ff0 读取 asmhead.nas 写入的启动信息
	bootinfo = (struct BOOTINFO *)0x0ff0;
	xsize = bootinfo->scrnx; // 屏幕宽 (320)
	ysize = bootinfo->scrny; // 屏幕高 (200)
	vram = bootinfo->vram;	 // 显存地址 (0xA0000)

	// ---- 桌面背景 ----
	boxfill8(vram, xsize, COL8_008484, 0, 0, xsize - 1, ysize - 29); // 浅蓝背景

	// ---- 三条分割线（把桌面和任务栏隔开） ----
	boxfill8(vram, xsize, COL8_C6C6C6, 0, ysize - 28, xsize - 1, ysize - 28); // 线1: 灰色
	boxfill8(vram, xsize, COL8_FFFFFF, 0, ysize - 27, xsize - 1, ysize - 27); // 线2: 白色（高光）
	boxfill8(vram, xsize, COL8_C6C6C6, 0, ysize - 26, xsize - 1, ysize - 1);  // 线3: 灰色 → 任务栏底

	// ---- 按钮1（左侧 "消る"） ----
	boxfill8(vram, xsize, COL8_FFFFFF, 3, ysize - 24, 59, ysize - 24); // 上边: 白色高光
	boxfill8(vram, xsize, COL8_FFFFFF, 2, ysize - 24, 2, ysize - 4);   // 左边: 白色高光
	boxfill8(vram, xsize, COL8_848484, 3, ysize - 4, 59, ysize - 4);   // 下边: 灰色阴影
	boxfill8(vram, xsize, COL8_848484, 59, ysize - 23, 59, ysize - 5); // 右边: 灰色阴影
	boxfill8(vram, xsize, COL8_000000, 2, ysize - 3, 59, ysize - 3);   // 内部: 黑色填充
	boxfill8(vram, xsize, COL8_000000, 60, ysize - 24, 60, ysize - 3); // 右缝: 黑色填充

	// ---- 按钮2（右侧） ----
	// 坐标用 xsize-47 而非硬编码 273，这样换分辨率时自动贴右边缘
	boxfill8(vram, xsize, COL8_848484, xsize - 47, ysize - 24, xsize - 4, ysize - 24); // 上边
	boxfill8(vram, xsize, COL8_848484, xsize - 47, ysize - 23, xsize - 47, ysize - 4); // 左边
	boxfill8(vram, xsize, COL8_FFFFFF, xsize - 47, ysize - 3, xsize - 4, ysize - 3);   // 下边: 白色高光
	boxfill8(vram, xsize, COL8_FFFFFF, xsize - 3, ysize - 24, xsize - 3, ysize - 3);   // 右边: 白色高光
	return;
}

// ============================================================
// putfont8 — 在屏幕上绘制一个 8×16 的等宽字符
// 参数: vram=显存, xsize=行宽, (x,y)=字符左上角坐标, c=颜色, font=16 字节点阵
//
// 工作原理：遍历 16 行点阵数据，每行一个字节 font[i]。
//   p = vram + (y + i) * xsize + x   → 第 i 行的首像素地址
//   p[0]~p[7]  → 该行从左到右 8 列（p[0]=最左列, p[7]=最右列）
//   若 font[i] 的某一位为 1，就把对应列涂成颜色 c
//
// 例如 font_A[1]=0x18=00011000?：
//   第4列(p[3])和第5列(p[4])亮，其余暗 → 形成 'A' 的左右竖笔
// ============================================================
void putfont8(char *vram, int xsize, int x, int y, char c, char *font)
{
	int i;
	char d, *p; // d=当前行点阵字节, p=当前行首像素地址
	for (i = 0; i < 16; i++)
	{									// 16 行，每行 1 字节控制 8 列
		p = vram + (y + i) * xsize + x; // 预计算第 i 行首地址，(y+i) 在屏幕上垂直下移 i 行
		d = font[i];					// 取第 i 行的点阵数据
		// 逐位检查：bit7→p[0](最左列) ... bit0→p[7](最右列)
		if ((d & 0x80) != 0)
		{
			p[0] = c;
		} // bit7=1 → 第1列亮
		if ((d & 0x40) != 0)
		{
			p[1] = c;
		} // bit6=1 → 第2列亮
		if ((d & 0x20) != 0)
		{
			p[2] = c;
		} // bit5=1 → 第3列亮
		if ((d & 0x10) != 0)
		{
			p[3] = c;
		} // bit4=1 → 第4列亮
		if ((d & 0x08) != 0)
		{
			p[4] = c;
		} // bit3=1 → 第5列亮
		if ((d & 0x04) != 0)
		{
			p[5] = c;
		} // bit2=1 → 第6列亮
		if ((d & 0x02) != 0)
		{
			p[6] = c;
		} // bit1=1 → 第7列亮
		if ((d & 0x01) != 0)
		{
			p[7] = c;
		} // bit0=1 → 第8列亮
	}
	return;
}

// ============================================================
// putfonts8_asc — 在屏幕上绘制一个以 0x00 结尾的字符串
// 参数: vram=显存, xsize=行宽, (x,y)=字符串左上角坐标, c=颜色, s=字符串指针
//
// 处理逻辑：
//   ① 遍历字符串，每次取一个字符
//   ② 从 hankaku 字库中查找该字符对应的 16 字节点阵（偏移 = 字符ASCII × 16）
//   ③ 调用 putfont8 绘制该字符
//   ④ x 坐标 +8，移到下一个字符位置
//   ⑤ 遇到结束符 0x00 停止
//
// 字符偏移公式: hankaku + *s * 16
//   *s          = 当前字符的 ASCII 码（如 'A'=65）
//   *s * 16     = 该字符在字库中的起始字节偏移（每个字符占16字节）
//   hankaku + N = 指向字库中第 N 字节的指针
// ============================================================
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s)
{
	extern char hankaku[4096]; // 导入 hankaku.obj 中的完整字库（256 字 × 16 行）
	for (; *s != 0x00; s++)	   // 遍历字符串，直到遇到结束符 0x00
	{
		putfont8(vram, xsize, x, y, c, hankaku + *s * 16); // 取当前字符的点阵数据绘制
		x += 8;											   // 一个字符占 8 像素宽，x 坐标右移，为下一个字符留位置
	}
	return;
}

// ============================================================
// init_mouse_cursor8 — 生成 16×16 鼠标指针点阵图案
// 参数: mouse = 输出缓冲区（256 字节，16×16 像素各 1 字节颜色号）
//       bc    = 背景色（与桌面背景相同，实现"透明"效果）
//
// 图案编码（ASCII 艺术）：
//   '*' → 黑色 (COL8_000000) — 鼠标轮廓、外框
//   'O' → 白色 (COL8_FFFFFF) — 鼠标内部填充
//   '.' → 背景色 (bc)        — 透明区域
// ============================================================
void init_mouse_cursor8(char *mouse, char bc)
{
	static char cursor[16][16] = {
		"**************..",
		"*OOOOOOOOOOO*...",
		"*OOOOOOOOOO*....",
		"*OOOOOOOOO*.....",
		"*OOOOOOOO*......",
		"*OOOOOOO*.......",
		"*OOOOOOO*.......",
		"*OOOOOOOO*......",
		"*OOOO**OOO*.....",
		"*OOO*..*OOO*....",
		"*OO*....*OOO*...",
		"*O*......*OOO*..",
		"**........*OOO*.",
		"*..........*OOO*",
		"............*OO*",
		".............***"};
	int x, y;
	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			if (cursor[y][x] == '*')
				mouse[y * 16 + x] = COL8_000000;
			if (cursor[y][x] == 'O')
				mouse[y * 16 + x] = COL8_FFFFFF;
			if (cursor[y][x] == '.')
				mouse[y * 16 + x] = bc;
		}
	}
	return;
}

// ============================================================
// putblock8_8 — 将一块矩形图案（如鼠标指针）拷贝到显存指定位置
// 参数: vram   = 显存基址
//       vxsize = 屏幕一行像素数
//       pxsize = 图块宽度, pysize = 图块高度
//       px0    = 目标左上角 X, py0 = 目标左上角 Y
//       buf    = 图案缓冲区
//       bxsize = 缓冲区一行像素数
// ============================================================
void putblock8_8(char *vram, int vxsize, int pxsize, int pysize, int px0, int py0, char *buf, int bxsize)
{
	int x, y;
	for (y = 0; y < pysize; y++)
	{
		for (x = 0; x < pxsize; x++)
		{
			vram[(py0 + y) * vxsize + (px0 + x)] = buf[y * bxsize + x];		// 计算显存地址并写入颜色数据
		}
	}
	return;
}

// ============================================================
// GDT / IDT 数据结构 —— 保护模式的分段与中断机制
//
// 为什么需要这两张表？
//   保护模式下不能直接访问物理内存，CPU 强制通过"描述符表"做间接寻址。
//   这就好比不能直接说出名字，得先查电话簿。
//
//   GDT（全局描述符表）：定义内存"段"的位置、大小和权限。
//     每个段描述符 8 字节，类似一个"权限门禁"——规定从哪到哪能读写/执行。
//   IDT（中断描述符表）：定义每个中断号对应的处理程序入口。
//     每个门描述符 8 字节，类似"紧急电话簿"——中断来了该打哪个号码。
//
// 内存布局（由 asmhead.nas 中的链接脚本保证）：
//   0x00270000  ← GDT 起始（最多 8192 个段描述符 × 8 字节 = 64 KB）
//   0x0026f800  ← IDT 起始（最多 256 个门描述符 × 8 字节 = 2 KB）
//   两个区域相邻，GDT 在高端，IDT 在低端，互不重叠。
// ============================================================

// ----------------------------------------------------------
// 段描述符结构体（8 字节）—— 对应 CPU 硬件定义的格式
// 内存布局（从低地址到高地址）：
//  字节0-1: limit_low    — 段界限低 16 位
//  字节2-3: base_low     — 基地址低 16 位
//  字节4:   base_mid     — 基地址中 8 位
//  字节5:   access_right — 访问权（P_DPL_S_TYPE 等标志）
//  字节6:   limit_high   — 高 4 位段界限 + 低 4 位标志（G/D/B/L/AVL）
//  字节7:   base_high    — 基地址高 8 位
// ============================================================
// init_gdtidt — 初始化 GDT 和 IDT
//
// 职责：
//   ① 将 GDT 的 8192 个槽位全部清零
//   ② 设置 3 个有实际意义的段描述符（见下方说明）
//   ③ 加载 GDTR 寄存器，告诉 CPU GDT 在哪
//   ④ 将 IDT 的 256 个槽位全部清零（暂时无中断处理程序）
//   ⑤ 加载 IDTR 寄存器，告诉 CPU IDT 在哪
//
// GDT 槽位分配：
//   [0] 空描述符（CPU 规定第 0 项必须为空，否则异常）
//   [1] 系统整体段：基址 0x00000000，大小 4GB，可读写（CPU 用这个段访问全体内存）
//   [2] 内核代码段：基址 0x00280000（bootpack 加载处），大小 512KB，可执行
//   其余 8189 个槽位预留，暂时全部无效。
// ============================================================
void init_gdtidt(void)
{
	struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *)0x00270000;	// GDT 起始地址
	struct GATE_DESCRIPTOR *idt = (struct GATE_DESCRIPTOR *)0x0026f800; 	// IDT 起始地址
	int i;

	// ---- GDT 初始化 ----
	for (i = 0; i < 8192; i++)	
	// 8192 个段描述符槽位,8192个槽位是因为段选择子占 16 位，其中 3 位是 RPL，1 位是 TI，剩下 12 位索引，
	// 所以最大索引为 2^12=4096，每个索引占 8 字节，所以 GDT 最大为 4096*8=32768 字节，即 32 KB，但为了对齐和预留空间，通常分配 64 KB（8192 个槽位）。
	{
		set_segmdesc(gdt + i, 0, 0, 0); // 清零：所有段设为"无效"（注意这里gdt指向的构造体是8字节，所以每次操作都影响8个字节）
	}
	// 段 [1]: 系统整体段 — 覆盖全部 4GB 地址空间，可读写
	//   limit = 0xffffffff, base = 0x00000000, ar = 0x4092（0xffffffff代表4GB的内存）
	//   0x4092 含义：Present+ring0+数据段+可读写+常规数据段
	//   这个段的作用是让 CPU 通过它访问全体内存，虽然我们不直接使用它，但必须存在，否则 CPU 无法访问内存。	
	set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, 0x4092);
	// 段 [2]: 内核代码段 — 覆盖 bootpack 所在的 512KB 区域，可执行
	//   limit = 0x0007ffff（= 512KB-1）, base = 0x00280000, ar = 0x409a
	//   0x409a 含义：Present+ring0+代码段+可执行+已访问位
	//   这个段的作用是让 CPU 通过它执行 bootpack 的代码，虽然我们不直接使用它，但必须存在，否则 CPU 无法执行代码。
	set_segmdesc(gdt + 2, 0x0007ffff, 0x00280000, 0x409a);
	load_gdtr(0xffff, 0x00270000); // GDTR: 表限长 = 64KB(8192×8-1), 表地址 = 0x270000

	// ---- IDT 初始化 ----
	for (i = 0; i < 256; i++)
	{
		set_gatedesc(idt + i, 0, 0, 0); // 清零：所有中断暂未注册处理程序
	}
	load_idtr(0x7ff, 0x0026f800); // IDTR: 表限长 = 2KB(256×8-1), 表地址 = 0x26f800
	return;
}

// ============================================================
// set_segmdesc — 填写一个段描述符（8 字节）
//
// 参数：
//   sd    = 指向目标段描述符的指针
//   limit = 段界限（字节数 - 1，即最大偏移量）
//   base  = 段基地址（32 位物理地址）
//   ar    = 访问权 + 标志位（16 位，低 8 位是 access_right，高 8 位是 limit_high 的标志部分）
//
// 段界限处理：
//   如果 limit > 0xfffff（即 1MB），说明需要以 4KB 为单位（G=1），
//   此时将 limit /= 0x1000，并设置 ar 的 bit15（G 位）。
//   CPU 收到 G=1 后会自动乘以 4KB 还原，所以实际范围可到 4GB。
//
// ar（Access Rights）编码说明（16 位，实际用低 12 位）：
//   bit15  : G（粒度）：0=字节, 1=4KB
//   bit14  : D/B（默认操作大小）：0=16位, 1=32位
//   bit13  : L（64位模式）：IA-32 下为 0
//   bit12  : AVL（软件可用位）：忽略
//   bit11-8: 保留
//   bit7   : P（存在位）：1=有效
//   bit6-5 : DPL（描述符特权级）：0=ring0(内核), 3=ring3(用户)
//   bit4   : S（系统/代码数据）：1=代码或数据段, 0=系统段
//   bit3   : 代码/数据段内：E(可执行)/C(一致)/R(可读)/A(已访问)
//            or 数据段内：ED(扩展方向)/W(可写)/A(已访问)
//   bit2-0 : TYPE（段类型）
//
// 常用 ar 值：
//   0x4092 = 0b0100 0000 1001 0010
//            G=1(4KB粒度) D=1(32位) P=1 DPL=0 S=1(数据段) W=1(可写)
//   0x409a = 0b0100 0000 1001 1010
//            G=1 D=1 P=1 DPL=0 S=1(代码段) E=1(可执行) R=1(可读)
// ============================================================
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar)
{
	if (limit > 0xfffff)
	{
		ar |= 0x8000;   // G_bit = 1：以 4KB 为粒度
		limit /= 0x1000; // 界限值除以 4KB，CPU 会自动乘回来
	}
	sd->limit_low = limit & 0xffff;          // 段界限低 16 位
	sd->base_low = base & 0xffff;            // 基地址低 16 位
	sd->base_mid = (base >> 16) & 0xff;      // 基地址中 8 位
	sd->access_right = ar & 0xff;            // access_right（低字节 = P_DPL_S_TYPE）
	sd->limit_high = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0); // 高 4 位段界限 + 高 4 位标志(G/D/B/L/AVL)
	sd->base_high = (base >> 24) & 0xff;     // 基地址高 8 位
	return;
}

// ============================================================
// set_gatedesc — 填写一个门描述符（8 字节）
//
// 参数：
//   gd       = 指向目标门描述符的指针
//   offset   = 中断处理函数的 32 位地址
//   selector = 目标代码段选择子（通常是 GDT[2] = 2*8 = 16）
//   ar       = access_right（低 8 位有效）
//
// ar 编码说明（中断门）：
//   bit7   : P（存在位）：1=有效
//   bit6-5 : DPL（描述符特权级）：通常 0（内核中断）
//   bit4-0 : TYPE：0x0e = 中断门（IF 自动清除，防嵌套）
//                   0x0f = 陷阱门（IF 不清除，用于调试）
//
// 关于 dw_count（参数复制计数）：
//   这是调用门（CALL GATE）才用的字段，表示从调用者栈复制多少个参数。
//   中断门和陷阱门不使用此字段，填 0 即可。
// ============================================================
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar)
{
	gd->offset_low = offset & 0xffff;       // 处理程序地址低 16 位
	gd->selector = selector;                 // 目标代码段选择子
	gd->dw_count = (ar >> 8) & 0xff;        // 调用门参数计数（中断门填 0）
	gd->access_right = ar & 0xff;            // 访问权（P_DPL_TYPE）
	gd->offset_high = (offset >> 16) & 0xffff; // 处理程序地址高 16 位
	return;
}
