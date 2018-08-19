CC = gcc -Wall -Wextra -std=c11

evd: evd.c config.h
	${CC} ${CFLAGS} $< -o $@

install: evd
	install -m755 evd /usr/bin/
	install -m644 evd.1 /usr/share/man/man1/

uninstall:
	rm -f \
		/usr/bin/evd \
		/usr/share/man/man1/evd.1 \

clean:
	rm -f evd
