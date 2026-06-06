; hello-os
; TAB=4

		CYLS	EQU		10				; 声明CYLS=10

		ORG		0x7c00			; 指明程序装载地址

; 标准FAT12格式软盘专用的代码
; Stand FAT12 format floppy code
		JMP		entry
		NOP					; 这两个字节是标准的跳转填充
		DB		"HELLOIPL"		; 启动扇区名称（8字节）(OEM Name, 8 bytes)
		DW		512				; 每个扇区（sector）大小（必须512字节）(Bytes per Sector)
		DB		1				; 簇（cluster）大小（必须为1个扇区）(Sectors per Cluster)
		DW		1				; FAT起始位置（一般为第一个扇区）(Reserved Sectors)
		DB		2				; FAT个数（必须为2）(Number of FATS)
		DW		224				; 根目录大小（一般为224项）(Root Entries)
		DW		2880			; 该磁盘大小（必须为2880扇区1440*1024/512）(Total Sectors)
		DB		0xf0			; 磁盘类型（必须为0xf0）(Media Descriptor)
		DW		9				; FAT的长度（必须9扇区）(Sectors per FAT)
		DW		18				; 一个磁道（track）有几个扇区（必须为18）(Sectors per Track)
		DW		2				; 磁头数（必须2）(Number of Heads)
		DD		0				; 不使用分区，必须是0(Hidden Sectors)
		DD		2880			; 重写一次磁盘大小(Large Sectors)

		; 扩展BPB部分 (Extended BPB)
		DB		0x00			; 驱动器号 (Drive Number)
		DB		0x00			; 保留 (Reserved)
		DB		0x29			; 扩展引导签名 (Extended Boot Signature)
		DD		0x12345678		; 卷序列号 (Volume Serial Number)
		DB		"HELLO-OS   "	; 磁盘的名称（必须为11字节，不足填空格）(Volume Label, 11 bytes)
		DB		"FAT12   "		; 磁盘格式名称（必须为8字节，不足填空格）(File System Type, 8 bytes)
		; 注意：这里没有 RESB 18 了！

; 程序主体
entry:
		MOV		AX,0			; 初始化寄存器
		MOV		SS,AX
		MOV		SP,0x7c00
		MOV		DS,AX
		MOV		ES,AX

;新增内容
error:
		MOV		SI,msg

	;读取扇区
		MOV 	AX,0x0820
		MOV 	ES,AX
		MOV 	CH,0			; 柱面0
		MOV 	DH,0			; 磁头0
		MOV 	CL,2			; 扇区2

readLoop:
		MOV 	SI,0			; 记录失败次数的寄存器

retry:
		MOV 	AH,0x02		; AH=0x02 ： 读盘
		MOV 	AL,1			; 1个扇区
		MOV 	BX,0
		MOV 	DL,0x00		; A驱动器
		INT		0x13			; 调用磁盘BIOS提供的关于磁盘的函数，成功则进位标记CF返回0
		JNC		next			; JNC条件跳转，当进位标记CF为0时跳转
		ADD		SI,1			; SI加1
		CMP		SI,5			; 比较SI和5
		JAE		error			; JAE条件跳转，对比结果大于等于时跳转
		MOV 	AH,0x00		
		MOV 	DL,0x00		; A驱动器
		INT		0x13			; 重置驱动器
		JMP		retry

next:
		MOV 	AX,ES
		ADD		AX,0x0020
		MOV		ES,AX
		ADD		CL,1
		CMP		CL,18			; 比较CL和18
		JBE		readLoop		; JBE条件跳转，对比结果小于等于时跳转
		MOV		CL,1
		ADD		DH,1
		CMP		DH,2
		JB		readLoop		; JB条件跳转，对比结果小于时跳转	
		MOV		DH,0
		ADD		CH,1
		CMP		CH,CYLS
		JB		readLoop		
; 读取完毕，跳转到haribote.sys执行！
		MOV		[0x0ff0],CH		; 记录读取进度
		JMP		0xc200			; 关键！成功读取后跳转到0xc200执行内核

;新增内容结尾

putloop:
		MOV		AL,[SI]
		ADD		SI,1			; 给SI加1
		CMP		AL,0
		JE		fin
		MOV		AH,0x0e			; 显示一个文字
		MOV		BX,15			; 指定字符颜色
		INT		0x10			; 调用显卡BIOS
		JMP		putloop
fin:
		HLT						; 让CPU停止，等待指令
		JMP		fin				; 无限循环

msg:
		DB		0x0a, 0x0a		; 换行两次
		DB		"error"
		DB		0x0a			; 换行
		DB		0

;结尾填充
		RESB	0x7dfe-$		; 填写0x00直到0x001fe

		DB		0x55, 0xaa