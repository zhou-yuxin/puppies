main.hex: main.rel pwm.rel
	sdcc main.rel pwm.rel
	packihx main.ihx > main.hex

pwm.rel: src/pwm.h src/pwm.c
	sdcc -c src/pwm.c -Isrc

main.rel: src/pwm.h src/main.c
	sdcc -c src/main.c -Isrc