/* anssequencia: lê blocos isolados (1 página) numa sequência e mostra a assinatura
 * de cada um, para ver se a resposta corresponde ao comando atual ou ao anterior. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned h(const unsigned char *p)
{
	unsigned x = 2166136261u;
	for (int i = 0; i < 4096; i++) x = (x ^ p[i]) * 16777619u;
	return x;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/ans0", O_RDONLY | O_DIRECT);
	unsigned char *p;
	if (fd < 0 || posix_memalign((void **)&p, 4096, 4096)) return 1;
	unsigned char *lixo = mmap(NULL, 8 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(lixo, 1, 8 << 20);
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == 'v') {	/* varre 8 MB: expulsa os caches */
			for (int j = 0; j < (8 << 20); j += 64) lixo[j]++;
			printf("(varreu 8 MB)\n");
			continue;
		}
		int lba = atoi(argv[i]);
		memset(p, 0xAA, 4096);
		pread(fd, p, 4096, (off_t)lba * 4096);
		printf("lba %3d -> %08x  %02x%02x%02x%02x%02x%02x%02x%02x\n", lba, h(p),
		       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
	}
	return 0;
}
