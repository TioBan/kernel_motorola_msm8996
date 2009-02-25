#ifndef _XT_STATISTIC_H
#define _XT_STATISTIC_H

#include <linux/types.h>

enum xt_statistic_mode {
	XT_STATISTIC_MODE_RANDOM,
	XT_STATISTIC_MODE_NTH,
	__XT_STATISTIC_MODE_MAX
};
#define XT_STATISTIC_MODE_MAX (__XT_STATISTIC_MODE_MAX - 1)

enum xt_statistic_flags {
	XT_STATISTIC_INVERT		= 0x1,
};
#define XT_STATISTIC_MASK		0x1

struct xt_statistic_info {
	__u16			mode;
	__u16			flags;
	union {
		struct {
			__u32	probability;
		} random;
		struct {
			__u32	every;
			__u32	packet;
			/* Used internally by the kernel */
			__u32	count;
		} nth;
	} u;
	struct xt_statistic_info	*master __attribute__((aligned(8)));
};

#endif /* _XT_STATISTIC_H */
