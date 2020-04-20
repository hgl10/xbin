main:
	rm -rf xbin.so
	gcc -O3 -fPIC -shared xbin.c -o xbin.so
	./sqlite3 -init test.sql

clean:
	rm -rf xbin.so
