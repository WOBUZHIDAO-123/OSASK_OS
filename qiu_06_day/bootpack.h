/* ============================================================
 * bootpack.h — 内核主头文件
 *
 * 本文件集中管理所有模块共享的声明、结构体定义和宏常量。
 * 原本这些声明分散在 bootPack.c 文件内部，拆分后归入此头文件，
 * 使模块间接口清晰可见，避免重复声明。
 *
 * 各个模块说明：
 *   asmhead.nas — 启动阶段写入的 BOOTINFO 结构体
 *   naskfunc.nas — I/O 底层操作函数（IN/OUT/CLI/STI 等）
 *   graphic.c — 图形绘制函数（调色板、字体、鼠标指针等）
 *   dsctbl.c — GDT/IDT 描述符表操作函数
 * ============================================================ */

/* ============================================================
 * BOOTINFO 结构体 — 对应 asmhead.nas 从 BIOS 获取的启动信息
 *
 * asmhead.nas 在初始化阶段（设置显示模式、键盘 LED 后）将以下数据
 * 连续写入内存 0x0ff0 ~ 0x0fff 区域，C 语言通过此结构体读取。
 * 成员顺序必须与 asmhead.nas 中的 EQU 偏移严格一致。
 *
 * 内存布局：
 *   0x0ff0 [+0] char  cyls    — IPL 读到的磁盘柱面数
 *   0x0ff1 [+1] char  leds    — 键盘 LED 状态（Num/Caps/Scroll Lock）
 *   0x0ff2 [+2] char  vmode   — 显示模式色彩位数（8 = 256色）
 *   0x0ff3 [+3] char  reserve — 对齐填充
 *   0x0ff4 [+4] short scrnx   — 屏幕宽度（320）
 *   0x0ff6 [+6] short scrny   — 屏幕高度（200）
 *   0x0ff8 [+8] char* vram    — 显存起始地址（0xA0000）
 *
 * ADR_BOOTINFO = 0x00000ff0 是结构体在内存中的基地址。
 * ============================================================ */
struct BOOTINFO {
	char cyls;		/* ブートセクタはどこまでディスクを読んだのか */
	char leds;		/* ブート時のキーボードのLEDの状態 */
	char vmode;		/* ビデオモード  何ビットカラーか */
	char reserve;
	short scrnx, scrny;	/* 画面解像度 */
	char *vram;
};
#define ADR_BOOTINFO	0x00000ff0

/* ============================================================
 * I/O 底层函数 — 实现在 naskfunc.nas 中
 *
 * C 语言无法直接执行 IN/OUT/CLI/STI/HLT 等底层硬件指令，
 * 这些函数用汇编实现，通过 GLOBAL 导出符号供 C 调用。
 * 调用时 C 编译器自动在函数名前加下划线匹配汇编符号。
 * ============================================================ */
void io_hlt(void);				// HLT：CPU 停机待机，等待中断唤醒
void io_cli(void);				// CLI：清除 IF 位，禁止可屏蔽中断
void io_out8(int port, int data);		// OUT：向指定 I/O 端口写入 1 字节
int io_load_eflags(void);			// PUSHFD+POP：读取 EFLAGS 寄存器
void io_store_eflags(int eflags);		// PUSH+POPFD：写入 EFLAGS 寄存器
void load_gdtr(int limit, int addr);		// LGDT：加载全局描述符表寄存器 GDTR
void load_idtr(int limit, int addr);		// LIDT：加载中断描述符表寄存器 IDTR

/* ============================================================
 * 图形处理函数 — 实现在 graphic.c 中
 * ============================================================ */
void init_palette(void);						// 初始化前 16 色调色板
void set_palette(int start, int end, unsigned char *rgb);	// 向 VGA 调色板写入 RGB 数据
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);	// 填充矩形
void init_screen8(char *vram, int x, int y);			// 绘制桌面背景+任务栏+按钮
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);	// 绘制一个 8×16 字符
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);	// 绘制字符串
void init_mouse_cursor8(char *mouse, char bc);			// 准备鼠标指针图案（16×16）
void putblock8_8(char *vram, int vxsize, int pxsize,		// 拷贝矩形图块到显存
	int pysize, int px0, int py0, char *buf, int bxsize);

/* ============================================================
 * 颜色号宏 — 调色板编号的语义化名称
 *
 * 显存中每个像素只存颜色编号（0~255），VGA 硬件通过调色板
 * 将编号翻译成真实 RGB。这里的宏对应 init_palette 设定的前 16 色。
 * 注意编号不是真实颜色值，只是调色板索引。
 * ============================================================ */
#define COL8_000000		0   // 黑
#define COL8_FF0000		1   // 亮红
#define COL8_00FF00		2   // 亮绿
#define COL8_FFFF00		3   // 亮黄
#define COL8_0000FF		4   // 亮蓝
#define COL8_FF00FF		5   // 亮紫
#define COL8_00FFFF		6   // 浅亮蓝
#define COL8_FFFFFF		7   // 白
#define COL8_C6C6C6		8   // 亮灰
#define COL8_840000		9   // 暗红
#define COL8_008400		10  // 暗绿
#define COL8_848400		11  // 暗黄
#define COL8_000084		12  // 暗蓝
#define COL8_840084		13  // 暗紫
#define COL8_008484		14  // 暗浅蓝
#define COL8_848484		15  // 暗灰

/* ============================================================
 * 描述符表数据结构 — 保护模式的内存分段与中断机制
 *
 * 保护模式下 CPU 不能直接访问物理内存，必须通过描述符表间接寻址。
 *
 * SEGMENT_DESCRIPTOR（段描述符，8 字节）：
 *   定义内存段的位置（基址）、大小（界限）和权限（DPL/类型）。
 *   每个描述符对应 GDT 中的一个槽位。
 *
 * GATE_DESCRIPTOR（门描述符，8 字节）：
 *   定义中断/异常处理程序的入口地址和所属代码段。
 *   每个描述符对应 IDT 中的一个槽位。
 *
 * 内存布局（链接脚本保证）：
 *   0x00270000 — GDT 起始（64 KB，8192 个段描述符）
 *   0x0026f800 — IDT 起始（2 KB，256 个门描述符）
 * ============================================================ */
struct SEGMENT_DESCRIPTOR {
	short limit_low, base_low;	// 段界限低 16 位 + 基地址低 16 位
	char base_mid, access_right;	// 基地址中 8 位 + 访问权（P_DPL_S_TYPE）
	char limit_high, base_high;	// 段界限高 4 位+标志 + 基地址高 8 位
};
struct GATE_DESCRIPTOR {
	short offset_low, selector;	// 处理程序地址低 16 位 + 代码段选择子
	char dw_count, access_right;	// 参数计数（调用门用）+ 访问权
	short offset_high;		// 处理程序地址高 16 位
};

/* ============================================================
 * 描述符表操作函数声明 — 实现在 dsctbl.c 中
 * ============================================================ */
void init_gdtidt(void);							// 初始化 GDT 和 IDT
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar);	// 填写段描述符
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);		// 填写门描述符

/* ============================================================
 * 地址与权限常量
 * ============================================================ */
#define ADR_IDT			0x0026f800	// IDT 基地址（256 个门描述符 × 8 字节 = 2 KB）
#define LIMIT_IDT		0x000007ff	// IDT 界限（2 KB - 1 = 0x7FF）
#define ADR_GDT			0x00270000	// GDT 基地址（8192 个段描述符 × 8 字节 = 64 KB）
#define LIMIT_GDT		0x0000ffff	// GDT 界限（64 KB - 1 = 0xFFFF）
#define ADR_BOTPAK		0x00280000	// bootpack 在内存中的加载地址
#define LIMIT_BOTPAK		0x0007ffff	// bootpack 段界限（512 KB - 1）
#define AR_DATA32_RW		0x4092		// 数据段 ar：G=1 D=1 P=1 DPL=0 S=1 W=1
#define AR_CODE32_ER		0x409a		// 代码段 ar：G=1 D=1 P=1 DPL=0 S=1 E=1 R=1
