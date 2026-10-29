/*
 * stm32h743xx_flash.ld — STM32H743xx (2MB Flash / 1MB SRAM) GCC 链接脚本
 *
 * 内存映射（STM32H743 数据手册）：
 *   FLASH  : 0x08000000, 2MB   （主 Flash，AXIM 总线，代码执行）
 *   ITCMRAM: 0x00000000, 64KB （指令紧耦合，0 等待，可放关键代码/中断向量）
 *   DTCMRAM: 0x20000000, 128KB（数据紧耦合，0 等待）
 *   SRAM1  : 0x30000000, 240KB（D1 域，主 SRAM）
 *   SRAM2  : 0x30040000, 64KB （D3 域）
 *   SRAM3  : 0x30040000+，但 H743 是 240+64+32=... 实际 1MB 含 SRAM1(240)+SRAM2(64)+SRAM3(32)+DTCM(128)+ITCM(64)
 *
 * 本工程（地面站）内存分配：
 *   - .text/.rodata → FLASH（2MB 够用）
 *   - .data → DTCMRAM（128KB，0 等待，快）
 *   - .bss  → DTCMRAM + SRAM1（栈/堆/全局变量）
 *   - 显存（LTDC framebuffer）后续放外部 SDRAM @0xC0000000（M2 阶段配 FMC 后）
 *
 * 栈：放 DTCMRAM 顶部，1KB 红区溢出检测。
 */
ENTRY(Reset_Handler)

_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);   /* 栈顶 = DTCMRAM 末 */
_Min_Heap_Size  = 0x400;   /* 1KB heap */
_Min_Stack_Size = 0x2000;  /* 8KB stack（G5：LVGL 页构建调用深度实测 >4KB → STKERR，扩容） */

MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  ITCMRAM  (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
  DTCMRAM  (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  AXIRAM   (xrw) : ORIGIN = 0x24000000, LENGTH = 320K   /* D1 域 AXI SRAM（G4：渲染小块）*/
  SRAM1    (xrw) : ORIGIN = 0x30000000, LENGTH = 240K
  SRAM2    (xrw) : ORIGIN = 0x30040000, LENGTH = 64K
  /* 外部 SDRAM（FMC Bank1 Region 1，M2 阶段启用后用）*/
  SDRAM    (xrw) : ORIGIN = 0xC0000000, LENGTH = 8192K
}

SECTIONS
{
  /* G4：LVGL PARTIAL 渲染小块（AXI，不碰扫描中的帧缓冲） */
  .axi_pool (NOLOAD) :
  {
    . = ALIGN(32);
    KEEP(*(.axi_pool))
    . = ALIGN(32);
  } > AXIRAM

  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >DTCMRAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >DTCMRAM

  /* DMA 缓冲等放 SRAM1（D-Cache 对齐区）*/
  .dma_ram (NOLOAD) :
  {
    . = ALIGN(4);
    *(.dma_ram)
    *(.dma_ram*)
    . = ALIGN(4);
  } >SRAM1

  /* 显存放 SDRAM（M2 阶段使用，NOLOAD 不进 bin）*/
  .sdram (NOLOAD) :
  {
    . = ALIGN(4);
    *(.sdram)
    *(.sdram*)
    . = ALIGN(4);
  } >SDRAM

  .heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM

  _Heap_Stack_Limit = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM) - _Min_Heap_Size - _Min_Stack_Size;
  _Heap_Stack_Size   = _Min_Heap_Size + _Min_Stack_Size;

  /* 检查栈堆不溢出 DTCMRAM */
  ASSERT(_Heap_Stack_Limit >= _ebss, "ERROR: DTCMRAM 溢出（heap+stack 超 bss 末）")
}
