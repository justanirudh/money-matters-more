all:	prog

prog: helper_functions.c transfer_program.c
	gcc -pthread helper_functions.c transfer_program.c -o perform_transfers

clean:
	rm perform_transfers
