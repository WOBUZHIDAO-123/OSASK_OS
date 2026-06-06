/* GDT 和 IDT 等描述符表相关函数 */

// ----------------------------------------------------------
// 段描述符结构体（8 字节）—— 对应 CPU 硬件定义的格式
// 内存布局（从低地址到高地址）：
//  字节0-1: limit_low    — 段界限低 16 位
//  字节2-3: base_low     — 基地址低 16 位
//  字节4:   base_mid     — 基地址中 8 位
//  字节5:   access_right — 访问权（P_DPL_S_TYPE 等标志）
//  字节6:   limit_high   — 高 4 位段界限 + 低 4 位标志（G/D/B/L/AVL）
//  字节7:   base_high    — 基地址高 8 位
// 最终基地址 = base_low | (base_mid << 16) | (base_high << 24)
// 最终段界限 = limit_low | ((limit_high & 0x0f) << 16)，配合 G 位决定单位
// ----------------------------------------------------------
struct SEGMENT_DESCRIPTOR {
	short limit_low, base_low;
	char base_mid, access_right;
	char limit_high, base_high;
};

// ----------------------------------------------------------
// 门描述符结构体（8 字节）—— 对应 CPU 硬件定义的中断门/陷阱门格式
// 内存布局（从低地址到高地址）：
//  字节0-1: offset_low   — 处理程序地址低 16 位
//  字节2-3: selector     — 目标代码段选择子（告诉 CPU 用哪个段执行中断处理）
//  字节4:   dw_count     — 调用门专用：参数复制个数（中断门填 0）
//  字节5:   access_right — 访问权（存在位、DPL、类型等）
//  字节6-7: offset_high  — 处理程序地址高 16 位
// 最终处理程序地址 = offset_low | (offset_high << 16)
// ----------------------------------------------------------
struct GATE_DESCRIPTOR {
	short offset_low, selector;
	char dw_count, access_right;
	short offset_high;
};

void init_gdtidt(void);
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar);
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);

// ============================================================
// init_gdtidt — 初始化 GDT 和 IDT
//
// 职责：
//   ① 将 GDT 的 8192 个槽位全部清零
//   ② 设置 3 个有实际意义的段描述符
//   ③ 加载 GDTR 寄存器，告诉 CPU GDT 在哪
//   ④ 将 IDT 的 256 个槽位全部清零
//   ⑤ 加载 IDTR 寄存器，告诉 CPU IDT 在哪
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
// GDT 槽位分配：
//   [0] 空描述符（CPU 规定第 0 项必须为空，否则异常）
//   [1] 系统整体段：基址 0x00000000，大小 4GB，可读写
//   [2] 内核代码段：基址 0x00280000（bootpack 加载处），大小 512KB，可执行
//
// 内存布局：
//   0x00270000  ← GDT 起始（最多 8192 个段描述符 × 8 字节 = 64 KB）
//   0x0026f800  ← IDT 起始（最多 256 个门描述符 × 8 字节 = 2 KB）
// ============================================================
void init_gdtidt(void)
{
	struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *) 0x00270000;
	struct GATE_DESCRIPTOR    *idt = (struct GATE_DESCRIPTOR    *) 0x0026f800;
	int i;

	// GDT 初始化
	for (i = 0; i < 8192; i++) {
		// 8192 个段描述符槽位,8192个槽位是因为段选择子占 16 位，其中 3 位是 RPL，1 位是 TI，剩下 12 位索引，
		// 所以最大索引为 2^12=4096，每个索引占 8 字节，所以 GDT 最大为 4096*8=32768 字节，即 32 KB，但为了对齐和预留空间，通常分配 64 KB（8192 个槽位）。
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

	// IDT 初始化
	for (i = 0; i < 256; i++) {
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
//
// ar（Access Rights）编码说明（16 位）：
//   bit15  : G（粒度）：0=字节, 1=4KB
//   bit14  : D/B（默认操作大小）：0=16位, 1=32位
//   bit13  : L（64位模式）：IA-32 下为 0
//   bit12  : AVL（软件可用位）：忽略
//   bit7   : P（存在位）：1=有效
//   bit6-5 : DPL（描述符特权级）：0=ring0(内核), 3=ring3(用户)
//   bit4   : S（系统/代码数据）：1=代码或数据段, 0=系统段
//   bit3   : 代码/数据段内：E(可执行)/C(一致)/R(可读)/A(已访问)
//            or 数据段内：ED(扩展方向)/W(可写)/A(已访问)
//   bit2-0 : TYPE（段类型）
//
// 常用 ar 值：
//   0x4092 = G=1(4KB粒度) D=1(32位) P=1 DPL=0 S=1(数据段) W=1(可写)
//   0x409a = G=1 D=1 P=1 DPL=0 S=1(代码段) E=1(可执行) R=1(可读)
// ============================================================
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar)
{
	if (limit > 0xfffff) {
		ar |= 0x8000; /* G_bit = 1 */
		limit /= 0x1000;
	}
	sd->limit_low    = limit & 0xffff;          // 段界限低 16 位
	sd->base_low     = base & 0xffff;            // 基地址低 16 位
	sd->base_mid     = (base >> 16) & 0xff;      // 基地址中 8 位
	sd->access_right = ar & 0xff;                // access_right（P_DPL_S_TYPE）
	sd->limit_high   = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0); // 高 4 位段界限 + 标志(G/D/B/L/AVL)
	sd->base_high    = (base >> 24) & 0xff;      // 基地址高 8 位
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
	gd->offset_low   = offset & 0xffff;          // 处理程序地址低 16 位
	gd->selector     = selector;                  // 目标代码段选择子
	gd->dw_count     = (ar >> 8) & 0xff;          // 调用门参数计数（中断门填 0）
	gd->access_right = ar & 0xff;                 // 访问权（P_DPL_TYPE）
	gd->offset_high  = (offset >> 16) & 0xffff;   // 处理程序地址高 16 位
	return;
}
