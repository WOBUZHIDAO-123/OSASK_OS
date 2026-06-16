# ============================================================
# Makefile — haribote 操作系统构建脚本（第5天）
# 定义工具链路径、编译规则和便捷命令
# ============================================================

OBJS_BOOTPACK = bootpack.obj naskfunc.obj hankaku.obj graphic.obj dsctbl.obj \
		int.obj fifo.obj


# ---------- 工具链路径 ----------
TOOLPATH = ../z_tools/
INCPATH  = ../z_tools/haribote/

# ---------- 工具宏定义（简化命令行调用） ----------
MAKE     = $(TOOLPATH)make.exe -r     # make 工具自身
NASK     = $(TOOLPATH)nask.exe        # 汇编器（NASM 风格）
CC1      = $(TOOLPATH)cc1.exe -I$(INCPATH) -Os -Wall -quiet  # C 编译器
GAS2NASK = $(TOOLPATH)gas2nask.exe -a # GNU 汇编 → NASM 格式转换器
OBJ2BIM  = $(TOOLPATH)obj2bim.exe     # 目标文件 → BIM 链接器
# ═══════════════════════════════════════════════════════════════
# 【新增】字库处理工具（第5天加入）
# MAKEFONT — 将 hankaku.txt 点阵文本编译为二进制字体文件
# BIN2OBJ  — 将二进制字体文件转换为 .obj 格式，导出符号 _hankaku
# ═══════════════════════════════════════════════════════════════
MAKEFONT = $(TOOLPATH)makefont.exe    # 字库点阵 → 二进制字体文件
BIN2OBJ  = $(TOOLPATH)bin2obj.exe     # 二进制文件 → 目标文件(.obj)
BIM2HRB  = $(TOOLPATH)bim2hrb.exe     # BIM → HRB 格式转换器
RULEFILE = $(TOOLPATH)haribote/haribote.rul  # 链接规则文件
EDIMG    = $(TOOLPATH)edimg.exe        # 磁盘映像制作工具
IMGTOL   = $(TOOLPATH)imgtol.com       # 映像写入软盘工具
COPY     = copy                        # Windows 文件复制命令
DEL      = del                         # Windows 文件删除命令

# ============================================================
# 默认目标
# ============================================================

default :
	$(MAKE) img

# ============================================================
# 文件生成规则
# ============================================================

# 汇编 IPL（引导扇区）→ 生成 512 字节二进制文件
ipl10.bin : ipl10.nas Makefile
	$(NASK) ipl10.nas ipl10.bin ipl10.lst

# 汇编 asmhead（保护模式切换等初始化代码）→ 生成二进制文件
asmhead.bin : asmhead.nas Makefile
	$(NASK) asmhead.nas asmhead.bin asmhead.lst

# 手写汇编函数（naskfunc.nas）→ 目标文件 .obj
naskfunc.obj : naskfunc.nas Makefile
	$(NASK) naskfunc.nas naskfunc.obj naskfunc.lst

# 通用编译规则（模式规则，自动匹配所有 .c → .gas → .nas → .obj）
%.gas : %.c Makefile
	$(CC1) -o $*.gas $*.c

%.nas : %.gas Makefile
	$(GAS2NASK) $*.gas $*.nas

%.obj : %.nas Makefile
	$(NASK) $*.nas $*.obj $*.lst

# ═══════════════════════════════════════════════════════════════
# 【新增】字库生成规则（第5天加入）
# hankaku.txt 包含 256 个字符的 8×16 点阵描述
# → 先转为二进制字库 → 再转为 .obj 供 C 代码 extern 引用
# ═══════════════════════════════════════════════════════════════
hankaku.bin : hankaku.txt Makefile
	$(MAKEFONT) hankaku.txt hankaku.bin

hankaku.obj : hankaku.bin Makefile
	$(BIN2OBJ) hankaku.bin hankaku.obj _hankaku

# ═══════════════════════════════════════════════════════════════
# 【修改】链接规则（第5天加入 hankaku.obj）
# 之前只有 bootpack.obj + naskfunc.obj，缺少 hankaku.obj
# 导致链接报错 "can't link _hankaku"
# 【第6天追加】加入 graphic.obj（图形绘制函数）和 dsctbl.obj（GDT/IDT 表初始化）
# ═══════════════════════════════════════════════════════════════
# 链接所有 .obj → bootpack.bim（使用 OBJS_BOOTPACK 宏避免重复）
bootpack.bim : $(OBJS_BOOTPACK) Makefile
	$(OBJ2BIM) @$(RULEFILE) out:bootpack.bim stack:3136k map:bootpack.map \
		$(OBJS_BOOTPACK)

# BIM → HRB 格式（haribote 专用，加载基址偏移 0）
bootpack.hrb : bootpack.bim Makefile
	$(BIM2HRB) bootpack.bim bootpack.hrb 0

# 合并 asmhead.bin + bootpack.hrb → 系统文件（二进制拼接）
haribote.sys : asmhead.bin bootpack.hrb Makefile
	copy /B asmhead.bin+bootpack.hrb haribote.sys

# 制作 FAT12 软盘映像：写入引导扇区 + 复制系统文件
haribote.img : ipl10.bin haribote.sys Makefile
	$(EDIMG)   imgin:../z_tools/fdimg0at.tek \
		wbinimg src:ipl10.bin len:512 from:0 to:0 \
		copy from:haribote.sys to:@: \
		imgout:haribote.img

# ============================================================
# 便捷命令
# ============================================================

# 生成磁盘映像
img :
	$(MAKE) haribote.img

# 生成映像 → 复制到 QEMU → 启动虚拟机
run :
	$(MAKE) img
	$(COPY) haribote.img ..\z_tools\qemu\fdimage0.bin
	$(MAKE) -C ../z_tools/qemu

# 生成映像 → 写入真实软盘（需软驱 A:）
install :
	$(MAKE) img
	$(IMGTOL) w a: haribote.img

# 清理编译中间产物（保留 haribote.img）
clean :
	-$(DEL) *.bin
	-$(DEL) *.lst
	-$(DEL) *.gas
	-$(DEL) *.obj
	-$(DEL) bootpack.nas graphic.nas dsctbl.nas int.nas
	-$(DEL) bootpack.map
	-$(DEL) bootpack.bim
	-$(DEL) bootpack.hrb
	-$(DEL) haribote.sys

# 全清理（包括最终映像）
src_only :
	$(MAKE) clean
	-$(DEL) haribote.img