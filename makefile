vmm : vmm.c vmm.h req
	gcc -g vmm.c -o vmm
req : req.c vmm.h
	gcc -g req.c -o req
clean : vmm
	rm -f vmm req