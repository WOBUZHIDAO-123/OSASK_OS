# IDT 中断执行历程详解

## 一、整体架构

```
┌─────────────────────────────────────────────────────────┐
│                       应用程序                            │
│                 HariMain (bootpack.c)                    │
│                    for(;;) { io_hlt(); }                │
└─────────────────────┬───────────────────────────────────┘
                      │  键盘按下 → IRQ1
                      ▼
┌─────────────────────────────────────────────────────────┐
│                     PIC (int.c)                          │
│            IRQ1 → 映射为 INT 0x21                       │
└─────────────────────┬───────────────────────────────────┘
                      │  通知 CPU
                      ▼
┌─────────────────────────────────────────────────────────┐
│                   IDT (dsctbl.c)                         │
│           查 IDT[0x21] 门描述符                          │
│           读出: 函数地址 + 段选择子 + 权限              │
└─────────────────────┬───────────────────────────────────┘
                      │  跳转
                      ▼
┌─────────────────────────────────────────────────────────┐
│           汇编 Wrapper (naskfunc.nas)                    │
│           _asm_inthandler21                              │
│       PUSH 保存现场 → CALL C 函数                       │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│           C 中断处理函数 (int.c)                         │
│           inthandler21(int *esp)                        │
│       画红色矩形 → 显示 "INT 21 ..." → 死循环          │
└─────────────────────────────────────────────────────────┘
```

---

## 二、逐层详解

### 1. GDT 初始化（`dsctbl.c : init_gdtidt`）

```c
// GDT[1]: 4GB 可读写数据段（让 CPU 能访问全部内存）
set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, 0x4092);
//         ↑索引1   ↑大小4GB   ↑基址0    ↑数据段可读写

// GDT[2]: 512KB 可执行代码段（让 CPU 能执行 bootpack 代码）
set_segmdesc(gdt + 2, 0x0007ffff, 0x00280000, 0x409a);
//         ↑索引2   ↑大小512KB ↑基址0x280000 ↑代码段可执行
```

**GDT == "区域许可名单"**

| 索引 | 范围 | 可做什么 |
|------|------|---------|
| GDT[0] | 无效（CPU 强制） | 不可用 |
| GDT[1] | `0x00000000 ~ 0xFFFFFFFF`（4GB） | 读写（数据段） |
| GDT[2] | `0x00280000 ~ 0x0027FFFF`（512KB） | 执行（代码段） |

`load_gdtr(0xffff, 0x00270000)` 告诉 CPU：**GDT 在内存地址 0x270000，大小 64KB。**

### 2. IDT 初始化（`dsctbl.c : init_gdtidt`）

```c
// 清零所有 256 个门描述符
for (i = 0; i < 256; i++) {
    set_gatedesc(idt + i, 0, 0, 0);
}

// 注册 3 个中断处理程序
set_gatedesc(idt + 0x21, (int) asm_inthandler21, 2 * 8, AR_INTGATE32);
set_gatedesc(idt + 0x27, (int) asm_inthandler27, 2 * 8, AR_INTGATE32);
set_gatedesc(idt + 0x2c, (int) asm_inthandler2c, 2 * 8, AR_INTGATE32);
```

`load_idtr(0x7ff, 0x0026f800)` 告诉 CPU：**IDT 在内存地址 0x26f800，大小 2KB。**

### 3. 门描述符的组成（`dsctbl.c : set_gatedesc`）

每个门描述符 8 字节：

```
┌───────┬────────┬───────┬──────────────┬────────┐
│offset_low│selector│dw_count│access_right│offset_high│
│ 16位     │ 16位   │ 8位    │ 8位         │ 16位      │
└───────┴────────┴───────┴──────────────┴────────┘
```

以键盘中断的注册为例：

```c
set_gatedesc(idt + 0x21,    // 写入 IDT[33] 号槽位
    (int) asm_inthandler21,  // 函数地址（32位）
    2 * 8,                   // 段选择子 = GDT[2]
    AR_INTGATE32);           // 权限 = 0x8E
```

`set_gatedesc` 内部将 32 位函数地址拆成低 16 位和高 16 位：

```c
gd->offset_low   = offset & 0xffff;          // 地址低 16 位
gd->selector     = selector;                  // 段选择子（2*8=16）
gd->dw_count     = (ar >> 8) & 0xff;          // 中断门不用，填 0
gd->access_right = ar & 0xff;                 // 0x8E
gd->offset_high  = (offset >> 16) & 0xffff;   // 地址高 16 位
```

---

### 4. PIC（可编程中断控制器）初始化（`int.c : init_pic`）

| 寄存器 | 端口 | 值 | 含义 |
|--------|------|----|------|
| ICW1 | 0x20 / 0xA0 | 0x11 | 边沿触发 + 级联 + 需要 ICW4 |
| ICW2 | 0x21 / 0xA1 | 0x20 / 0x28 | **IRQ0→INT 0x20, IRQ8→INT 0x28** |
| ICW3 | 0x21 / 0xA1 | 0x04 / 0x02 | 主片 IRQ2 接从片 |
| ICW4 | 0x21 / 0xA1 | 0x01 | 8086 模式 |
| IMR | 0x21 / 0xA1 | 0xFB / 0xFF | 初始全屏蔽 |

**PIC 的作用**：把硬件 IRQ 映射到 IDT 中断号。

```
IRQ0  →  INT 0x20  (PIT 定时器)
IRQ1  →  INT 0x21  (键盘)       ← 我们注册了
IRQ2  →  INT 0x22  (级联从片)
IRQ7  →  INT 0x27  (伪中断)     ← 我们注册了
IRQ12 →  INT 0x2C  (鼠标)       ← 我们注册了
```

### 5. 主循环中开中断（`bootpack.c : HariMain`）

```c
init_gdtidt();                  // 设置 GDT + IDT
init_pic();                     // 设置 PIC
io_sti();                       // STI 指令——CPU 开始响应中断

// ... 画桌面、鼠标 ...

io_out8(PIC0_IMR, 0xf9);       // 11111001 → 允许键盘 + PIC1
io_out8(PIC1_IMR, 0xef);       // 11101111 → 允许鼠标

for (;;) {
    io_hlt();                   // HLT 待机，等中断唤醒
}
```

---

### 6. 中断发生时——CPU 硬件自动完成

当键盘按下，以下是 CPU **硬件自动** 完成的操作：

```
① PIC 收到 IRQ1
② PIC 查 ICW2 映射表：IRQ1 → INT 0x21
③ PIC 通过 INTR 引脚通知 CPU
④ CPU 执行完当前指令（HLT）后检查到中断请求
⑤ CPU 查 IDTR 寄存器找到 IDT 基址 0x26F800
⑥ CPU 计算 IDT[0x21] 地址 = 0x26F800 + 0x21 × 8
⑦ CPU 读出 8 字节门描述符
⑧ CPU 检查门描述符的 P 位（存在位=1，有效）
⑨ CPU 检查段选择子 2×8 → 查 GDT[2] 找到代码段基址 0x280000
⑩ CPU 从门描述符拼出函数地址（offset_low + offset_high）
⑪ CPU 将 EFLAGS、CS、EIP 压栈（保存返回点）
⑫ CPU 跳转到 _asm_inthandler21
```

### 7. 汇编 Wrapper（`naskfunc.nas : _asm_inthandler21`）

```asm
_asm_inthandler21:
    PUSH    ES                    ; 保存段寄存器 ES
    PUSH    DS                    ; 保存段寄存器 DS
    PUSHAD                        ; 保存所有通用寄存器 (EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI)
    MOV     EAX,ESP
    PUSH    EAX                   ; 传栈指针给 C 函数 (参数: int *esp)
    MOV     AX,SS
    MOV     DS,AX                 ; 统一 DS=SS（C 函数能正确访问栈上数据）
    MOV     ES,AX                 ; 统一 ES=SS
    CALL    _inthandler21         ; 调用 C 函数 (int.c)
    POP     EAX                   ; 清理栈上的参数
    POPAD                         ; 恢复所有通用寄存器
    POP     DS                    ; 恢复 DS
    POP     ES                    ; 恢复 ES
    IRETD                         ; 中断返回（恢复 EFLAGS, CS, EIP）
```

**为什么需要这个汇编包装层？**

| 问题 | 解决 |
|------|------|
| C 函数会修改寄存器值 | `PUSHAD` / `POPAD` 保存和恢复 |
| C 函数需要栈指针参数 | `MOV EAX,ESP` / `PUSH EAX` 传参 |
| C 函数假设 DS=SS | `MOV AX,SS / MOV DS,AX` 统一段寄存器 |
| 中断返回需要特殊指令 | `IRETD` 而非 `RET` |

### 8. C 中断处理函数（`int.c : inthandler21`）

```c
void inthandler21(int *esp)
{
    struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;

    // 画红色背景
    boxfill8(binfo->vram, binfo->scrnx, COL8_FF0000, 0, 0, 32 * 8 - 1, 15);

    // 显示 "INT 21 (IRQ-1) : PS/2 keyboard"
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF,
                  "INT 21 (IRQ-1) : PS/2 keyboard");

    for (;;) {
        io_hlt();   // 死循环（当前版本演示用，不返回）
    }
}
```

---

## 三、完整流程时间线

```
时间 →
─────── 启动阶段 ───────
1. HariMain 调用 init_gdtidt()     → 在内存 0x270000 创建 GDT
                                    在内存 0x26F800 创建 IDT
2. HariMain 调用 init_pic()        → 设置 PIC，IRQ1→INT 0x21
3. HariMain 调用 io_sti()          → CPU 开中断

─────── 正常运行 ───────
4. HariMain 进入 for(;;) { io_hlt() }  → CPU 待机

─────── 键盘按下 ───────
5. 键盘硬件产生 IRQ1               → PIC 收到信号
6. PIC 查表 IRQ1→INT 0x21         → 通知 CPU
7. CPU 查 IDTR → 找到 IDT          → 找到 IDT[0x21]
8. CPU 读出 门描述符               → 函数地址 + GDT[2] + 权限
9. CPU 查 GDT[2] → 找到代码段      → 基址 0x280000
10.CPU 跳转到 _asm_inthandler21    → 位于 naskfunc.nas
11._asm_inthandler21 保存现场      → 调 inthandler21
12.inthandler21 画红色背景+文字    → 死循环
```

---

## 四、关键常量表

| 常量 | 值 | 用途 |
|------|----|------|
| `ADR_GDT` | `0x00270000` | GDT 基地址 |
| `LIMIT_GDT` | `0x0000ffff` | GDT 界限（64KB） |
| `ADR_IDT` | `0x0026f800` | IDT 基地址 |
| `LIMIT_IDT` | `0x000007ff` | IDT 界限（2KB） |
| `ADR_BOTPAK` | `0x00280000` | bootpack 加载地址 |
| `LIMIT_BOTPAK` | `0x0007ffff` | bootpack 段界限（512KB） |
| `AR_DATA32_RW` | `0x4092` | 32位数据段，可读写 |
| `AR_CODE32_ER` | `0x409a` | 32位代码段，可执行可读 |
| `AR_INTGATE32` | `0x008e` | 32位中断门 |
| `PIC0_ICW2` | `0x21` | 主片 IRQ0→INT 0x20 |
| `PIC1_ICW2` | `0xA1` | 从片 IRQ8→INT 0x28 |

---

## 五、文件与函数对应表

| 文件 | 函数 | 用途 |
|------|------|------|
| `dsctbl.c` | `init_gdtidt()` | 初始化 GDT + IDT，注册中断 |
| `dsctbl.c` | `set_segmdesc()` | 填写段描述符 |
| `dsctbl.c` | `set_gatedesc()` | 填写门描述符 |
| `int.c` | `init_pic()` | 初始化 PIC |
| `int.c` | `inthandler21()` | 键盘中断处理 C 函数 |
| `int.c` | `inthandler27()` | 伪中断处理 C 函数 |
| `int.c` | `inthandler2c()` | 鼠标中断处理 C 函数 |
| `naskfunc.nas` | `_asm_inthandler21()` | 键盘中断汇编入口（保存现场→调 C） |
| `naskfunc.nas` | `_asm_inthandler27()` | 伪中断汇编入口 |
| `naskfunc.nas` | `_asm_inthandler2c()` | 鼠标中断汇编入口 |
| `naskfunc.nas` | `load_gdtr()` / `load_idtr()` | 加载 GDTR / IDTR 寄存器 |
| `bootpack.c` | `HariMain()` | 主函数，调用初始化并进入主循环 |
