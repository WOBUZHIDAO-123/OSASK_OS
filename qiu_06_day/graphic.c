/* 图形处理相关函数 */

void io_hlt(void);
void io_cli(void);
void io_out8(int port, int data);
int io_load_eflags(void);
void io_store_eflags(int eflags);

void init_palette(void);
void set_palette(int start, int end, unsigned char *rgb);
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);
void init_screen8(char *vram, int x, int y);
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void init_mouse_cursor8(char *mouse, char bc);
void putblock8_8(char *vram, int vxsize, int pxsize,
	int pysize, int px0, int py0, char *buf, int bxsize);

#define COL8_000000		0
#define COL8_FF0000		1
#define COL8_00FF00		2
#define COL8_FFFF00		3
#define COL8_0000FF		4
#define COL8_FF00FF		5
#define COL8_00FFFF		6
#define COL8_FFFFFF		7
#define COL8_C6C6C6		8
#define COL8_840000		9
#define COL8_008400		10
#define COL8_848400		11
#define COL8_000084		12
#define COL8_840084		13
#define COL8_008484		14
#define COL8_848484		15

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
	set_palette(0, 15, table_rgb);
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
// 参数：vram=显存基址, x=屏幕宽度, y=屏幕高度
// 调用时由 bootPack.c 的 HariMain 传入 binfo->vram/scrnx/scrny
//
// 按钮坐标说明：
//   按钮1（左）: 写死 x=3~59，因为左侧位置不随屏幕宽度变化
//   按钮2（右）: 用 xsize-47 .. xsize-4，自动贴右边缘（适应不同分辨率）
// 按钮立体感：上+左白边 → 高光；下+右灰边 → 阴影；内部黑底
// ============================================================
void init_screen8(char *vram, int x, int y)
{
	// ---- 桌面背景 ----
	boxfill8(vram, x, COL8_008484,  0,     0,      x -  1, y - 29); // 浅蓝背景

	// ---- 三条分割线（把桌面和任务栏隔开） ----
	boxfill8(vram, x, COL8_C6C6C6,  0,     y - 28, x -  1, y - 28); // 线1: 灰色
	boxfill8(vram, x, COL8_FFFFFF,  0,     y - 27, x -  1, y - 27); // 线2: 白色（高光）
	boxfill8(vram, x, COL8_C6C6C6,  0,     y - 26, x -  1, y -  1); // 线3: 灰色 → 任务栏底

	// ---- 按钮1（左侧 "消る"） ----
	boxfill8(vram, x, COL8_FFFFFF,  3,     y - 24, 59,     y - 24); // 上边: 白色高光
	boxfill8(vram, x, COL8_FFFFFF,  2,     y - 24,  2,     y -  4); // 左边: 白色高光
	boxfill8(vram, x, COL8_848484,  3,     y -  4, 59,     y -  4); // 下边: 灰色阴影
	boxfill8(vram, x, COL8_848484, 59,     y - 23, 59,     y -  5); // 右边: 灰色阴影
	boxfill8(vram, x, COL8_000000,  2,     y -  3, 59,     y -  3); // 内部: 黑色填充
	boxfill8(vram, x, COL8_000000, 60,     y - 24, 60,     y -  3); // 右缝: 黑色填充

	// ---- 按钮2（右侧，坐标用 x-47 而非硬编码，换分辨率时自动贴右边缘） ----
	boxfill8(vram, x, COL8_848484, x - 47, y - 24, x -  4, y - 24); // 上边
	boxfill8(vram, x, COL8_848484, x - 47, y - 23, x - 47, y -  4); // 左边
	boxfill8(vram, x, COL8_FFFFFF, x - 47, y -  3, x -  4, y -  3); // 下边: 白色高光
	boxfill8(vram, x, COL8_FFFFFF, x -  3, y - 24, x -  3, y -  3); // 右边: 白色高光
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
		if ((d & 0x80) != 0) { p[0] = c; } // bit7 → 第1列亮
		if ((d & 0x40) != 0) { p[1] = c; } // bit6 → 第2列亮
		if ((d & 0x20) != 0) { p[2] = c; } // bit5 → 第3列亮
		if ((d & 0x10) != 0) { p[3] = c; } // bit4 → 第4列亮
		if ((d & 0x08) != 0) { p[4] = c; } // bit3 → 第5列亮
		if ((d & 0x04) != 0) { p[5] = c; } // bit2 → 第6列亮
		if ((d & 0x02) != 0) { p[6] = c; } // bit1 → 第7列亮
		if ((d & 0x01) != 0) { p[7] = c; } // bit0 → 第8列亮
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
		".............***"
	};
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
			vram[(py0 + y) * vxsize + (px0 + x)] = buf[y * bxsize + x]; // 计算显存地址并写入颜色数据
		}
	}
	return;
}
