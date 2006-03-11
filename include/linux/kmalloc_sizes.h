#if (PAGE_SIZE == 4096)
	CACHE(32)
#endif
	CACHE(64)
#if L1_CACHE_BYTES < 64
	CACHE(96)
#endif
	CACHE(128)
#if L1_CACHE_BYTES < 128
	CACHE(192)
#endif
	CACHE(256)
	CACHE(512)
	CACHE(1024)
	CACHE(2048)
	CACHE(4096)
	CACHE(8192)
	CACHE(16384)
	CACHE(32768)
	CACHE(65536)
	CACHE(131072)
#if (NR_CPUS > 512) || (MAX_NUMNODES > 256) || !defined(CONFIG_MMU)
	CACHE(262144)
#endif
#ifndef CONFIG_MMU
	CACHE(524288)
	CACHE(1048576)
#ifdef CONFIG_LARGE_ALLOCS
	CACHE(2097152)
	CACHE(4194304)
	CACHE(8388608)
	CACHE(16777216)
	CACHE(33554432)
#endif /* CONFIG_LARGE_ALLOCS */
#endif /* CONFIG_MMU */
