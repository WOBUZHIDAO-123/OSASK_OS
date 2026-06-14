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
	char cyls;      // 磁盘柱面数（IPL 读了几个柱面）
	char leds;      // 键盘 LED 状态（Num Lock / Caps Lock / Scroll Lock）
	char vmode;     // 显示模式色彩位数（8 = 256色）
	char reserve;   // 对齐填充
	short scrnx;    // 屏幕宽度（像素）
	short scrny;    // 屏幕高度（像素）
	char *vram;     // 显存基址
};
#define ADR_BOOTINFO	0x00000ff0	/* ADR_BOOTINFO = 0x00000ff0 是结构体在内存中的基地址 */

/* ============================================================
 * I/O 底层函数 — 实现在 naskfunc.nas 中
 *
 * C 语言无法直接执行 IN/OUT/CLI/STI/HLT 等底层硬件指令，
 * 这些函数用汇编实现，通过 GLOBAL 导出符号供 C 调用。
 * 调用时 C 编译器自动在函数名前加下划线匹配汇编符号。
 * ============================================================ */

// 执行 HLT 指令，CPU 进入低功耗待机状态，直到下一次中断发生才被唤醒
void io_hlt(void);

// 执行 CLI 指令，清除 EFLAGS 寄存器的 IF 位（bit9），禁止 CPU 响应可屏蔽中断
void io_cli(void);

// 执行 STI 指令，设置 EFLAGS 寄存器的 IF 位（bit9），允许 CPU 响应可屏蔽中断
void io_sti(void);

// 先执行 STI 开中断，再立即执行 HLT 待机（常用于主循环空闲时，可响应中断又省电）
void io_stihlt(void);

// 从指定 I/O 端口读取 8 位（1 字节）数据
//   port: I/O 端口地址（0x0000~0xFFFF）
//   返回: 读取到的 8 位数据（零扩展到 32 位）
int io_in8(int port);

// 从指定 I/O 端口读取 16 位（2 字节）数据
int io_in16(int port);

// 从指定 I/O 端口读取 32 位（4 字节）数据
int io_in32(int port);

// 向指定 I/O 端口写入 8 位（1 字节）数据
//   port: I/O 端口地址（0x0000~0xFFFF）
//   data: 要写入的 8 位数据
void io_out8(int port, int data);

// 向指定 I/O 端口写入 16 位（2 字节）数据
//   port: I/O 端口地址（0x0000~0xFFFF）
//   data: 要写入的 16 位数据
void io_out16(int port, int data);

// 向指定 I/O 端口写入 32 位（4 字节）数据
//   port: I/O 端口地址（0x0000~0xFFFF）
//   data: 要写入的 32 位数据
void io_out32(int port, int data);

// 读取 EFLAGS 寄存器值（bit9=IF 中断许可标志最常用）
int io_load_eflags(void);

// 将值写入 EFLAGS 寄存器（与 io_load_eflags 配对使用，恢复之前保存的 CPU 状态）
//   eflags: 要写入 EFLAGS 的 32 位值（通常由 io_load_eflags 返回）
void io_store_eflags(int eflags);

// 执行 LGDT 指令，加载全局描述符表寄存器 GDTR，告诉 CPU GDT 的位置和大小
//   limit: GDT 界限（总字节数 - 1）
//   addr: GDT 基地址（0x00270000）
void load_gdtr(int limit, int addr);

// 执行 LIDT 指令，加载中断描述符表寄存器 IDTR，告诉 CPU IDT 的位置和大小
//   limit: IDT 界限（总字节数 - 1）
//   addr: IDT 基地址（0x0026f800）
void load_idtr(int limit, int addr);

// 中断处理汇编入口 — 实现在 naskfunc.nas 中
// 这些是汇编语言的中断处理 wrapper，负责保存/恢复寄存器现场，
// 然后调用对应的 C 语言处理函数（inthandler21/inthandler27/inthandler2c）
void asm_inthandler21(void);
void asm_inthandler27(void);
void asm_inthandler2c(void);

/* ============================================================
 * 图形处理函数 — 实现在 graphic.c 中
 * ============================================================ */

// 初始化前 16 色的 VGA 调色板
void init_palette(void);

// 向 VGA 调色板写入 RGB 数据（start: 起始色号, end: 结束色号, rgb: RGB 数组）
void set_palette(int start, int end, unsigned char *rgb);

// 在显存中填充一个纯色矩形（坐标均为包含，即左上和右下像素都会被填充）
//   vram: 显存基址, xsize: 屏幕一行像素数（用于计算 vram[y*xsize+x] 偏移）
//   c: 颜色编号（0~15，对应调色板索引）, x0,y0: 矩形左上角坐标, x1,y1: 矩形右下角坐标
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);

// 绘制桌面背景（浅蓝底色）+ 底部任务栏 + 左右立体按钮
//   vram: 显存基址, x: 屏幕宽度（像素）, y: 屏幕高度（像素）
void init_screen8(char *vram, int x, int y);

// 在屏幕上绘制一个 8×16 的等宽字符（逐位检查字节点阵进行画点）
//   vram: 显存基址, xsize: 屏幕一行像素数
//   x,y: 字符左上角坐标, c: 颜色编号, font: 16 字节点阵数据（每字节控制一行 8 列）
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);

// 绘制以 0x00 结尾的 ASCII 字符串（自动从 hankaku 字库中取每个字符的点阵）
//   vram: 显存基址, xsize: 屏幕一行像素数
//   x,y: 字符串左上角坐标, c: 颜色编号, s: 字符串指针（以 0x00 结尾）
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);

// 生成 16×16 鼠标指针图案到缓冲区（用字符模板填充颜色号，'*'=黑, 'O'=白, '.'=背景）
//   mouse: 输出缓冲区（16×16=256 字节，调用者提供空间）
//   bc: 背景色号（与桌面底色相同，画到屏幕上时看起来是透明的）
void init_mouse_cursor8(char *mouse, char bc);

// 将矩型图块（如鼠标光标）从缓冲区拷贝到显存指定位置
//   vram: 显存基址, vxsize: 屏幕一行像素数（用于计算显存寻址偏移）
//   pxsize,pysize: 图块宽度/高度（像素）, px0,py0: 图块在屏幕上的目标左上角坐标
//   buf: 图案缓冲区（一维数组）, bxsize: 缓冲区一行像素数（通常与 pxsize 相同）
void putblock8_8(char *vram, int vxsize, int pxsize,
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

//整个段描述符占 8 字节共64位，包含了段界限（20位）、基地址（base共32位）和访问权限等信息（8位），标志（4位）。
//段界限可以是字节单位（G=0）或 4KB 单位（G=1），基地址指定段的起始地址，访问权限定义了段的类型和特权级别。
//分段是为了兼容8086时代的实模式程序，保护模式下虽然可以使用分页机制，但分段机制仍然存在并且必须正确设置，否则 CPU 无法访问内存。
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

// 初始化 GDT 和 IDT：管理段描述符(64KB)和中断门描述符(2KB)
// 必须最先调用，否则保护模式下 CPU 无法访问内存和响应中断
void init_gdtidt(void);

// 填写一个段描述符（8 字节），将 32 位基址拆成 16+8+8、20 位界限拆成 16+4 填入硬件格式
//   sd: 目标段描述符指针, limit: 段界限, base: 段基址（32位）, ar: 访问权+标志
// 示例: set_segmdesc(gdt+1, 0xffffffff, 0x00000000, 0x4092);
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar);

// 填写一个门描述符（8 字节），将 32 位处理函数地址拆成低 16 位 + 高 16 位填入 IDT
//   gd: 目标门描述符指针, offset: 中断处理函数 32 位地址
//   selector: 代码段选择子（通常 GDT[2]=16）, ar: 访问权（0x8E=内核中断门）
// 示例: set_gatedesc(idt+0x21, handler_addr, 2*8, 0x8E);
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);

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
#define AR_INTGATE32		0x008e		// 中断门 ar：P=1 DPL=0 S=0 TYPE=1110(32位中断门)

/* ============================================================
 * PIC（可编程中断控制器）初始化函数和端口宏
 *
 * 主 PIC（PIC0）管理 IRQ0~IRQ7，端口 0x20~0x21
 * 从 PIC（PIC1）管理 IRQ8~IRQ15，端口 0xA0~0xA1
 *
 * 中断向量映射（PIC ICW2 设定）：
 *   主 PIC IRQ0~IRQ7 → IDT 0x20~0x27
 *   从 PIC IRQ8~IRQ15 → IDT 0x28~0x2F
 *   为什么从 0x20 开始？因为 IDT 0x00~0x1F 已被 CPU 异常占满
 *   （除零、调试、GPF 等 32 个异常向量），硬件中断必须避开这片区域
 *
 * 常见设备 IRQ 编号：
 *   IRQ0（0x20）= PIT 定时器
 *   IRQ1（0x21）= 键盘
 *   IRQ12（0x2C）= 鼠标
 *
 * ICW（Initialization Command Word）：初始化控制数据，有4个，每个1字节，配置 PIC 的工作模式和连接方式
 * OCW（Operation Command Word）：操作命令字
 * IMR（Interrupt Mask Register）：中断屏蔽寄存器,8位，每位对应一个 IRQ（连接PIC和CPU的连接线），1=屏蔽（禁止），0=允许
 * ============================================================ */
// 初始化主从 PIC（8259A），映射 IRQ0~IRQ7 → IDT 0x20~0x27, IRQ8~IRQ15 → IDT 0x28~0x2F
void init_pic(void);
void inthandler21(int *esp);
void inthandler27(int *esp);
void inthandler2c(int *esp);
#define PIC0_ICW1		0x0020		// 主 PIC ICW1（边沿触发、级联、需要 ICW4）
#define PIC0_OCW2		0x0020		// 主 PIC OCW2（发送 EOI 通知中断结束）
#define PIC0_IMR		0x0021		// 主 PIC IMR（屏蔽/启用中断）
#define PIC0_ICW2		0x0021		// 主 PIC ICW2（IRQ 基向量号）
#define PIC0_ICW3		0x0021		// 主 PIC ICW3（级联连接，bit2 接从 PIC）
#define PIC0_ICW4		0x0021		// 主 PIC ICW4（8086 模式，自动 EOI）
#define PIC1_ICW1		0x00a0		// 从 PIC ICW1
#define PIC1_OCW2		0x00a0		// 从 PIC OCW2
#define PIC1_IMR		0x00a1		// 从 PIC IMR
#define PIC1_ICW2		0x00a1		// 从 PIC ICW2
#define PIC1_ICW3		0x00a1		// 从 PIC ICW3（级联标识，接主 PIC 的 IRQ2）
#define PIC1_ICW4		0x00a1		// 从 PIC ICW4