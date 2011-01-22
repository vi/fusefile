all:
	gcc `pkg-config fuse --cflags --libs` fusefile.c -o fusefile -g3

prefix=/usr

install:
	install fusefile ${prefix}/bin/	

test:
	rm -Rf m qqq
	touch m
	echo 123 > qqq
	./fusefile qqq m
	read Q < m && test "$$Q" == "123"
	fusermount -u m
	./fusefile qqq m -r -O 2
	read Q < m && test "$$Q" == "3"
	fusermount -u m
	./fusefile qqq m -r -S 2
	read Q < m ; test "$$Q" == "12"
	fusermount -u m
	./fusefile qqq m -r -S 1 -O 1
	read Q < m ; test "$$Q" == "2"
	fusermount -u m
	./fusefile qqq m -w -S 1 -O 1
	echo -n 5 > m
	fusermount -u m
	read Q < qqq ; test "$$Q" == "153"
	echo 123 > qqq
	./fusefile /dev/zero m -r -S $$((1024*1024*1024*4)) -M 0100600
	dd if=m of=/dev/null skip=2047 bs=1M count=2
	fusermount -u m
	rm -Rf m qqq

