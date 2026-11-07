/*
 * tenda-wrap - emit the 64-byte Tenda plaintext-image header in front of a
 * Realtek cvimg ("cr6c") KernelFS container, producing a file shaped exactly
 * like the output of the vendor's own /bin/decry_firm.
 *
 * Copyright (C) 2026 rx2l-pro-openwrt contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Field layout, established byte-exactly from two decrypted vendor images
 * (RX2L Pro V16.03.51.17 and TX2L Pro V16.03.51.14):
 *
 *   0x00  4  magic 27 05 19 56                       (constant)
 *   0x04  4  BE u32 - per-build, not content-derived (build id)
 *   0x08  4  BE u32 - per-build, not content-derived (build epoch)
 *   0x0c  4  BE u32 - payload length
 *   0x10  4  BE u32 0x80000000                       (constant)
 *   0x14  4  BE u32 - per-product kernel reference (see NOTES below)
 *   0x18  4  BE u32 - CRC32 of the payload           (verified on both samples)
 *   0x1c  8  05 05 02 03 02 00 00 00                 (constant)
 *   0x24  4  BE u32 0x00031053                       (constant)
 *   0x28  4  BE u32 - total file length (0x40 + payload)
 *   0x2c 20  zeros
 *
 * NOTES on 0x04/0x08/0x14: these three words differ between the two samples
 * and are not derivable from the payload (they are not checksums of it), so
 * nothing can verify them offline. -b and -k let a build pin them; the
 * defaults keep the values observed in the stock RX2L Pro image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define HDR_LEN		0x40
#define SIG_LEN		16

static const uint8_t sig[HDR_LEN] = {
	0x27, 0x05, 0x19, 0x56,
	0xcb, 0x70, 0x1d, 0xc8,	/* 0x04 build id   */
	0x68, 0x4f, 0xf8, 0x74,	/* 0x08 build epoch*/
	0x00, 0x00, 0x00, 0x00,	/* 0x0c payload len */
	0x80, 0x00, 0x00, 0x00,	/* 0x10 */
	0x80, 0x8d, 0x45, 0xc0,	/* 0x14 kernel ref  */
	0x00, 0x00, 0x00, 0x00,	/* 0x18 crc32       */
	0x05, 0x05, 0x02, 0x03,	/* 0x1c */
	0x02, 0x00, 0x00, 0x00,	/* 0x20 */
	0x00, 0x03, 0x10, 0x53,	/* 0x24 */
	0x00, 0x00, 0x00, 0x00,	/* 0x28 total len   */
	0x00, 0x00, 0x00, 0x00,	/* 0x2c */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

static void usage(const char *p)
{
	fprintf(stderr,
		"Usage: %s [-i in.cr6c] [-o out.bin] [-c cr6c] [-k u32] [-b u32]\n"
		"  -i FILE  cvimg/cr6c container to wrap (default stdin)\n"
		"  -o FILE  output (default stdout)\n"
		"  -c STR   reject the input unless its first 4 bytes are STR (default cr6c)\n"
		"  -k U32   value for header 0x14 (kernel reference), hex or decimal\n"
		"  -b U32   value for header 0x04 and 0x08 (build id/epoch)\n",
		p);
	exit(1);
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len, uint32_t seed)
{
	static uint32_t table[256];
	static int ready;
	uint32_t c = seed ^ 0xFFFFFFFF;
	size_t i;

	if (!ready) {
		for (i = 0; i < 256; i++) {
			uint32_t v = i;
			int j;
			for (j = 0; j < 8; j++)
				v = (v & 1) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
			table[i] = v;
		}
		ready = 1;
	}
	for (i = 0; i < len; i++)
		c = table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFF;
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static uint32_t get_be32(const uint8_t *p)
{
	return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/* Read a whole FILE into memory. Returns NULL on failure. */
static uint8_t *slurp(const char *path, size_t *len)
{
	FILE *f;
	uint8_t *buf;
	long sz;
	size_t got;

	if (!strcmp(path, "-")) {
		size_t cap = 1 << 20, n = 0;
		buf = malloc(cap);
		if (!buf)
			return NULL;
		while ((got = fread(buf + n, 1, cap - n, stdin)) > 0) {
			n += got;
			if (n == cap) {
				cap *= 2;
				buf = realloc(buf, cap);
				if (!buf)
					return NULL;
			}
		}
		*len = n;
		return buf;
	}

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Error: cannot open %s: %s\n", path, strerror(errno));
		return NULL;
	}
	if (fseek(f, 0, SEEK_END)) {
		fclose(f);
		return NULL;
	}
	sz = ftell(f);
	rewind(f);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	buf = malloc(sz ? (size_t) sz : 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	if (fread(buf, 1, sz, f) != (size_t) sz) {
		fprintf(stderr, "Error: short read on %s\n", path);
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t) sz;
	return buf;
}

int main(int argc, char *argv[])
{
	const char *in = "-", *out = "-", *want_sig = "cr6c";
	uint32_t kref = 0x808d45c0, build = 0;
	int opt, rc;
	size_t len, total;
	uint8_t *pay, hdr[HDR_LEN];

	while ((opt = getopt(argc, argv, "i:o:c:k:b")) != -1) {
		switch (opt) {
		case 'i': in = optarg; break;
		case 'o': out = optarg; break;
		case 'c': want_sig = optarg; break;
		case 'k': kref = (uint32_t) strtoull(optarg, NULL, 0); break;
		case 'b': build = (uint32_t) strtoull(optarg, NULL, 0); break;
		default: usage(argv[0]);
		}
	}

	if (strlen(want_sig) != 4) {
		fprintf(stderr, "Error: -c must be exactly 4 characters\n");
		return 2;
	}

	pay = slurp(in, &len);
	if (!pay)
		return 2;
	if (len < SIG_LEN) {
		fprintf(stderr, "Error: payload too short (%zu bytes)\n", len);
		free(pay);
		return 2;
	}
	if (memcmp(pay, want_sig, 4)) {
		fprintf(stderr, "Error: payload does not start with '%s' (got %.4s) - "
			"wrong image or wrong -c\n", want_sig, pay);
		free(pay);
		return 2;
	}

	memcpy(hdr, sig, HDR_LEN);
	if (build) {
		put_be32(hdr + 0x04, build);
		put_be32(hdr + 0x08, build);
	}
	put_be32(hdr + 0x0c, (uint32_t) len);
	put_be32(hdr + 0x14, kref);
	put_be32(hdr + 0x18, crc32_buf(pay, len, 0));
	total = HDR_LEN + len;
	if (total > 0xFFFFFFFFu) {
		fprintf(stderr, "Error: image too large\n");
		free(pay);
		return 2;
	}
	put_be32(hdr + 0x28, (uint32_t) total);

	rc = 0;
	if (strcmp(out, "-")) {
		FILE *f = fopen(out, "wb");
		if (!f) {
			fprintf(stderr, "Error: cannot write %s: %s\n", out, strerror(errno));
			free(pay);
			return 2;
		}
		if (fwrite(hdr, 1, HDR_LEN, f) != HDR_LEN || fwrite(pay, 1, len, f) != len)
			rc = 2;
		if (fclose(f) && !rc)
			rc = 2;
	} else {
		if (fwrite(hdr, 1, HDR_LEN, stdout) != HDR_LEN || fwrite(pay, 1, len, stdout) != len)
			rc = 2;
	}

	if (!rc)
		printf("Wrapped %s: payload %#zx bytes, signature %.4s, crc32 %#010x -> %s\n",
		       in, len, pay, get_be32(hdr + 0x18), out);
	free(pay);
	return rc;
}
