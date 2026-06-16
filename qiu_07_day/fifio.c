/* ============================================================
 * fifio.c — 通用环形缓冲区（FIFO）操作函数
 *
 * 提供 FIFO8 环形缓冲区的初始化、写入和读取操作。
 * 相比 KEYBUF（固定 32 字节、硬编码），FIFO8 更加通用：
 *   - 缓冲区大小由调用者指定
 *   - 缓冲区内存由调用者分配并传入指针
 *   - 通过 free 成员判断满/空，高效 O(1)
 * ============================================================ */

#include "bootpack.h"

// FIFO 溢出标志：当缓冲区已满时仍有数据写入，设置此标志
#define FLAGS_OVERRUN 0x0001

// ============================================================
// fifo8_init — 初始化 FIFO8 环形缓冲区
//
// 功能：设置缓冲区参数，将读写指针和溢出标志归零
//
// 参数：
//   fifo — 指向要初始化的 FIFO8 结构体
//   size — 缓冲区大小（字节数）
//   buf  — 由调用者分配的外部缓冲区数组，长度至少 size 字节
//
// 使用示例：
//   unsigned char buf[256];
//   struct FIFO8 fifo;
//   fifo8_init(&fifo, 256, buf);
// ============================================================
void fifo8_init(struct FIFO8 *fifo, int size, unsigned char *buf)
{
	fifo->size = size;      // 记录缓冲区总大小
	fifo->buf = buf;        // 绑定外部缓冲区
	fifo->free = size;      // 初始时全部为空闲（free == size 表示空）
	fifo->flags = 0;        // 清零溢出标志
	fifo->p = 0;            // 写入位置从 0 开始
	fifo->q = 0;            // 读取位置从 0 开始
	return;
}

// ============================================================
// fifo8_put — 向 FIFO8 环形缓冲区写入一个字节
//
// 功能：将数据写入缓冲区尾部（p 指向的位置）。若缓冲区已满，
//       则设置溢出标志并返回 -1，数据被丢弃。
//
// 环形机制：
//   p 在写入后自增，到达 size 边界时回绕到 0，形成环形效果。
//   free 递减跟踪剩余空间（free == 0 表示满）。
//
// 参数：
//   fifo — 指向 FIFO8 结构体
//   data — 要写入的一个字节数据
//
// 返回：
//   0  — 写入成功
//   -1 — 缓冲区已满，数据被丢弃（溢出）
// ============================================================
int fifo8_put(struct FIFO8 *fifo, unsigned char data)
{
	if (fifo->free == 0) {          // 缓冲区已满？
		fifo->flags |= FLAGS_OVERRUN; // 设置溢出标志，flags原本是0，表示正常状态，设置后变为1，表示发生溢出
		return -1;                    // 丢弃数据，返回失败
	}
	fifo->buf[fifo->p] = data;      // 将数据写入当前位置
	fifo->p++;                       // 写入位置后移
	if (fifo->p == fifo->size) {     // 到达缓冲区末尾？
		fifo->p = 0;                 // 回绕到开头（环形）
	}
	fifo->free--;                    // 剩余空间减 1
	return 0;                        // 写入成功
}

int fifo8_get(struct FIFO8 *fifo)
{
    int data;
    if (fifo->free == fifo->size) { // 缓冲区为空？
        return -1;                    // 返回 -1 表示没有数据可读
    }
    data = fifo->buf[fifo->q];       // 从当前位置读取数据
    fifo->q++;                       // 读取位置后移
    if (fifo->q == fifo->size) {     // 到达缓冲区末尾？
        fifo->q = 0;                 // 回绕到开头（环形）
    }
    fifo->free++;                    // 剩余空间加 1
    return data;                     // 返回读取的数据
}

int fifo8_status(struct FIFO8 *fifo)
{
    return fifo->size - fifo->free; // 已使用空间 = 总大小 - 剩余空间
}