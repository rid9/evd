CC = gcc -Wall -Wextra -std=c99

ksd: ksd.c config.h
	${CC} ${CFLAGS} $< -o $@

install: ksd
	install -m755 ksd /usr/bin/
	install -m644 ksd.1 /usr/share/man/man1/
	[ -d /etc/systemd/system ] && \
		install -m644 ksd.service /etc/systemd/system/

uninstall:
	rm -f \
		/usr/bin/ksd \
		/usr/share/man/man1/ksd.1 \
		/etc/systemd/system/ksd.service

clean:
	rm -f ksd
