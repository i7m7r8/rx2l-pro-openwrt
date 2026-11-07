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
 * ---------------------------------------------------------------------------
 * FIELD LAYOUT
 * ---------------------------------------------------------------------------
 * Established byte-exactly from two decrypted vendor images (RX2L Pro
 * V16.03.51.17 and TX2L Pro V16.03.51.14) and re-proven against the genuine
 * RX2L Pro sample by tests/test_container.py case 10:
 *
 *   0x00  4  magic 27 05 19 56                       (constant)
 *   0x04  4  BE u32 - per-build, not content-derived (build id)
 *   0x08  4  BE u32 - per-build, not content-derived (build epoch)
 *   0x0c  4  BE u32 - payload length                 (computed)
 *   0x10  4  BE u32 0x80000000                       (constant)
 *   0x14  4  BE u32 - per-product kernel reference    (-k)
 *   0x18  4  BE u32 - CRC32 of the payload            (computed)
 *   0x1c  8  05 05 02 03 02 00 00 00                 (constant)
 *   0x20  1  fw_cls = 2 = FW_TYPE_KERNEL_IMAGE       (-f)
 *   0x24  4  BE u32 0x00031053 = fw.product.id        (-p)
 *   0x28  4  BE u32 - total file length (0x40+payl)   (computed)
 *   0x2c 20  zeros
 *   0x40 .. container begins here
 *
 * ---------------------------------------------------------------------------
 * WHAT THE STOCK VERIFIER ACTUALLY CHECKS (why every default below is a
 * hard requirement and not a stylistic choice)
 * ---------------------------------------------------------------------------
 * check_fw_type(buf, len) in libcommon.so (disassembled at 0xa824) derives the
 * image type FROM THIS HEADER, not from any CGI parameter. It memcpy's 0x44
 * bytes, then:
 *
 *   ntohl(+0x00) == 0x27051956  ->  fw_cls byte at +0x20 must be 2
 *                                    (FW_TYPE_KERNEL_IMAGE; its log line is
 *                                    "pimg_header->fw_cls:%x,
 *                                     FW_TYPE_KERNEL_IMAGE:%x.")
 *   ntohl(+0x00) == 0x27051958  ->  fw_cls at +0x20 must be 4
 *   raw   +0x00 == 0x48844884   ->  type 6 (dual-image package; this board has
 *                                    a single KernelFS, so type 6 is wrong)
 *   anything else               ->  -1 ("unknow fw type.",
 *                                        "get fw type is fail.")
 *
 * The ONLY rejection on the type-2 path is "Image is dual image,but flash not":
 * it fires when a dual-image helper returns 0 AND ntohl(buf+0x40) == 0x476f6431
 * (the ASCII "God1"). Our container begins with "cr6c", i.e.
 * ntohl(+0x40) == 0x63723663 != 0x476f6431, so type 2 is returned
 * unconditionally. That is an INVARIANT of this writer, enforced below, not an
 * accident of the payload.
 *
 * check_fw(buf, a1, buf, len) in libcommonprod.so (0x2a518) then runs, for
 * type 2, in this order:
 *   access("/tmp/UPGRADE_DECRY_FAIL_FLAG", F_OK) -> -1 if present (see NOTES)
 *   check_fw_valid(2, a1)   fail -> -1   (compares ntohl(+0x24) against
 *                                         strtoul(GetValue("fw.product.id"),16))
 *   check_fw_crc(2, buf, len) fail -> -2 (compares +0x18 against crc32(payload))
 *   check_mtd_size(2, a1)   fail -> -3   (bounds the image by the partition)
 *
 * NOTES on the flag file: decry_firm creates /tmp/UPGRADE_DECRY_FAIL_FLAG when
 * its RSA step fails, which it always does for a plaintext image, and the
 * webui's upgrade() ignores decry_firm's return value -- but check_fw reads the
 * file *first*. Whether the plaintext path therefore works is an open,
 * gate-dependent question owned by port/08-veto-paths; this writer cannot
 * influence it and must not assume it away.
 *
 * NOTES on 0x04/0x08/0x14: these words differ between vendor builds and are not
 * derivable from the payload (they are not checksums of it), so nothing can
 * verify them. -b and -k let a build pin them; the defaults keep the values
 * observed in this product's own image. None of the three is per-unit and none
 * is secret.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS TOOL ALSO VALIDATES BEFORE WRAPPING
 * ---------------------------------------------------------------------------
 * Wrapping a container the stage-1 loader refuses would produce a file that
 * installs and then never boots, so the loader's own two rules are re-derived
 * here (magic in {cr6c, cs6c}, BE-u16 additive sum over `len` bytes from +0x10
 * == 0 mod 2^16, start_addr is KSEG0) and the check_mtd_size bound is applied
 * before a single byte is written. Use -n only if you are deliberately
 * producing a non-bootable test fixture.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define HDR_LEN		0x40
#define SIG_LEN		16

/* check_fw_type() header-magic constants (host-order values after ntohl()). */
#define FW_MAGIC_KIMG	0x27051956u	/* bytes 27 05 19 56 -> fw_cls 2 */
#define FW_MAGIC_ALT	0x27051958u	/* bytes 27 05 19 58 -> fw_cls 4 */
#define FW_MAGIC_RAW6	0x48844884u	/* raw word, no ntohl -> type 6 */

/* check_fw_type() dual-image trap: "Image is dual image,but flash not". */
#define DUAL_IMAGE_TAG	0x476f6431u	/* ASCII "God1" at +0x40 */

/* fw_cls values, named as the vendor strings name them. */
#define FW_TYPE_KERNEL_IMAGE	2

/* mtd3 KernelFS: 0x040000..0xFC0000 -> 0xF80000 bytes. check_mtd_size() is
 * documented as "bounds the image by the partition"; which of the file total
 * or the payload it compares is still unverified, so we bound the PAYLOAD at
 * the window minus the header, which is correct under either reading. */
#define KERNELFS_MAX		0xF80000u

static const uint8_t sig[HDR_LEN] = {
	0x27, 0x05, 0x19, 0x56,
	0xcb, 0x70, 0x1d, 0xc8,	/* 0x04 build id    */
	0x68, 0x4f, 0xf8, 0x74,	/* 0x08 build epoch */
	0x00, 0x00, 0x00, 0x00,	/* 0x0c payload len  */
	0x80, 0x00, 0x00, 0x00,	/* 0x10 constant     */
	0x80, 0x8d, 0x45, 0xc0,	/* 0x14 kernel ref   */
	0x00, 0x00, 0x00, 0x00,	/* 0x18 crc32        */
	0x05, 0x05, 0x02, 0x03,	/* 0x1c constant     */
	0x02, 0x00, 0x00, 0x00,	/* 0x20 fw_cls + pad */
	0x00, 0x03, 0x10, 0x53,	/* 0x24 product id   */
	0x00, 0x00, 0x00, 0x00,	/* 0x28 total len    */
	0x00, 0x00, 0x00, 0x00,	/* 0x2c              */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

static void usage(const char *p)
{
	fprintf(stderr,
"Usage: %s [-i in.cr6c] [-o out.bin] [-c cr6c] [-k u32] [-b u32]\n"
"                [-f cls] [-p product-id] [-M max-payload] [-n] [-q]\n"
"  -i FILE  cvimg/cr6c container to wrap (default stdin)\n"
"  -o FILE  output (default stdout)\n"
"  -c STR   reject the input unless its first 4 bytes are STR (default cr6c)\n"
"  -k U32   header +0x14 kernel reference, hex or decimal (default 0x808d45c0)\n"
"  -b U32   header +0x04 and +0x08 build id/epoch (default: stock values)\n"
"  -f N     header +0x20 fw_cls. 2 => magic 27 05 19 56, 4 => 27 05 19 58.\n"
"           Anything else is rejected: check_fw_type() returns -1.\n"
"  -p U32   header +0x24 fw.product.id; check_fw_valid() requires it to equal\n"
"           the router's CFG value (default 0x00031053, this product)\n"
"  -M U32   maximum payload bytes, the check_mtd_size() bound\n"
"           (default 0xf80000 = the whole mtd3 KernelFS window)\n"
"  -n       skip the stage-1 loader validation of the payload (test fixtures\n"
"           only: the result installs and then does not boot)\n"
"  -q       quiet\n",
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

/*
 * The stage-1 loader's checksum loop verbatim (mtd1 disassembly, and the same
 * rule scripts/rx2lpro-image-gates.py and tools/check-cr6c.py re-derive):
 * additive sum of BIG-ENDIAN u16 words over `len` bytes starting at
 * container + 0x10, mod 2^16, pass == 0. A trailing odd byte counts as b0<<8.
 */
static uint16_t loader_sum(const uint8_t *data, size_t off, size_t len)
{
	unsigned total = 0;
	size_t i;

	for (i = 0; i + 1 < len; i += 2)
		total = (total + ((data[off + i] << 8) | data[off + i + 1])) & 0xFFFF;
	if (len & 1)
		total = (total + (data[off + len - 1] << 8)) & 0xFFFF;
	return (uint16_t) total;
}

/*
 * Validate the payload as the boot ROM would, before we agree to put a Tenda
 * header in front of it. Returns 0 when the container is bootable.
 */
static int check_container(const uint8_t *pay, size_t len, const char *want_sig,
			   int *kernel_only, int verbose)
{
	uint16_t sum;
	uint32_t start, burn, clen;

	if (len < SIG_LEN) {
		fprintf(stderr, "Error: payload too short for a cvimg header (%zu)\n", len);
		return -1;
	}
	if (memcmp(pay, want_sig, 4)) {
		fprintf(stderr, "Error: payload does not start with '%s' (got %.4s) - "
			"wrong image or wrong -c\n", want_sig, pay);
		return -1;
	}
	/*
	 * check_fw_type()'s single rejection on the type-2 path. If the bytes at
	 * container +0 (file +0x40) were "God1", the webui prints
	 * "Image is dual image,but flash not" and refuses us. -c cr6c already
	 * excludes it; this check makes the invariant explicit and survives
	 * somebody widening -c.
	 */
	if (get_be32(pay) == DUAL_IMAGE_TAG) {
		fprintf(stderr, "Error: payload begins with 'God1' - check_fw_type() "
			"would print 'Image is dual image,but flash not' and refuse "
			"the image on this single-KernelFS board\n");
		return -1;
	}
	if (memcmp(pay, "cr6c", 4) && memcmp(pay, "cs6c", 4)) {
		fprintf(stderr, "Error: stage-1 loader accepts only cr6c/cs6c at "
			"container +0 (got %.4s); anything else prints "
			"'No sys signature at %%X!'\n", pay);
		return -1;
	}

	start = get_be32(pay + 0x04);
	burn  = get_be32(pay + 0x08);
	clen  = get_be32(pay + 0x0c);

	if (clen == 0) {
		fprintf(stderr, "Error: container len (+0x0c) is 0, loader refuses\n");
		return -1;
	}
	if ((size_t) SIG_LEN + clen > len) {
		fprintf(stderr, "Error: container len %#x runs past the payload (%#zx)\n",
			clen, len);
		return -1;
	}
	/*
	 * start_addr is BOTH the jump target and the copy destination base
	 * (stage-1 copies to start_addr | 0x20000000), so it must stay in KSEG0.
	 * burn_addr is read, byteswapped and never used -- recorded only.
	 */
	if ((start & 0xF0000000u) != 0x80000000u) {
		fprintf(stderr, "Error: start_addr %#010x is not KSEG0; |0x20000000 "
			"would not alias the same RAM\n", start);
		return -1;
	}
	sum = loader_sum(pay, SIG_LEN, clen);
	if (sum != 0) {
		fprintf(stderr, "Error: loader checksum gate fails: BE-u16 sum over "
			"%#x bytes from +0x10 = %#04x, must be 0 "
			"('check failed, ret:%%d, sum:%%x')\n", clen, sum);
		return -1;
	}

	if (kernel_only)
		*kernel_only = !memcmp(pay, "cs6c", 4);

	if (verbose)
		printf("container ok: sig %.4s start_addr=%#010x burn_addr=%#010x "
		       "len=%#x (copy %#x), BE-u16 sum == 0\n",
		       pay, start, burn, clen, clen - 2);
	return 0;
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

/* Map the fw_cls byte to the header magic check_fw_type() pairs it with. */
static uint32_t magic_for_cls(unsigned cls)
{
	switch (cls) {
	case FW_TYPE_KERNEL_IMAGE: return FW_MAGIC_KIMG;	/* 2 -> ...56 */
	case 4:			  return FW_MAGIC_ALT;	/* 4 -> ...58 */
	default:			  return 0;
	}
}

int main(int argc, char *argv[])
{
	const char *in = "-", *out = "-", *want_sig = "cr6c";
	uint32_t kref = 0x808d45c0, build = 0;
	uint32_t product_id = 0x00031053, max_payload = KERNELFS_MAX;
	unsigned cls = FW_TYPE_KERNEL_IMAGE;
	int opt, rc, skip_loader_check = 0, quiet = 0, kernel_only = 0;
	size_t len, total;
	uint32_t magic;
	uint8_t *pay, hdr[HDR_LEN];

	while ((opt = getopt(argc, argv, "i:o:c:k:b:f:p:M:nq")) != -1) {
		switch (opt) {
		case 'i': in = optarg; break;
		case 'o': out = optarg; break;
		case 'c': want_sig = optarg; break;
		case 'k': kref = (uint32_t) strtoull(optarg, NULL, 0); break;
		case 'b': build = (uint32_t) strtoull(optarg, NULL, 0); break;
		case 'f': cls = (unsigned) strtoul(optarg, NULL, 0); break;
		case 'p': product_id = (uint32_t) strtoull(optarg, NULL, 0); break;
		case 'M': max_payload = (uint32_t) strtoull(optarg, NULL, 0); break;
		case 'n': skip_loader_check = 1; break;
		case 'q': quiet = 1; break;
		default: usage(argv[0]);
		}
	}

	if (strlen(want_sig) != 4) {
		fprintf(stderr, "Error: -c must be exactly 4 characters\n");
		return 2;
	}

	/*
	 * check_fw_type() pairs the magic with the class byte. Emitting cls 4
	 * under the ...56 magic is the single edit that turns an otherwise
	 * perfect image into "unknow fw type.", so the pair is derived here and
	 * never taken from two independent options.
	 */
	magic = magic_for_cls(cls);
	if (!magic) {
		fprintf(stderr, "Error: -f %u is not a fw_cls check_fw_type() accepts "
			"with a 27 05 19 xx header (2 or 4; type 6 is the dual-image "
			"0x48844884 layout and this board has one KernelFS)\n", cls);
		return 2;
	}
	if (!max_payload) {
		fprintf(stderr, "Error: -M must be non-zero\n");
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
	if (len > max_payload) {
		fprintf(stderr, "Error: payload %#zx exceeds the check_mtd_size() "
			"bound %#x (mtd3 KernelFS); the image would be rejected by "
			"the webui, or would not fit the partition\n",
			len, max_payload);
		free(pay);
		return 2;
	}
	if (!skip_loader_check) {
		rc = check_container(pay, len, want_sig, &kernel_only, !quiet);
		if (rc) {
			free(pay);
			return 2;
		}
	}

	memcpy(hdr, sig, HDR_LEN);
	/* +0x00: magic chosen by -f, big-endian on disk, exactly as the vendor
	 * image stores it (ntohl() on the device recovers FW_MAGIC_*). */
	put_be32(hdr + 0x00, magic);
	if (build) {
		put_be32(hdr + 0x04, build);
		put_be32(hdr + 0x08, build);
	}
	put_be32(hdr + 0x0c, (uint32_t) len);
	put_be32(hdr + 0x14, kref);
	put_be32(hdr + 0x18, crc32_buf(pay, len, 0));
	/* +0x1c is five constant bytes; +0x20 is fw_cls, +0x21..23 stay zero. */
	hdr[0x1c] = 0x05;
	hdr[0x1d] = 0x05;
	hdr[0x1e] = 0x02;
	hdr[0x1f] = 0x03;
	hdr[0x20] = (uint8_t) cls;
	hdr[0x21] = 0x00;
	hdr[0x22] = 0x00;
	hdr[0x23] = 0x00;
	put_be32(hdr + 0x24, product_id);
	total = HDR_LEN + len;
	if (total > 0xFFFFFFFFu) {
		fprintf(stderr, "Error: image too large\n");
		free(pay);
		return 2;
	}
	put_be32(hdr + 0x28, (uint32_t) total);

	/*
	 * Self-verification of what we just assembled. Everything here is
	 * derived, so a mismatch is a bug in this tool, not in the input, and it
	 * must stop the build rather than ship a file the router rejects.
	 */
	if (get_be32(hdr + 0x0c) != len ||
	    get_be32(hdr + 0x28) != total ||
	    get_be32(hdr + 0x18) != crc32_buf(pay, len, 0) ||
	    hdr[0x20] != cls ||
	    get_be32(hdr + 0x24) != product_id ||
	    get_be32(hdr) != magic) {
		fprintf(stderr, "Error: internal header self-check failed\n");
		free(pay);
		return 2;
	}
	/* The God1 invariant, stated on the OUTPUT file's coordinates: the four
	 * bytes at +0x40 are the container's first four bytes. */
	if (get_be32(pay) == DUAL_IMAGE_TAG ||
	    memcmp(pay, "God1", 4) == 0) {
		fprintf(stderr, "Error: +0x40 is 'God1' (=%#010x), which is the one "
			"dual-image rejection on the type-2 path\n", DUAL_IMAGE_TAG);
		free(pay);
		return 2;
	}

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

	if (!rc && !quiet)
		printf("Wrapped %s: payload %#zx bytes, signature %.4s%s, fw_cls %u, "
		       "product id %#010x, crc32 %#010x, total %#x -> %s\n",
		       in, len, pay, kernel_only ? " (kernel-only container)" : "",
		       cls, product_id, get_be32(hdr + 0x18),
		       (unsigned) total, out);
	free(pay);
	return rc;
}
