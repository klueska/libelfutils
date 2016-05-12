all: soinfo ldcache

soinfo: soinfo.c
	gcc -std=gnu99 -o $@ $^ -lelf

ldcache: ldcache.c
	gcc -std=gnu99 -o $@ $^ -lelf

clean:
	rm -rf soinfo ldcache
