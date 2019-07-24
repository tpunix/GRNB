
all: diskimg testgrnb

diskimg:
	cp -f disk.img testdisk.img
	dd conv=notrunc if=../mbr.bin  of=testdisk.img bs=1   count=446 
	dd conv=notrunc if=../grnb.tlz of=testdisk.img bs=512 seek=1

# testgrnb.bin can be load by grub
testgrnb:
	cat ../mbr.bin ../mbr.bin ../grnb.tlz > testgrnb.bin
	dd conv=notrunc if=../mbr.bin of=testgrnb.bin bs=1 seek=432 count=1

clean:
	rm -f testdisk.img
	rm -f testgrnb.bin

