# GRNB

GRNB是一个很小很简单的用于X86的bootloader. 它可以完成GRUB的一些工作, 但做了很多简化.

那么, 为什么不直接使用GRUB或GRUB4DOS呢? 因为:
 1. 使用不便. 加载一个简单的ELF格式的RTOS都很麻烦.
 2. 不稳定. 在我自己的板子上总是随机出现莫名其妙的问题.
 3. 很难调试. GRUB/GRUB4DOS代码庞大而复杂, 很难定位问题所在. 甚至还挑编译器版本.
 4. 重新写个bootloader很好玩.

但GRNB不准备去替代GRUB/GRUB4DOS. 那太复杂了. 它就是一个玩具.

目前GRNB可以:
    识别MBR/GPT分区.
    访问FAT/NTFS文件系统.
    启动ELF文件.
    chainloader其他系统.

TODO:
    EXT文件系统.
    启动linux.
    启动更多类型的系统.
    网络TFTP启动.


