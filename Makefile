main:
	rm -rf xbin.so
	gcc -g -fPIC -shared xbin.c -o xbin.so
	sqlite3 -init test.sql

clean:
	rm -rf xbin.so
